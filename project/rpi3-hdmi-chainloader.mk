TARGET := rpi3-vpu

MODULES += app/shell platform/bcm28xx/vc4-hdmi \
           platform/bcm28xx/arm-chainloader \
           platform/bcm28xx/rpi-ddr2/autoram

BOOTCODE := 1
VPU_STAGE2 := 1
HDMI_HOLD_ARM_BOOT ?= 0
GLOBAL_DEFINES += BOOTCODE=1 VPU_STAGE2=1 BCM283X_PLLD_CONTROL=1 BCM283X_HDMI=1 HDMI_HOLD_ARM_BOOT=$(HDMI_HOLD_ARM_BOOT)
# The VPU must not consume PL011 RX bytes after Linux handoff.
GLOBAL_DEFINES += PL011_TX_ONLY
# Pi 3B+ Linux/USB handoff requires the full firmware PHY setup.  The
# no-PHY diagnostic reaches earlycon but stops before normal Linux bring-up.
HDMI_USB_PHY_PREFLIGHT ?= 1
ifeq ($(HDMI_USB_PHY_PREFLIGHT),1)
MODULES += platform/bcm28xx/usb-phy
endif
GLOBAL_DEFINES += HDMI_USB_PHY_PREFLIGHT=$(HDMI_USB_PHY_PREFLIGHT)
# Opt-in cold-start IMAGE/PERII ownership correction. The sequence
# executes in the stage-1 project; stage 2 only observes the initialized state.
EARLY_IMAGE_PERII_STARTUP ?= 1
GLOBAL_DEFINES += EARLY_IMAGE_PERII_STARTUP=$(EARLY_IMAGE_PERII_STARTUP)
GLOBAL_DEFINES += IMAGE_TWO_STAGE_STARTUP=$(EARLY_IMAGE_PERII_STARTUP)
HDMI_USB_EARLY_HUB ?= 1
GLOBAL_DEFINES += HDMI_USB_EARLY_HUB=$(HDMI_USB_EARLY_HUB)
HDMI_USB_PHY_WRITE_ONLY ?= 0
GLOBAL_DEFINES += HDMI_USB_PHY_WRITE_ONLY=$(HDMI_USB_PHY_WRITE_ONLY)
HDMI_USB_PHY_SKIP_17 ?= 0
GLOBAL_DEFINES += HDMI_USB_PHY_SKIP_17=$(HDMI_USB_PHY_SKIP_17)
USB_PHY_MDIO_PRE_PREAMBLE ?= 0
GLOBAL_DEFINES += USB_PHY_MDIO_PRE_PREAMBLE=$(USB_PHY_MDIO_PRE_PREAMBLE)
USB_PHY_REFERENCE_CLOCK ?= 0
GLOBAL_DEFINES += USB_PHY_REFERENCE_CLOCK=$(USB_PHY_REFERENCE_CLOCK)
USB_PLLD_START ?= 1
GLOBAL_DEFINES += USB_PLLD_START=$(USB_PLLD_START)
USB_STOCK_GP1_CLOCK ?= 1
GLOBAL_DEFINES += USB_STOCK_GP1_CLOCK=$(USB_STOCK_GP1_CLOCK)
USB_LANRUN_EXPANDER ?= 0
GLOBAL_DEFINES += USB_LANRUN_EXPANDER=$(USB_LANRUN_EXPANDER)
DEBUG := 0
BCM283X_HDMI := 1

# Primary WO-046 hardware diagnostic target.  Set HDMI_MODE_1080P60=0 for the
# preserved 640x480 baseline.
HDMI_MODE_1080P60 ?= 1
GLOBAL_DEFINES += HDMI_MODE_1080P60=$(HDMI_MODE_1080P60)
# Optional 720p60 diagnostic; select instead of 1080p60.
HDMI_MODE_1280X800 ?= 0
GLOBAL_DEFINES += HDMI_MODE_1280X800=$(HDMI_MODE_1280X800)
HDMI_MODE_720P60 ?= 0
GLOBAL_DEFINES += HDMI_MODE_720P60=$(HDMI_MODE_720P60)
HDMI_EARLY_CORE_PROBE ?= 0
GLOBAL_DEFINES += HDMI_EARLY_CORE_PROBE=$(HDMI_EARLY_CORE_PROBE)
HDMI_DIAG_DVI ?= 0
GLOBAL_DEFINES += HDMI_DIAG_DVI=$(HDMI_DIAG_DVI)
HDMI_EARLY_PV_PROBE ?= 0
GLOBAL_DEFINES += HDMI_EARLY_PV_PROBE=$(HDMI_EARLY_PV_PROBE)

# Optional ordered-background 1080p diagnostic.
HDMI_ORDERED_BACKGROUND ?= 0
GLOBAL_DEFINES += HDMI_ORDERED_BACKGROUND=$(HDMI_ORDERED_BACKGROUND)

# Opt-in DDC discovery command; transaction status reads have stalled hardware.
HDMI_EDID_DIAGNOSTIC ?= 1
GLOBAL_DEFINES += HDMI_EDID_DIAGNOSTIC=$(HDMI_EDID_DIAGNOSTIC)

# Experimental 1280x800 HDMI signalling comparison, including AVI packet.
HDMI_DIAG_FORCE_HDMI ?= 0
GLOBAL_DEFINES += HDMI_DIAG_FORCE_HDMI=$(HDMI_DIAG_FORCE_HDMI)

# Experimental exact-EDID auto-selection; DDC MMIO may stall.
HDMI_AUTO_MODE ?= 1
GLOBAL_DEFINES += HDMI_AUTO_MODE=$(HDMI_AUTO_MODE)

# The HDMI state-machine clock needs a parent above the requested 163.68 MHz
# rate. With the normal 250 MHz PLLC_PER parent the divider would be below
# the clock manager's minimum and preflight aborts before enabling HDMI.
# Keep the 500 MHz parent for both ordered and automatic HDMI startup.
ifneq ($(filter 1,$(HDMI_ORDERED_BACKGROUND) $(HDMI_AUTO_MODE)),)
PLLC_PER_DIV := 1
endif

# If the EDID has no HDMI extension, use DVI on the 1280x800 profile.  A
# native HDMI sink is selected only when EDID explicitly advertises HDMI.
HDMI_AUTO_800_DVI ?= 1
GLOBAL_DEFINES += HDMI_AUTO_800_DVI=$(HDMI_AUTO_800_DVI)

# Start automatic discovery at the known-compatible 1280x800 timing.  This
# avoids touching the HDMI scheduler with an unsupported 1080p seed mode.
HDMI_AUTO_PREFLIGHT_800 ?= 1
GLOBAL_DEFINES += HDMI_AUTO_PREFLIGHT_800=$(HDMI_AUTO_PREFLIGHT_800)

# Optional diagnostic delay before the initial automatic preflight signal.
HDMI_AUTO_START_DELAY_MS ?= 0
GLOBAL_DEFINES += HDMI_AUTO_START_DELAY_MS=$(HDMI_AUTO_START_DELAY_MS)

# Optional boot-time comparison with reduced diagnostic output.
HDMI_AUTO_QUIET ?= 0
GLOBAL_DEFINES += HDMI_AUTO_QUIET=$(HDMI_AUTO_QUIET)

# Experimental preloaded EDID offset write.
HDMI_DDC_PREFILL ?= 0
GLOBAL_DEFINES += HDMI_DDC_PREFILL=$(HDMI_DDC_PREFILL)

# Optional suppression of pure-variable early PLL progress prints.
HDMI_PLL_QUIET ?= 0
GLOBAL_DEFINES += HDMI_PLL_QUIET=$(HDMI_PLL_QUIET)

# Retain early PLL readbacks but omit their serial printing.
HDMI_PLL_SNAPSHOT_QUIET ?= 0
GLOBAL_DEFINES += HDMI_PLL_SNAPSHOT_QUIET=$(HDMI_PLL_SNAPSHOT_QUIET)

# Reduce prefill progress markers during boot timing experiments.
HDMI_DDC_MARKER_COMPACT ?= 0
GLOBAL_DEFINES += HDMI_DDC_MARKER_COMPACT=$(HDMI_DDC_MARKER_COMPACT)
