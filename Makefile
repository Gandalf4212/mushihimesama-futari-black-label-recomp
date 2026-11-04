# Dev build (Linux/macOS/MinGW). Real builds will move to CMake once the runtime exists.
CC ?= cc
CFLAGS ?= -O2 -Wall -Wno-unused-variable
BUILD := build

all: $(BUILD)/boot_runner

$(BUILD)/sh3_interp.inc: core/sh3/isa.py core/sh3/gen_interp.py
	@mkdir -p $(BUILD)
	python3 core/sh3/gen_interp.py $@

$(BUILD)/boot_runner: core/runtime/boot_runner.c core/sh3/sh3_core.c core/sh3/sh3.h core/sh3/sh3_ops.h $(BUILD)/sh3_interp.inc
	$(CC) $(CFLAGS) -I$(BUILD) -o $@ core/runtime/boot_runner.c core/sh3/sh3_core.c

clean:
	rm -rf $(BUILD)
