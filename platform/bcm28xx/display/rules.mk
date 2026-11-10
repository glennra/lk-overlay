LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_DEPS += platform/bcm28xx/bridge platform/bcm28xx/dsi platform/bcm28xx/hvs
MODULE_SRCS += $(LOCAL_DIR)/display.c
ifeq ($(WITH_MATRIX_BOOT_ANIMATION),1)
MODULE_SRCS += $(LOCAL_DIR)/matrix_anim.c
endif
GLOBAL_INCLUDES += $(LOCAL_DIR)/include

include make/module.mk
