LOCAL_DIR := $(GET_LOCAL_DIR)
MODULE := $(LOCAL_DIR)

MODULE_DEPS += platform/bcm28xx/otp platform/bcm28xx/power platform/bcm28xx/pll
MODULE_DEPS += platform/bcm28xx/display-handoff
MODULE_SRCS += $(LOCAL_DIR)/payload.S $(LOCAL_DIR)/handoff.c $(LOCAL_DIR)/mappings.c \
	$(LOCAL_DIR)/clock.c $(LOCAL_DIR)/release.c $(LOCAL_DIR)/start.c
ifeq ($(USB_LANRUN_EXPANDER),1)
MODULE_SRCS += $(LOCAL_DIR)/lanrun_i2c.c $(LOCAL_DIR)/lanrun_fxl.c
endif
MODULE_DEFINES += ARM_CHAINLOADER_FILE=\"$(ARM_CHAINLOADER_BIN)\"

# Always refresh the embedded input, including when a symlink target changes.
.PHONY: arm-chainloader-payload-refresh
arm-chainloader-payload-refresh:
$(BUILDDIR)/$(LOCAL_DIR)/payload.S.o: $(ARM_CHAINLOADER_BIN) arm-chainloader-payload-refresh

include make/module.mk
