BOARD ?= MPS2_AN385

# Make the build directory reflect the board.
BUILD ?= build-$(BOARD)

include ../../py/mkenv.mk
-include mpconfigport.mk

# Include board specific .mk file.
include boards/$(BOARD).mk

# qstr definitions (must come before including py.mk)
QSTR_DEFS = qstrdefsport.h

# MicroPython feature configurations
MICROPY_ROM_TEXT_COMPRESSION ?= 1

ifeq ($(QEMU_ARCH),arm)
FROZEN_MANIFEST ?= "freeze('test-frzmpy')"
endif
ifeq ($(QEMU_ARCH),riscv32)
FROZEN_MANIFEST ?= "freeze('test-frzmpy', ('frozen_const.py', 'frozen_viper.py', 'native_frozen_align.py'))"
endif

# include py core make definitions
include $(TOP)/py/py.mk
include $(TOP)/extmod/extmod.mk

QEMU_SYSTEM = qemu-system-$(QEMU_ARCH)
QEMU_ARGS += -machine $(QEMU_MACHINE) -nographic -monitor null -semihosting

INC += -I.
INC += -I$(TOP)
INC += -I$(BUILD)

CFLAGS += -DMICROPY_HW_BOARD_NAME='"$(QEMU_MACHINE)"'
CFLAGS += $(INC) -Wall -Wpointer-arith -Wdouble-promotion -Wfloat-conversion -Werror -std=gnu99 $(COPT) \
	 -ffunction-sections -fdata-sections
CFLAGS += $(CFLAGS_EXTRA)

# Debugging/Optimization
ifeq ($(DEBUG), 1)
CFLAGS += -g
COPT = -O0
else
COPT += -Os -DNDEBUG
endif

ifeq ($(QEMU_ARCH),arm)

CROSS_COMPILE ?= arm-none-eabi-

## With CoudeSourcery it's actually a little different, you just need `-T generic-m-hosted.ld`.
## Although for some reason `$(LD)` will not find that linker script, it works with `$(CC)`.
## It turns out that this is specific to CoudeSourcery, and ARM version of GCC ships something
## else instead and according to the following files, this is what we need to pass to `$(CC).
## - gcc-arm-none-eabi-4_8-2014q1/share/gcc-arm-none-eabi/samples/src/makefile.conf
## - gcc-arm-none-eabi-4_8-2014q1/share/gcc-arm-none-eabi/samples/src/qemu/Makefile
LDFLAGS= -T $(LDSCRIPT) --gc-sections -Map=$(@:.elf=.map)
LIBS = $(shell $(CC) $(CFLAGS) -print-libgcc-file-name)

SRC_C += \
	mcu/arm/startup.c \
	shared/runtime/semihosting_arm.c \

endif

ifeq ($(QEMU_ARCH),riscv32)

CROSS_COMPILE ?= riscv64-unknown-elf-

GCC_VERSION = $(word 1, $(subst ., , $(shell $(CC) -dumpversion)))

RV32_ABI = ilp32

QEMU_ARGS += -bios none

# GCC 10 and lower do not recognise the Zicsr extension in the
# architecture name.  "Make" unfortunately does not provide any simple way
# to perform numeric comparisons, so to keep things simple we assume that
# GCC is at least version 10 for the time being.
ifeq ($(GCC_VERSION),10)
RV32_ARCH ?= rv32imac
else
# Recent GCC versions explicitly require to declare extensions.
RV32_ARCH ?= rv32imac_zicsr
endif

# If Picolibc is available then select it explicitly.  Ubuntu 22.04 ships its
# bare metal RISC-V toolchain with Picolibc rather than Newlib, and the default
# is "nosys" so a value must be provided.  To avoid having per-distro
# workarounds, always select Picolibc if available.
PICOLIBC_SPECS = $(shell $(CC) --print-file-name=picolibc.specs)
ifeq ($(PICOLIBC_SPECS),picolibc.specs)
# Picolibc was not found.
SPECS_FRAGMENT =
else
SPECS_FRAGMENT = --specs=$(PICOLIBC_SPECS)
CFLAGS += $(SPECS_FRAGMENT)
endif

LD = $(CC)

AFLAGS += -mabi=$(RV32_ABI) -march=$(RV32_ARCH)
CFLAGS += $(AFLAGS)
LDFLAGS += -mabi=$(RV32_ABI) -march=$(RV32_ARCH) -T $(LDSCRIPT) -Wl,-EL
LDFLAGS += $(SPECS_FRAGMENT) -Wl,--gc-sections -Wl,-Map=$(@:.elf=.map)

SRC_C += \
	mcu/rv32/interrupts.c \
	mcu/rv32/startup.c \

SRC_BOARD_O += mcu/rv32/entrypoint.o

endif

SRC_C += \
	main.c \
	uart.c \
	mphalport.c \
	shared/libc/string0.c \
	shared/readline/readline.c \
	shared/runtime/interrupt_char.c \
	shared/runtime/pyexec.c \
	shared/runtime/stdout_helpers.c \
	shared/runtime/sys_stdio_mphal.c \

LIB_SRC_C += $(SRC_LIB_LIBM_C)
LIB_SRC_C += $(SRC_LIB_LIBM_SQRT_SW_C)

OBJ += $(PY_O)
OBJ += $(addprefix $(BUILD)/, $(SRC_C:.c=.o))
OBJ += $(addprefix $(BUILD)/, $(SRC_BOARD_O))
OBJ += $(addprefix $(BUILD)/, $(LIB_SRC_C:.c=.o))

# List of sources for qstr extraction
SRC_QSTR += $(SRC_C) $(LIB_SRC_C)

all: $(BUILD)/firmware.elf

.PHONY: repl
repl: $(BUILD)/firmware.elf
	$(ECHO) "Use machine.reset() to exit"
	$(QEMU_SYSTEM) $(QEMU_ARGS) -serial mon:stdio -kernel $<

.PHONY: run
run: $(BUILD)/firmware.elf
	$(QEMU_SYSTEM) $(QEMU_ARGS) -serial pty -kernel $<

# `make debug` will block QEMU until a debugger is connected to port 1234.
.PHONY: debug
debug: $(BUILD)/firmware.elf
	$(QEMU_SYSTEM) $(QEMU_ARGS) -serial mon:stdio -S -s -kernel $<

.PHONY: test
test: $(BUILD)/firmware.elf
	$(eval DIRNAME=ports/$(notdir $(CURDIR)))
	cd $(TOP)/tests && ./run-tests.py --target qemu-arm --device execpty:"$(QEMU_SYSTEM) $(QEMU_ARGS) -serial pty -kernel ../$(DIRNAME)/$<" $(TESTS_EXCLUDE)

## `$(LD)` doesn't seem to like `--specs` for some reason, but we can just use `$(CC)` here.
$(BUILD)/firmware.elf: $(LDSCRIPT) $(OBJ)
	$(Q)$(LD) $(LDFLAGS) -o $@ $(OBJ) $(LIBS)
	$(Q)$(SIZE) $@

include $(TOP)/py/mkrules.mk
