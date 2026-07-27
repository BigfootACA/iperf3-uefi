SHELL := /bin/bash

ARCH ?= $(shell uname -m)
EDK2_PATH ?= $(realpath edk2)
EDK2_TARGET ?= RELEASE
EDK2_TOOLCHAIN ?= GCC5
EDK_TOOLS_PATH ?= $(abspath $(EDK2_PATH)/BaseTools)
WORKSPACE ?= $(CURDIR)/build
PACKAGES_PATH ?= $(abspath $(EDK2_PATH)):$(CURDIR)

ifeq ($(ARCH),x86_64)
	EDK2_ARCH := X64
else ifeq ($(ARCH),amd64)
	EDK2_ARCH := X64
else ifeq ($(ARCH),aarch64)
	EDK2_ARCH := AARCH64
else ifeq ($(ARCH),arm64)
	EDK2_ARCH := AARCH64
else ifeq ($(ARCH),i386)
	EDK2_ARCH := IA32
else ifeq ($(ARCH),arm)
	EDK2_ARCH := ARM
else
	$(error Unsupported architecture: $(ARCH))
endif

OUTPUT_DIR := $(WORKSPACE)/Build/IPerf3Pkg/$(EDK2_TARGET)_$(EDK2_TOOLCHAIN)/$(EDK2_ARCH)
EFI_BINARY := $(OUTPUT_DIR)/IPerf3.efi

export EDK_TOOLS_PATH PACKAGES_PATH WORKSPACE

all: build-edk2

basetools: patched
	@test -f "$(EDK2_PATH)/edksetup.sh" || \
		{ echo "EDK2 checkout not found; set EDK2_PATH or clone it as ./edk2" >&2; exit 1; }
	$(MAKE) -C "$(EDK_TOOLS_PATH)"

patched: scripts/patch.sh
	bash scripts/patch.sh

build-edk2: patched
	@mkdir -p "$(WORKSPACE)"
	source "$(EDK2_PATH)/edksetup.sh" && \
		build -a "$(EDK2_ARCH)" -t "$(EDK2_TOOLCHAIN)" \
		-b "$(EDK2_TARGET)" -p IPerf3Pkg/IPerf3Pkg.dsc
	@printf 'Built %s\n' "$(EFI_BINARY)"

clean:
	rm -rf "$(WORKSPACE)/Build/IPerf3Pkg"

.PHONY: all basetools patched build-edk2 clean
