# Stable build-directory name for the upstream Pi 3 HDMI chainloader workflow.
# Keep the HDMI configuration selected while preserving build-rpi3-chainloader.
include project/rpi3-hdmi-chainloader.mk

ifeq ($(strip $(ARM_CHAINLOADER_BIN)),)
$(error Set ARM_CHAINLOADER_BIN to the absolute path of the ARM chainloader binary)
endif
ARM_CHAINLOADER_BIN := $(abspath $(ARM_CHAINLOADER_BIN))
ifeq ($(wildcard $(ARM_CHAINLOADER_BIN)),)
$(error ARM_CHAINLOADER_BIN does not exist: $(ARM_CHAINLOADER_BIN))
endif

GLOBAL_DEFINES += BCM283X_ARM_CHAINLOADER=1
