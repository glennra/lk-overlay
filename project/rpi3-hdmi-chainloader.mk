TARGET := rpi3-vpu

MODULES += app/shell platform/bcm28xx/vc4-hdmi \
           platform/bcm28xx/arm-chainloader \
           platform/bcm28xx/rpi-ddr2/autoram

BOOTCODE := 1
VPU_STAGE2 := 1
GLOBAL_DEFINES += BOOTCODE=1 VPU_STAGE2=1 BCM283X_PLLD_CONTROL=1 BCM283X_HDMI=1 HDMI_HOLD_ARM_BOOT=1
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

# Optional Redox-sequence 1080p diagnostic; disabled by default.
HDMI_DIAG_REDOX_BACKGROUND ?= 0
GLOBAL_DEFINES += HDMI_DIAG_REDOX_BACKGROUND=$(HDMI_DIAG_REDOX_BACKGROUND)

# Redox uses a 500 MHz PLLC_PER parent.  LK normally sets this tap to
# 250 MHz.  Limit the altered shared clock to this diagnostic build.
ifeq ($(HDMI_DIAG_REDOX_BACKGROUND),1)
PLLC_PER_DIV := 1
endif

# Opt-in DDC discovery command; transaction status reads have stalled hardware.
HDMI_EDID_DIAGNOSTIC ?= 0
GLOBAL_DEFINES += HDMI_EDID_DIAGNOSTIC=$(HDMI_EDID_DIAGNOSTIC)

# Experimental 1280x800 HDMI signalling comparison, including AVI packet.
HDMI_DIAG_FORCE_HDMI ?= 0
GLOBAL_DEFINES += HDMI_DIAG_FORCE_HDMI=$(HDMI_DIAG_FORCE_HDMI)

# Experimental exact-EDID auto-selection; DDC MMIO may stall.
HDMI_AUTO_MODE ?= 0
GLOBAL_DEFINES += HDMI_AUTO_MODE=$(HDMI_AUTO_MODE)

# Diagnostic comparison: use DVI on the 1280x800 auto profile.
HDMI_AUTO_800_DVI ?= 0
GLOBAL_DEFINES += HDMI_AUTO_800_DVI=$(HDMI_AUTO_800_DVI)

# Experiment: preflight at 1280x800 and retain it if EDID matches.
HDMI_AUTO_PREFLIGHT_800 ?= 0
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
