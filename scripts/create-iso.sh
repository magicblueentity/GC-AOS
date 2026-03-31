#!/bin/bash
# ============================================================================
# GC AOS - Create Bootable x86_64 ISO for VirtualBox/VMware
# ============================================================================

set -e

ROOT_DIR="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="${1:-${ROOT_DIR}/build/x86_64}"
ISO_DIR="${BUILD_DIR}/iso"
OUTPUT_ISO="${BUILD_DIR}/gc-aos-x86_64.iso"
KERNEL_ELF="${BUILD_DIR}/kernel/gc-aos-x86_64.elf"
GRUB_CFG_SOURCE="${ROOT_DIR}/boot/grub/grub.cfg"

echo "============================================"
echo "GC AOS x86_64 ISO Creator"
echo "============================================"

if [ ! -f "${KERNEL_ELF}" ]; then
    echo "[ERROR] x86_64 kernel not found at ${KERNEL_ELF}"
    echo "Build it first with:"
    echo "  make -f Makefile.multiarch ARCH=x86_64 kernel"
    exit 1
fi

if ! command -v grub-mkrescue >/dev/null 2>&1; then
    echo "[ERROR] grub-mkrescue not found."
    echo "Install GRUB ISO tools in WSL/Linux, for example:"
    echo "  sudo apt update"
    echo "  sudo apt install -y grub-pc-bin grub-common xorriso mtools"
    exit 1
fi

echo "[CHECK] Verifying multiboot header..."
if command -v grub-file >/dev/null 2>&1; then
    if ! grub-file --is-x86-multiboot2 "${KERNEL_ELF}"; then
        echo "[ERROR] ${KERNEL_ELF} is not recognized as Multiboot2."
        exit 1
    fi
else
    echo "[INFO] grub-file not available, skipping multiboot verification."
fi

echo "[CREATE] Setting up ISO structure..."
rm -rf "${ISO_DIR}"
mkdir -p "${ISO_DIR}/boot/grub"

echo "[COPY] Copying kernel..."
cp "${KERNEL_ELF}" "${ISO_DIR}/boot/gc-aos-x86_64.elf"
cp "${GRUB_CFG_SOURCE}" "${ISO_DIR}/boot/grub/grub.cfg"

echo "[BUILD] Creating bootable ISO with GRUB..."
grub-mkrescue -o "${OUTPUT_ISO}" "${ISO_DIR}" >/dev/null

echo ""
echo "============================================"
echo "ISO Created Successfully"
echo "============================================"
echo "Output: ${OUTPUT_ISO}"
echo "Size: $(du -h "${OUTPUT_ISO}" | cut -f1)"
echo ""
echo "VirtualBox:"
echo "  Type: Other"
echo "  Version: Other/Unknown (64-bit)"
echo "  Firmware: BIOS first, then try EFI if needed"
echo "  RAM: 2048 MB or more"
echo "  Attach ISO: ${OUTPUT_ISO}"
