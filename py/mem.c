
#include <assert.h>
#include <stdio.h>
#include <string.h>

#include "py/mpstate.h"
#include "py/obj.h"
#include "py/runtime.h"
#include "py/mem.h"

#if 0 // print debugging info
#define DEBUG_PRINT (1)
#define DEBUG_printf DEBUG_printf
#else // don't print debugging info
#define DEBUG_PRINT (0)
#define DEBUG_printf(...) (void)0
#endif

// make this 1 to dump the heap each time it changes
#define EXTENSIVE_HEAP_PROFILING (0)

#define WORDS_PER_BLOCK (4)
#define BYTES_PER_BLOCK (WORDS_PER_BLOCK * BYTES_PER_WORD)


#define AT_FREE (0)
#define AT_HEAD (1)
#define AT_TAIL (2)
#define AT_MARK (3)

#define BLOCKS_PER_ATB (4)
#define ATB_MASK_0 (0x03)
#define ATB_MASK_1 (0x0c)
#define ATB_MASK_2 (0x30)
#define ATB_MASK_3 (0xc0)

#define ATB_0_IS_FREE(a) (((a) & ATB_MASK_0) == 0)
#define ATB_1_IS_FREE(a) (((a) & ATB_MASK_1) == 0)
#define ATB_2_IS_FREE(a) (((a) & ATB_MASK_2) == 0)
#define ATB_3_IS_FREE(a) (((a) & ATB_MASK_3) == 0)

/**
 *  /brief      ATB Access Macros
 *
 *              These macros are designed to mark "blocks" in the allocation table
 *              as used, free or marked (during garbage collection).
 *
 *              See the documentation for the Allocation Table for more information.
 */
#define BLOCK_SHIFT(block) (2 * ((block) & (BLOCKS_PER_ATB - 1)))
#define ATB_GET_KIND(block) ((MP_STATE_MEM(gc_alloc_table_start)[(block) / BLOCKS_PER_ATB] >> BLOCK_SHIFT(block)) & 3)
#define ATB_ANY_TO_FREE(block) do { MP_STATE_MEM(gc_alloc_table_start)[(block) / BLOCKS_PER_ATB] &= (~(AT_MARK << BLOCK_SHIFT(block))); } while (0)
#define ATB_FREE_TO_HEAD(block) do { MP_STATE_MEM(gc_alloc_table_start)[(block) / BLOCKS_PER_ATB] |= (AT_HEAD << BLOCK_SHIFT(block)); } while (0)
#define ATB_FREE_TO_TAIL(block) do { MP_STATE_MEM(gc_alloc_table_start)[(block) / BLOCKS_PER_ATB] |= (AT_TAIL << BLOCK_SHIFT(block)); } while (0)
#define ATB_HEAD_TO_MARK(block) do { MP_STATE_MEM(gc_alloc_table_start)[(block) / BLOCKS_PER_ATB] |= (AT_MARK << BLOCK_SHIFT(block)); } while (0)
#define ATB_MARK_TO_HEAD(block) do { MP_STATE_MEM(gc_alloc_table_start)[(block) / BLOCKS_PER_ATB] &= (~(AT_TAIL << BLOCK_SHIFT(block))); } while (0)

#define BLOCK_FROM_PTR(ptr) (((ptr) - (mp_uint_t)MP_STATE_MEM(gc_pool_start)) / BYTES_PER_BLOCK)
#define PTR_FROM_BLOCK(block) (((block) * BYTES_PER_BLOCK + (mp_uint_t)MP_STATE_MEM(gc_pool_start)))
#define ATB_FROM_BLOCK(bl) ((bl) / BLOCKS_PER_ATB)

#if MICROPY_ENABLE_FINALISER
/**
 * /brief           The "finalizer" is the method that is called when an object is
 *                  freed/deleted. In python, it is created by creating the __del__
 *                  method.
 */
// FTB = finaliser table byte
// if set, then the corresponding block may have a finaliser

#define BLOCKS_PER_FTB (8)

#define FTB_GET(block) ((MP_STATE_MEM(gc_finaliser_table_start)[(block) / BLOCKS_PER_FTB] >> ((block) & 7)) & 1)
#define FTB_SET(block) do { MP_STATE_MEM(gc_finaliser_table_start)[(block) / BLOCKS_PER_FTB] |= (1 << ((block) & 7)); } while (0)
#define FTB_CLEAR(block) do { MP_STATE_MEM(gc_finaliser_table_start)[(block) / BLOCKS_PER_FTB] &= (~(1 << ((block) & 7))); } while (0)
#endif

/*----------------------------------------------------------------------------*/
/* return 1 if the pointer falls within the memory pool                       */
#define VERIFY_PTR(ptr) ( \
        (ptr & (BYTES_PER_BLOCK - 1)) == 0          /* must be aligned on a block */ \
        && ptr >= (mp_uint_t)MP_STATE_MEM(gc_pool_start)     /* must be above start of pool */ \
        && ptr < (mp_uint_t)MP_STATE_MEM(gc_pool_end)        /* must be below end of pool */ \
    )

mp_uint_t mem_first(){
    for (mp_uint_t block = 0; block < MP_STATE_MEM(gc_alloc_table_byte_len) * BLOCKS_PER_ATB; block++) {
        if(ATB_GET_KIND(block) == AT_MARK || ATB_GET_KIND(block) == AT_HEAD){return block;}
    }
    return MEM_BLOCK_ERROR;
}

mp_uint_t mem_sizeof(mp_uint_t block){
    mp_uint_t n_blocks = 0;
    do {
        n_blocks += 1;
    } while (ATB_GET_KIND(block + n_blocks) == AT_TAIL);
    return n_blocks * BYTES_PER_BLOCK;
}

/*----------------------------------------------------------------------------*/
/**
 *  get void pointer from block
 */
void *mem_void_p(mp_uint_t block){
    assert(VERIFY_PTR(PTR_FROM_BLOCK(block)));
    assert(ATB_GET_KIND(block) == AT_MARK || ATB_GET_KIND(block) == AT_HEAD);
    return (void *)PTR_FROM_BLOCK(block);
}

/*----------------------------------------------------------------------------*/
/**
 *  get the next allocated block of memory
 */
mp_uint_t mem_next(mp_uint_t block){
    block++;
    for (; block < MP_STATE_MEM(gc_alloc_table_byte_len) * BLOCKS_PER_ATB; block++) {
        switch (ATB_GET_KIND(block)) {
            case AT_HEAD:
            case AT_MARK:
                return block;
        }
    }
    return MEM_BLOCK_ERROR;
}

int8_t mem_valid(mp_uint_t block){
    if(block >= MP_STATE_MEM(gc_alloc_table_byte_len) * BLOCKS_PER_ATB){return 0;}
    if((ATB_GET_KIND(block) != AT_HEAD) && (ATB_GET_KIND(block) != AT_MARK)){return 0;}
    return 1;
}

int8_t mem_ptr_valid(void *ptr){
    return VERIFY_PTR((mp_uint_t) ptr);
}

/*----------------------------------------------------------------------------*/
/**
 *  The following three functions are for setting, clearing and getting the
 *  "mark" used during garbage collection
 */
void mem_set_mark(mp_uint_t block){
    assert(ATB_GET_KIND(block) == AT_HEAD);
    ATB_HEAD_TO_MARK(block);
}

void mem_clear_mark(mp_uint_t block){
    assert(ATB_GET_KIND(block) == AT_MARK);
    ATB_MARK_TO_HEAD(block);
}

int8_t mem_get_mark(mp_uint_t block){
    assert(ATB_GET_KIND(block) == AT_MARK || ATB_GET_KIND(block) == AT_HEAD);
    return ATB_GET_KIND(block) == AT_MARK;
}

void mem_free(void *ptr_in) {
    if (VERIFY_PTR((mp_uint_t)ptr_in)) {
        mp_uint_t block = BLOCK_FROM_PTR((mp_uint_t) ptr_in);
        if (ATB_GET_KIND(block) == AT_HEAD) {
            // set the last_free pointer to this block if it's earlier in the heap
            if (block / BLOCKS_PER_ATB < MP_STATE_MEM(gc_last_free_atb_index)) {
                MP_STATE_MEM(gc_last_free_atb_index) = block / BLOCKS_PER_ATB;
            }
            // free head and all of its tail blocks
            do {
                ATB_ANY_TO_FREE(block);
                block += 1;
            } while (ATB_GET_KIND(block) == AT_TAIL);
        } else {
            assert(!"bad free, ptr not at head");
        }
    } else if (ptr_in != NULL) {
        assert(!"bad free, ptr not valid");
    }
}

mp_uint_t mem_alloc(mp_uint_t n_bytes) {
    mp_uint_t n_blocks = ((n_bytes + BYTES_PER_BLOCK - 1) & (~(BYTES_PER_BLOCK - 1))) / BYTES_PER_BLOCK;
    DEBUG_printf("gc_alloc(" UINT_FMT " bytes -> " UINT_FMT " blocks)\n", n_bytes, n_blocks);

    // check for 0 allocation
    if (n_blocks == 0) {
        return MEM_BLOCK_ERROR;
    }

    mp_uint_t i;
    mp_uint_t end_block;
    mp_uint_t start_block;
    mp_uint_t n_free = 0;
    // look for a run of n_blocks available blocks
    for (i = MP_STATE_MEM(gc_last_free_atb_index); i < MP_STATE_MEM(gc_alloc_table_byte_len); i++) {
        byte a = MP_STATE_MEM(gc_alloc_table_start)[i];
        if (ATB_0_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 0; goto found; } } else { n_free = 0; }
        if (ATB_1_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 1; goto found; } } else { n_free = 0; }
        if (ATB_2_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 2; goto found; } } else { n_free = 0; }
        if (ATB_3_IS_FREE(a)) { if (++n_free >= n_blocks) { i = i * BLOCKS_PER_ATB + 3; goto found; } } else { n_free = 0; }
    }
    return MEM_BLOCK_ERROR; // nothing found!

    // found, ending at block i inclusive
found:
    // get starting and end blocks, both inclusive
    end_block = i;
    start_block = i - n_free + 1;

    // Set last free ATB index to block after last block we found, for start of
    // next scan.  To reduce fragmentation, we only do this if we were looking
    // for a single free block, which guarantees that there are no free blocks
    // before this one.  Also, whenever we free or shink a block we must check
    // if this index needs adjusting (see gc_realloc and gc_free).
    if (n_free == 1) {
        MP_STATE_MEM(gc_last_free_atb_index) = (i + 1) / BLOCKS_PER_ATB;
    }

    // mark first block as used head
    ATB_FREE_TO_HEAD(start_block);

    // mark rest of blocks as used tail
    // TODO for a run of many blocks can make this more efficient
    for (mp_uint_t bl = start_block + 1; bl <= end_block; bl++) {
        ATB_FREE_TO_TAIL(bl);
    }

    return start_block;
}
