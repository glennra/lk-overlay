CC ?= cc
BUILD_DIR ?= ../../build-hdmi-edid-test

.PHONY: test clean

test: $(BUILD_DIR)/edid_test
	$(BUILD_DIR)/edid_test

$(BUILD_DIR)/edid_test: edid_test.c ../../platform/bcm28xx/vc4-hdmi/edid.c \
        ../../platform/bcm28xx/vc4-hdmi/edid.h
	mkdir -p $(BUILD_DIR)
	$(CC) -std=c11 -Wall -Wextra -Werror -I../../platform/bcm28xx/vc4-hdmi \
		edid_test.c ../../platform/bcm28xx/vc4-hdmi/edid.c -o $@

clean:
	rm -f $(BUILD_DIR)/edid_test
