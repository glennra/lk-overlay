# Canonical production target: one stage-2 image containing both backends.
VPU_STAGE2_RELOCATED := 1
GLOBAL_DEFINES += VPU_STAGE2_RELOCATED=1
include project/rpi3-hdmi-chainloader.mk
MODULES += platform/bcm28xx/display
MODULES += platform/bcm28xx/vec
GLOBAL_DEFINES += DISPLAY_RUNTIME_AUTO=1 DISPLAY_BACKEND_AUTO=1 BCM283X_DSI_HVS=1
WITH_MATRIX_BOOT_ANIMATION ?= 0
GLOBAL_DEFINES += WITH_MATRIX_BOOT_ANIMATION=$(WITH_MATRIX_BOOT_ANIMATION)
override DISPLAY_BACKEND := AUTO

ifeq ($(strip $(ARM_CHAINLOADER_BIN)),)
$(error Set ARM_CHAINLOADER_BIN to the absolute path of the ARM chainloader binary)
endif
ARM_CHAINLOADER_BIN := $(abspath $(ARM_CHAINLOADER_BIN))
ifeq ($(wildcard $(ARM_CHAINLOADER_BIN)),)
$(error ARM_CHAINLOADER_BIN does not exist: $(ARM_CHAINLOADER_BIN))
endif

GLOBAL_DEFINES += BCM283X_ARM_CHAINLOADER=1
