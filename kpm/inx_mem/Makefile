# inx_mem KPM Makefile — same pattern as the KernelPatch SDK's own
# kpms/demo-hello/Makefile (TARGET_COMPILE + KP_DIR convention), just
# pointed at our own source file. Built in CI (see
# .github/workflows/build.yml, job build-inx-kpm) against a pinned
# KernelPatch SDK checkout, using the real ARM GNU bare-metal toolchain
# (the same one KernelPatch's own CI uses to build kpimg/demo KPMs) — NOT
# the Android NDK's clang, since this Makefile's $(TARGET_COMPILE)gcc /
# $(TARGET_COMPILE)ld need an actual gcc/ld pair, which that toolchain
# ships and NDK clang does not.

ifndef TARGET_COMPILE
    $(error TARGET_COMPILE not set)
endif

ifndef KP_DIR
    KP_DIR = ../..
endif

CC = $(TARGET_COMPILE)gcc
LD = $(TARGET_COMPILE)ld
CFLAGS += -nostdinc
CFLAGS += -fno-pic -fno-PIC -fno-pie -fno-stack-protector
CFLAGS += -fno-asynchronous-unwind-tables -fno-unwind-tables -fno-exceptions

INCLUDE_DIRS := . include patch/include linux/include linux/arch/arm64/include linux/tools/arch/arm64/include

INCLUDE_FLAGS := $(foreach dir,$(INCLUDE_DIRS),-I$(KP_DIR)/kernel/$(dir))

# inx_libc.o provides bare memcpy/memset/memmove/memcmp so compiler-
# synthesized aggregate copies resolve inside the module (KParch does not
# export the bare names, only kf_*). See inx_libc.c.
objs := inx_mem.o inx_libc.o

all: inx_mem.kpm

inx_mem.kpm: ${objs}
	${CC} -r -o $@ $^

%.o: %.c
	${CC} $(CFLAGS) $(INCLUDE_FLAGS) -Tinx_mem.lds -c -O2 -o $@ $<

.PHONY: clean
clean:
	rm -rf *.kpm
	find . -name "*.o" | xargs rm -f
