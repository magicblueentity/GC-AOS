#!/bin/bash
# Create bootable disk image for GC AOS
# Creates a GPT-partitioned disk image with EFI and root partitions

set -e

BUILD_DIR="${1:-build}"
IMAGE_DIR="${2:-image}"
IMAGE_NAME="gc-aos.img"
IMAGE_SIZE="8G"

# Colors
GREEN='\033[0;32m'
NC='\033[0m'

log() {
    echo -e "${GREEN}[IMAGE]${NC} $1"
}

# Create image directory
mkdir -p "$IMAGE_DIR"

IMAGE_PATH="$IMAGE_DIR/$IMAGE_NAME"

log "Creating disk image: $IMAGE_PATH ($IMAGE_SIZE)"

case "$(uname -s)" in
    Darwin)
        # Create empty disk image
        dd if=/dev/zero of="$IMAGE_PATH" bs=1M count=8192 2>/dev/null

        log "Creating GPT partition table..."

        DISK=$(hdiutil attach -nomount "$IMAGE_PATH" | head -1 | awk '{print $1}')

        if [ -z "$DISK" ]; then
            log "Failed to attach disk image"
            exit 1
        fi

        log "Attached disk image as $DISK"

        diskutil partitionDisk "$DISK" GPT \
            FAT32 EFI 500M \
            "Free Space" ROOT R \
            2>/dev/null || {
            log "Using fallback partition method..."
            diskutil eraseDisk GPT "GC AOS" "$DISK"
        }

        EFI_PART="${DISK}s1"
        ROOT_PART="${DISK}s2"

        log "EFI partition: $EFI_PART"
        log "Root partition: $ROOT_PART"

        EFI_MOUNT=$(mktemp -d)
        mount -t msdos "$EFI_PART" "$EFI_MOUNT" 2>/dev/null || {
            log "Could not mount EFI partition directly, using diskutil..."
            diskutil mount "$EFI_PART"
            EFI_MOUNT="/Volumes/EFI"
        }

        log "EFI mounted at $EFI_MOUNT"
        mkdir -p "$EFI_MOUNT/EFI/BOOT"

        if [ -f "$BUILD_DIR/kernel/gc-aos.efi" ]; then
            cp "$BUILD_DIR/kernel/gc-aos.efi" "$EFI_MOUNT/EFI/BOOT/BOOTAA64.EFI"
            log "Copied kernel EFI stub"
        elif [ -f "$BUILD_DIR/kernel/gc-aos.elf" ]; then
            log "Creating boot configuration..."
            cat > "$EFI_MOUNT/EFI/BOOT/startup.nsh" << 'EOF'
@echo -off
echo GC AOS Boot Loader
echo Loading kernel...
\EFI\BOOT\kernel.elf
EOF
            cp "$BUILD_DIR/kernel/gc-aos.elf" "$EFI_MOUNT/EFI/BOOT/kernel.elf" 2>/dev/null || {
                log "Kernel not yet built, creating placeholder..."
                echo "GC AOS kernel placeholder" > "$EFI_MOUNT/EFI/BOOT/kernel.txt"
            }
        else
            log "Kernel not yet built, creating boot structure only..."
            echo "GC AOS - Kernel not yet built" > "$EFI_MOUNT/EFI/BOOT/README.txt"
        fi

        cat > "$EFI_MOUNT/EFI/BOOT/boot.json" << EOF
{
    "name": "GC AOS",
    "version": "0.1.0",
    "arch": "arm64",
    "kernel": "kernel.elf",
    "cmdline": "console=serial0 root=/dev/nvme0n1p2"
}
EOF

        sync

        log "Unmounting partitions..."
        umount "$EFI_MOUNT" 2>/dev/null || diskutil unmount "$EFI_PART" 2>/dev/null || true

        hdiutil detach "$DISK" 2>/dev/null || {
            log "Disk may be in use, force detaching..."
            hdiutil detach "$DISK" -force
        }
        ;;
    *)
        if command -v truncate >/dev/null 2>&1; then
            truncate -s "$IMAGE_SIZE" "$IMAGE_PATH"
        else
            dd if=/dev/zero of="$IMAGE_PATH" bs=1M count=8192 2>/dev/null
        fi

        if ! command -v parted >/dev/null 2>&1; then
            log "parted not found; created raw 8G disk image without partitions"
        else
            log "Creating GPT partition table..."
            parted -s "$IMAGE_PATH" mklabel gpt
            parted -s "$IMAGE_PATH" mkpart ESP fat32 1MiB 513MiB
            parted -s "$IMAGE_PATH" set 1 esp on
            parted -s "$IMAGE_PATH" mkpart primary ext4 513MiB 100%
            log "Created EFI and root partitions inside raw disk image"
        fi
        ;;
esac

log "Boot image created successfully: $IMAGE_PATH"
ls -lh "$IMAGE_PATH"

echo ""
log "To test in QEMU: make qemu"
log "To boot with GUI + persistent disk: make run-gui"
