/*
 * This file is part of the MicroPython project, http://micropython.org/
 *
 * The MIT License (MIT)
 *
 * Copyright (c) 2015 Glenn Ruben Bakke
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include <string.h>

#include "py/mpstate.h"
#include "py/mphal.h"
#include "py/mperrno.h"
#include "py/runtime.h"
#include "py/stream.h"
#include "uart.h"
#include "nrfx_errors.h"
#include "nrfx_config.h"

#if MICROPY_PY_TIME_TICKS
#include "nrfx_rtc.h"
#include "nrf_clock.h"
#endif

#if !defined(USE_WORKAROUND_FOR_ANOMALY_132) && \
    (defined(NRF52832_XXAA) || defined(NRF52832_XXAB))
// ANOMALY 132 - LFCLK needs to avoid frame from 66us to 138us after LFCLK stop.
#define USE_WORKAROUND_FOR_ANOMALY_132 1
#endif

#if USE_WORKAROUND_FOR_ANOMALY_132
#include "soc/nrfx_coredep.h"
#endif

void mp_nrf_start_lfclk(void) {
    if (!nrf_clock_lf_start_task_status_get(NRF_CLOCK)) {
        // Check if the clock was recently stopped but is still running.
        #if USE_WORKAROUND_FOR_ANOMALY_132
        bool was_running = nrf_clock_lf_is_running(NRF_CLOCK);
        #endif
        // If so, wait for it to stop, otherwise the source cannot be changed. This also ensures
        // that the delay for anomaly 132 workaround does not land us in the middle of the forbidden
        // interval.
        while (nrf_clock_lf_is_running(NRF_CLOCK)) {
        }
        // Use the same LFCLK source as for bluetooth so that enabling the softdevice will not cause
        // an interruption.
        nrf_clock_lf_src_set(NRF_CLOCK,
            #if BLUETOOTH_LFCLK_RC
            NRF_CLOCK_LFCLK_RC
            #elif BLUETOOTH_LFCLK_SYNTH
            NRF_CLOCK_LFCLK_Synth
            #else
            NRF_CLOCK_LFCLK_Xtal
            #endif
            );
        #if USE_WORKAROUND_FOR_ANOMALY_132
        // If the clock just stopped, we can start it again right away as we are certainly before
        // the forbidden 66-138us interval. Otherwise, apply a delay of 138us to make sure we are
        // after the interval.
        if (!was_running) {
            nrfx_coredep_delay_us(138);
        }
        #endif
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_LFCLKSTART);
    }
    // When synthesizing LFCLK from HFCLK, start HFXO if it hasn't been started yet, otherwise we
    // would be synthesizing from HFINT, which is pointless as it's even less accurate than LFRC.
    // Must come after starting LFCLK, otherwise the LFCLK source reverts to RC.
    #if BLUETOOTH_LFCLK_SYNTH
    if (!nrf_clock_hf_start_task_status_get(NRF_CLOCK)) {
        nrf_clock_task_trigger(NRF_CLOCK, NRF_CLOCK_TASK_HFCLKSTART);
    }
    #endif
}

#if MICROPY_PY_TIME_TICKS

// Use RTC1 for time ticks generation (ms and us) with 32kHz tick resolution
// and overflow handling in RTC IRQ.

#define RTC_TICK_INCREASE_MSEC (33)

#define RTC_RESCHEDULE_CC(rtc, cc_nr, ticks) \
    do { \
        nrfx_rtc_cc_set(&rtc, cc_nr, nrfx_rtc_counter_get(&rtc) + ticks, true); \
    } while (0);

// RTC overflow irq handling notes:
// - If has_overflowed is set it could be before or after COUNTER is read.
//   If before then an adjustment must be made, if after then no adjustment is necessary.
// - The before case is when COUNTER is very small (because it just overflowed and was set to zero),
//   the after case is when COUNTER is very large (because it's just about to overflow
//   but we read it right before it overflows).
// - The extra check for counter is to distinguish these cases. 1<<23 because it's halfway
//   between min and max values of COUNTER.
#define RTC1_GET_TICKS_ATOMIC(rtc, overflows, counter) \
    do { \
        rtc.p_reg->INTENCLR = RTC_INTENCLR_OVRFLW_Msk; \
        overflows = rtc_overflows; \
        counter = rtc.p_reg->COUNTER; \
        uint32_t has_overflowed = rtc.p_reg->EVENTS_OVRFLW; \
        if (has_overflowed && counter < (1 << 23)) { \
            overflows += 1; \
        } \
        rtc.p_reg->INTENSET = RTC_INTENSET_OVRFLW_Msk; \
    } while (0);

nrfx_rtc_t rtc1 = NRFX_RTC_INSTANCE(1);
volatile mp_uint_t rtc_overflows = 0;

const nrfx_rtc_config_t rtc_config_time_ticks = {
    .prescaler = 0,
    .reliable = 0,
    .tick_latency = 0,
    #ifdef NRF51
    .interrupt_priority = 1,
    #else
    .interrupt_priority = 3,
    #endif
};

static void rtc_irq_time(nrfx_rtc_int_type_t event) {
    // irq handler for overflow
    if (event == NRFX_RTC_INT_OVERFLOW) {
        rtc_overflows += 1;
    }
    // irq handler for wakeup from WFI (~1msec)
    if (event == NRFX_RTC_INT_COMPARE0) {
        RTC_RESCHEDULE_CC(rtc1, 0, RTC_TICK_INCREASE_MSEC)
    }
}

void rtc1_init_time_ticks(void) {
    // Start the low-frequency clock (if it hasn't been started already)
    mp_nrf_start_lfclk();
    // Uninitialize first, then set overflow IRQ and first CC event
    nrfx_rtc_uninit(&rtc1);
    nrfx_rtc_init(&rtc1, &rtc_config_time_ticks, rtc_irq_time);
    nrfx_rtc_overflow_enable(&rtc1, true);
    RTC_RESCHEDULE_CC(rtc1, 0, RTC_TICK_INCREASE_MSEC)
    nrfx_rtc_enable(&rtc1);
}

mp_uint_t mp_hal_ticks_ms(void) {
    // Compute: (rtc_overflows << 24 + COUNTER) * 1000 / 32768
    //
    // Note that COUNTER * 1000 / 32768 would overflow during calculation, so use
    // the less obvious * 125 / 4096 calculation (overflow secure).
    //
    // Make sure not to call this function within an irq with higher prio than the
    // RTC's irq.  This would introduce the danger of preempting the RTC irq and
    // calling mp_hal_ticks_ms() at that time would return a false result.
    uint32_t overflows;
    uint32_t counter;
    // guard against overflow irq
    RTC1_GET_TICKS_ATOMIC(rtc1, overflows, counter)
    return (overflows << 9) * 1000 + (counter * 125 / 4096);
}

mp_uint_t mp_hal_ticks_us(void) {
    // Compute: ticks_us = (overflows << 24 + counter) * 1000000 / 32768
    //    = (overflows << 15 * 15625) + (counter * 15625 / 512)
    // Since this function is likely to be called in a poll loop it must
    // be fast, using an optimized 64bit mult/divide.
    uint32_t overflows;
    uint32_t counter;
    // guard against overflow irq
    RTC1_GET_TICKS_ATOMIC(rtc1, overflows, counter)
    // first compute counter * 15625
    uint32_t counter_lo = (counter & 0xffff) * 15625;
    uint32_t counter_hi = (counter >> 16) * 15625;
    // actual value is counter_hi << 16 + counter_lo
    return ((overflows << 15) * 15625) + ((counter_hi << 7) + (counter_lo >> 9));
}

#else

mp_uint_t mp_hal_ticks_ms(void) {
    return 0;
}

#endif

#if MICROPY_PY_TIME_TIME_TIME_NS && MICROPY_PY_TIME_TICKS

// nanoseconds between 2000-01-01 and tick counter zero, to adjust clock
static uint64_t epoch_offset = 0;

uint64_t mp_hal_time_ns(void) {
    // Range of `overflows` is sufficient: nanoseconds overflow 64 bits before `overflows` overflows
    // 32 bits.
    // Same logic as in mp_hal_ticks_ms, no need to worry about intermediate result overflows if we
    // do everything in 64-bit (this probably needn't be fast).
    uint32_t overflows;
    uint32_t counter;
    // guard against overflow irq
    RTC1_GET_TICKS_ATOMIC(rtc1, overflows, counter)
    return (((uint64_t)overflows << 18) * 1953125) + (((uint64_t)counter * 1953125) >> 6) + epoch_offset;
}

void mp_hal_set_time_ns(uint64_t ns_since_epoch) {
    epoch_offset += ns_since_epoch - mp_hal_time_ns();
}

#else

uint64_t mp_hal_time_ns(void) {
    return 0;
}

#endif

// this table converts from HAL_StatusTypeDef to POSIX errno
const byte mp_hal_status_to_errno_table[4] = {
    [HAL_OK] = 0,
    [HAL_ERROR] = MP_EIO,
    [HAL_BUSY] = MP_EBUSY,
    [HAL_TIMEOUT] = MP_ETIMEDOUT,
};

NORETURN void mp_hal_raise(HAL_StatusTypeDef status) {
    mp_raise_OSError(mp_hal_status_to_errno_table[status]);
}

#if !MICROPY_KBD_EXCEPTION
void mp_hal_set_interrupt_char(int c) {

}
#endif

#if !MICROPY_PY_BLE_NUS && !MICROPY_HW_USB_CDC
uintptr_t mp_hal_stdio_poll(uintptr_t poll_flags) {
    uintptr_t ret = 0;
    if ((poll_flags & MP_STREAM_POLL_RD) && MP_STATE_VM(dupterm_objs[0]) != MP_OBJ_NULL
        && uart_rx_any(MP_STATE_VM(dupterm_objs[0]))) {
        ret |= MP_STREAM_POLL_RD;
    }
    if ((poll_flags & MP_STREAM_POLL_WR) && MP_STATE_VM(dupterm_objs[0]) != MP_OBJ_NULL) {
        ret |= MP_STREAM_POLL_WR;
    }
    return ret;
}

int mp_hal_stdin_rx_chr(void) {
    for (;;) {
        if (MP_STATE_VM(dupterm_objs[0]) != MP_OBJ_NULL && uart_rx_any(MP_STATE_VM(dupterm_objs[0]))) {
            return uart_rx_char(MP_STATE_VM(dupterm_objs[0]));
        }
        MICROPY_EVENT_POLL_HOOK
    }

    return 0;
}

mp_uint_t mp_hal_stdout_tx_strn(const char *str, mp_uint_t len) {
    if (MP_STATE_VM(dupterm_objs[0]) != MP_OBJ_NULL) {
        uart_tx_strn(MP_STATE_VM(dupterm_objs[0]), str, len);
        return len;
    }
    return 0;
}

void mp_hal_stdout_tx_strn_cooked(const char *str, mp_uint_t len) {
    if (MP_STATE_VM(dupterm_objs[0]) != MP_OBJ_NULL) {
        uart_tx_strn_cooked(MP_STATE_VM(dupterm_objs[0]), str, len);
    }
}
#endif

void mp_hal_stdout_tx_str(const char *str) {
    mp_hal_stdout_tx_strn(str, strlen(str));
}

#if MICROPY_PY_TIME_TICKS

void mp_hal_delay_us(mp_uint_t us) {
    uint32_t now;
    if (us == 0) {
        return;
    }
    now = mp_hal_ticks_us();
    while (mp_hal_ticks_us() - now < us) {
    }
}

void mp_hal_delay_ms(mp_uint_t ms) {
    uint32_t now;
    if (ms == 0) {
        return;
    }
    now = mp_hal_ticks_ms();
    while (mp_hal_ticks_ms() - now < ms) {
        MICROPY_EVENT_POLL_HOOK
    }
}

#else

void mp_hal_delay_us(mp_uint_t us) {
    if (us == 0) {
        return;
    }
    register uint32_t delay __ASM("r0") = us;
    __ASM volatile (
        "1:\n"
        #ifdef NRF51
        " SUB %0, %0, #1\n"
        #else
        " SUBS %0, %0, #1\n"
        #endif
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        #if defined(NRF52) || defined(NRF9160_XXAA)
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        " NOP\n"
        #endif
        " BNE 1b\n"
        : "+r" (delay));
}

void mp_hal_delay_ms(mp_uint_t ms) {
    for (mp_uint_t i = 0; i < ms; i++)
    {
        mp_hal_delay_us(999);
    }
}
#endif

#if defined(NRFX_LOG_ENABLED) && (NRFX_LOG_ENABLED == 1)

static const char nrfx_error_unknown[1] = "";

static const char nrfx_error_success[] = "NRFX_SUCCESS";
static const char nrfx_error_internal[] = "NRFX_ERROR_INTERNAL";
static const char nrfx_error_no_mem[] = "NRFX_ERROR_NO_MEM";
static const char nrfx_error_not_supported[] = "NRFX_ERROR_NOT_SUPPORTED";
static const char nrfx_error_invalid_param[] = "NRFX_ERROR_INVALID_PARAM";
static const char nrfx_error_invalid_state[] = "NRFX_ERROR_INVALID_STATE";
static const char nrfx_error_invalid_length[] = "NRFX_ERROR_INVALID_LENGTH";
static const char nrfx_error_timeout[] = "NRFX_ERROR_TIMEOUT";
static const char nrfx_error_forbidden[] = "NRFX_ERROR_FORBIDDEN";
static const char nrfx_error_null[] = "NRFX_ERROR_NULL";
static const char nrfx_error_invalid_addr[] = "NRFX_ERROR_INVALID_ADDR";
static const char nrfx_error_busy[] = "NRFX_ERROR_BUSY";
static const char nrfx_error_already_initalized[] = "NRFX_ERROR_ALREADY_INITIALIZED";

static const char *nrfx_error_strings[13] = {
    nrfx_error_success,
    nrfx_error_internal,
    nrfx_error_no_mem,
    nrfx_error_not_supported,
    nrfx_error_invalid_param,
    nrfx_error_invalid_state,
    nrfx_error_invalid_length,
    nrfx_error_timeout,
    nrfx_error_forbidden,
    nrfx_error_null,
    nrfx_error_invalid_addr,
    nrfx_error_busy,
    nrfx_error_already_initalized
};

static const char nrfx_drv_error_twi_err_overrun[] = "NRFX_ERROR_DRV_TWI_ERR_OVERRUN";
static const char nrfx_drv_error_twi_err_anack[] = "NRFX_ERROR_DRV_TWI_ERR_ANACK";
static const char nrfx_drv_error_twi_err_dnack[] = "NRFX_ERROR_DRV_TWI_ERR_DNACK";

static const char *nrfx_drv_error_strings[3] = {
    nrfx_drv_error_twi_err_overrun,
    nrfx_drv_error_twi_err_anack,
    nrfx_drv_error_twi_err_dnack
};

const char *nrfx_error_code_lookup(uint32_t err_code) {
    if (err_code >= NRFX_ERROR_BASE_NUM && err_code <= NRFX_ERROR_BASE_NUM + 13) {
        return nrfx_error_strings[err_code - NRFX_ERROR_BASE_NUM];
    } else if (err_code >= NRFX_ERROR_DRIVERS_BASE_NUM && err_code <= NRFX_ERROR_DRIVERS_BASE_NUM + 3) {
        return nrfx_drv_error_strings[err_code - NRFX_ERROR_DRIVERS_BASE_NUM];
    }

    return nrfx_error_unknown;
}

#endif // NRFX_LOG_ENABLED
