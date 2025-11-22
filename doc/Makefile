
BUILD_DIR := $(shell readlink -f $(PWD)/../build)
CUR_DIR := $(PWD)

.PHONY: all

all: demo.gif

demo.gif: demo.vhs $(BUILD_DIR)/stsmon ${BUILD_DIR}/test-tsg
	docker run --rm -v $(CUR_DIR):/vhs -v $(BUILD_DIR):/stsmon ghcr.io/charmbracelet/vhs demo.vhs

clean:
	rm -f stsmon.1 stsmon.html