TARGET := rpi3-vpu

MODULES += app/shell platform/bcm28xx/vc4-hdmi \
           platform/bcm28xx/arm-chainloader \
           platform/bcm28xx/rpi-ddr2/autoram

BOOTCODE := 1
VPU_STAGE2 := 1
GLOBAL_DEFINES += BOOTCODE=1 VPU_STAGE2=1 BCM283X_PLLD_CONTROL=1 BCM283X_HDMI=1 HDMI_HOLD_ARM_BOOT=1
DEBUG := 0
BCM283X_HDMI := 1
