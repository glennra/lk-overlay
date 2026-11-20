ARM_CC ?= arm-none-eabi-gcc
ARM_CXX ?= arm-none-eabi-g++
HOST_CC ?= cc
TEST_DIR ?= ../build-arm-chainloader-test

.PHONY: static-test
static-test:
	$(ARM_CC) -std=c11 -ffreestanding -fsyntax-only -I. tests/firmware_abi.c
	$(ARM_CXX) -std=c++11 -ffreestanding -fsyntax-only -DBAREMETAL -I. -Icommon/include -Icommon -Iarm_chainloader -Iarm_chainloader/drivers/libfdt -Iarm_chainloader/drivers/fatfs -Iarm_chainloader/drivers arm_chainloader/loader.cc arm_chainloader/drivers/sdhost_impl.cc
	$(ARM_CC) -c -ffreestanding -march=armv6zk -mfpu=vfp -mfloat-abi=softfp -I. -Iarm_chainloader -x assembler-with-cpp arm_chainloader/start.s -o /dev/null
	@mkdir -p "$(TEST_DIR)"
	$(HOST_CC) -std=c11 -Wall -Wextra -Werror -Iarm_chainloader tests/board_dtb_test.c -o "$(TEST_DIR)/board_dtb_test"
	"$(TEST_DIR)/board_dtb_test"
	$(HOST_CC) -std=c11 -Wall -Wextra -Iarm_chainloader -Iarm_chainloader/drivers/libfdt tests/cma_experiment_test.c arm_chainloader/drivers/libfdt/fdt.c arm_chainloader/drivers/libfdt/fdt_ro.c arm_chainloader/drivers/libfdt/fdt_rw.c arm_chainloader/drivers/libfdt/fdt_sw.c arm_chainloader/drivers/libfdt/fdt_empty_tree.c arm_chainloader/drivers/libfdt/fdt_wip.c arm_chainloader/drivers/libfdt/fdt_addresses.c -o "$(TEST_DIR)/cma_experiment_test"
	"$(TEST_DIR)/cma_experiment_test"
	@echo 'ARM loader static checks passed'
