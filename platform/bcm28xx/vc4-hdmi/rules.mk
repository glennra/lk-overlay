LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

MODULES += \
	platform/bcm28xx/hvs \
	platform/bcm28xx/pixelvalve \


MODULE_SRCS += \
	$(LOCAL_DIR)/hdmi.c \

ifeq ($(HDMI_EDID_DIAGNOSTIC),1)
MODULE_SRCS += $(LOCAL_DIR)/ddc.c $(LOCAL_DIR)/edid.c $(LOCAL_DIR)/discovery.c
endif

include make/module.mk
