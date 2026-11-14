LOCAL_DIR := $(GET_LOCAL_DIR)

MODULE := $(LOCAL_DIR)

TOOLCHAIN_PREFIX := vc4-elf-
LD := vc4-elf-ld
CC := vc4-elf-gcc
OBJCOPY := vc4-elf-objcopy
OBJDUMP := vc4-elf-objdump

$(BUILDDIR)/system-onesegment.ld: $(LOCAL_DIR)/start.ld
	@echo generating $@
	@$(MKDIR)
	echo TODO: properly template the linker script
	cp $< $@

# TODO, fix the linker flags
# Keep this path aligned with the workspace VC4 toolchain.  The old absolute
# Nix-store path is not present on this host.
ARCH_LDFLAGS += -L/home/glenn/projects/car-hmi/gpu-boot/vc4-toolchain/prefix/lib/gcc/vc4-elf/6.2.1/
ARCH_CFLAGS += -fstack-usage -funroll-loops -Os -fno-plt
#ARCH_COMPILEFLAGS += -fno-omit-frame-pointer

MODULE_SRCS += \
	$(LOCAL_DIR)/arch.c \
	$(LOCAL_DIR)/thread.c \
	$(LOCAL_DIR)/intc.c \
	$(LOCAL_DIR)/start.S \
	$(LOCAL_DIR)/thread_asm.S \
	$(LOCAL_DIR)/interrupt.S \

MODULE_DEPS += dev/timer/vc4
GLOBAL_DEFINES += VC4_TIMER_CHANNEL=0 ARCH_HAS_MMU=0 USE_BUILTIN_ATOMICS=0

WITH_LINKER_GC ?= 1

LIBGCC := $(shell vc4-elf-gcc -print-libgcc-file-name)

include make/module.mk
