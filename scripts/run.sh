#!/bin/bash

set -ex

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_ROOT/build/disk"
IMAGE_NAME="iperf3-uefi-boot.img"
IMAGE_PATH="$BUILD_DIR/$IMAGE_NAME"
if [ -z "$EDK2_TARGET" ]; then
	export EDK2_TARGET=RELEASE
fi
if [ -z "$ARCH" ]; then
	export ARCH="$(uname -m)"
fi
case "$ARCH" in
	x86_64|x64|amd64) export EDK2_ARCH=X64 ;;
	aarch64|arm64|armv8a|armv8l) export EDK2_ARCH=AARCH64 ;;
	i386|i486|i586|i686|x86|ia32) export EDK2_ARCH=IA32 ;;
	*) echo "Unsupported architecture: $ARCH" >&2; exit 1 ;;
esac
typeset -l EDK2_LARCH
export EDK2_LARCH="$EDK2_ARCH"

function find_first_file() {
	for path in "$@"; do
		if [ -f "$path" ]; then
			echo "$path"
			return 0
		fi
	done
	return 1
}

function build_efi() {
	cd "$PROJECT_ROOT"
	make -j 4
}

function pack_boot() {
	command -v mformat >/dev/null || { echo "Missing mformat"; exit 1; }
	command -v mcopy >/dev/null || { echo "Missing mcopy"; exit 1; }
	mkdir -p "$BUILD_DIR"
	rm -f "$IMAGE_PATH"
	truncate -s 64M "$IMAGE_PATH"
	mformat -i "$IMAGE_PATH" -F ::
	mcopy -i "$IMAGE_PATH" \
		"$PROJECT_ROOT/build/Build/IPerf3Pkg/${EDK2_TARGET}_${EDK2_TOOLCHAIN:-GCC5}/$EDK2_ARCH/IPerf3.efi" \
		::/IPerf3.efi
}

function run_qemu_x86_64() {
	local firmware shell
	firmware="${QEMU_FIRMWARE:-$(find_first_file /usr/share/edk2/x64/OVMF.4m.fd /usr/share/OVMF/OVMF_CODE.fd)}"
	shell="${UEFI_SHELL:-$(find_first_file /usr/share/edk2-shell/x64/Shell_Full.efi /usr/share/efi-shell-x64/shellx64.efi)}"
	exec qemu-system-x86_64 \
		-machine q35 \
		-nic none \
		-display none \
		-serial stdio \
		-monitor none \
		-bios "$firmware" \
		-kernel "$shell" \
		-drive if=none,id=disk,format=raw,file="$IMAGE_PATH" \
		-device virtio-blk-pci,drive=disk
}

function run_qemu_aarch64() {
	local firmware shell
	firmware="${QEMU_FIRMWARE:-$(find_first_file /usr/share/edk2/aarch64/QEMU_EFI.fd /usr/share/AAVMF/AAVMF_CODE.fd /usr/share/qemu-efi-aarch64/QEMU_EFI.fd)}"
	shell="${UEFI_SHELL:-$(find_first_file /usr/share/edk2-shell/aarch64/Shell_Full.efi /usr/share/efi-shell-aa64/shellaa64.efi)}"
	exec qemu-system-aarch64 \
		-m 512M \
		-machine virt \
		-cpu max \
		-nic none \
		-display none \
		-serial stdio \
		-monitor none \
		-bios "$firmware" \
		-kernel "$shell" \
		-drive if=none,id=disk,format=raw,file="$IMAGE_PATH" \
		-device virtio-blk-device,drive=disk
}

function run_qemu() {
	case "$EDK2_ARCH" in
		X64) run_qemu_x86_64 ;;
		AARCH64) run_qemu_aarch64 ;;
		*) echo "QEMU is unsupported for $EDK2_ARCH" >&2; exit 1 ;;
	esac
}

case "$1" in
	build_efi) build_efi ;;
	pack_boot) pack_boot ;;
	run_qemu) run_qemu ;;
	run_qemu_x86_64) run_qemu_x86_64 ;;
	run_qemu_aarch64) run_qemu_aarch64 ;;
	"") build_efi; pack_boot; run_qemu ;;
	*) echo "Unknown command: $1" >&2; exit 1 ;;
esac
