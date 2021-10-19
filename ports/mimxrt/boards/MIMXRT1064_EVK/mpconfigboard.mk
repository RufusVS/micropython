MCU_SERIES = MIMXRT1064
MCU_VARIANT = MIMXRT1064DVL6A

MICROPY_FLOAT_IMPL = double
MICROPY_PY_MACHINE_SDCARD = 1

MICROPY_HW_SDRAM_AVAIL = 1
MICROPY_HW_SDRAM_SIZE  = 0x2000000  # 32MB

BOARD_FLASH_TYPE ?= qspi_nor
BOARD_FLASH_SIZE ?= 0x400000  # 4MB

MICROPY_PY_LWIP = 1
MICROPY_PY_USSL = 1
MICROPY_SSL_MBEDTLS = 1

JLINK_PATH ?= /media/RT1064-EVK/

deploy: $(BUILD)/firmware.bin
	cp $< $(JLINK_PATH)
