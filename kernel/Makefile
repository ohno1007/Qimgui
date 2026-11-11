# SPDX-License-Identifier: GPL-2.0
# Builds qimgui.ko out of tree.
#
#   make -C kernel KDIR=/path/to/kernel/source \
#        ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- QK_CXX=aarch64-linux-gnu-g++
#
# Use the toolchain the target kernel itself was built with: a module built by
# another compiler disagrees with the kernel about CFI, the stack protector and
# structure layout. For a kernel built with clang, pass CC=clang LLVM=1
# QK_CXX=clang++.
#
# The sources span several directories of this repository and include each
# other by relative path, so they are copied into a staging tree that keeps
# that layout; the kernel build writes its objects there and never into the
# repository.

KDIR ?= /lib/modules/$(shell uname -r)/build
STAGE ?= $(CURDIR)/.build
ROOT := $(CURDIR)/..

STAGED := \
	kernel/src kernel/compat kernel/raster kernel/ui \
	config/aimgui_imconfig.h \
	third_party/imgui third_party/material_shapes \
	jni/include/ui jni/include/qimgui jni/src/ui jni/src/core/font_data.h \
	jni/src/core/glass.h jni/src/core/haptics.h

.PHONY: all modules stage clean help

all: modules

stage:
	@mkdir -p $(STAGE)
	@cd $(ROOT) && cp -r --parents $(STAGED) $(STAGE)/
	@cp $(CURDIR)/Kbuild $(STAGE)/Kbuild
	@cp $(CURDIR)/Kbuild $(STAGE)/Makefile

# PAHOLE/RESOLVE_BTFIDS can be pointed at /bin/true when the host's pahole is
# older than the target kernel wants. The module then carries no BTF, which
# the kernel does not need to load it -- but CONFIG_DEBUG_INFO_BTF_MODULES
# must stay as the target kernel has it, because it changes struct module's
# size and the kernel refuses a module that disagrees about that.
modules: stage
	$(MAKE) -C $(KDIR) M=$(STAGE) $(if $(QK_CXX),QK_CXX=$(QK_CXX),) \
		$(if $(PAHOLE),PAHOLE=$(PAHOLE),) \
		$(if $(RESOLVE_BTFIDS),RESOLVE_BTFIDS=$(RESOLVE_BTFIDS),) modules

clean:
	@rm -rf $(STAGE)

help:
	@echo 'make -C kernel KDIR=<kernel source> ARCH=arm64 \'
	@echo '     CROSS_COMPILE=aarch64-linux-gnu- QK_CXX=aarch64-linux-gnu-g++'
	@echo
	@echo 'Output: kernel/.build/qimgui.ko'
	@echo 'Load:   insmod qimgui.ko width=640 height=1280 pos_x=220 pos_y=560'
	@echo 'Status: cat /proc/qimgui'
