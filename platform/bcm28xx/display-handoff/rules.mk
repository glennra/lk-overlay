LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

GLOBAL_INCLUDES += $(LOCAL_DIR)/include
# The production chainloader always links both backends.  DISPLAY_BACKEND is
# retained only as a compatibility/configuration label; it must not prune a
# runtime-auto image.
MODULE_DEPS += platform/bcm28xx/vc4-hdmi platform/bcm28xx/display
MODULE_SRCS += $(LOCAL_DIR)/manager.c $(LOCAL_DIR)/hdmi.c $(LOCAL_DIR)/dsi.c

include make/module.mk
