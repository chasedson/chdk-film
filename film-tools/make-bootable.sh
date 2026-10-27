#!/bin/sh
# Mark the CHDKBOOT partition of an autoboot card as bootable by writing
# "BOOTDISK" at offset 0x40 of its boot sector (what the A570 looks for).
#
#   sudo film-tools/make-bootable.sh diskN
#
# Only touches partition 1, and only if the disk is an SD card whose first
# partition is FAT16. The rest of the boot sector is left as it was.
set -e

disk="$1"
case "$disk" in
    disk[0-9]*) ;;
    *) echo "usage: sudo $0 diskN   (see 'diskutil list')" >&2; exit 1 ;;
esac
[ "$(id -u)" -eq 0 ] || { echo "Needs root: sudo $0 $disk" >&2; exit 1; }

part="${disk}s1"
diskutil info "$disk" | grep -q 'Protocol: *Secure Digital' || { echo "$disk is not an SD card, stopping." >&2; exit 1; }
diskutil info "$part" | grep -q 'File System Personality: *MS-DOS FAT16' || { echo "$part is not FAT16 (set up the card with two partitions first)." >&2; exit 1; }

tmp=$(mktemp -t bootsector)
trap 'rm -f "$tmp"' EXIT

diskutil unmountDisk "$disk" >/dev/null
dd if="/dev/r$part" of="$tmp" bs=512 count=1 2>/dev/null

# sanity: boot signature 55 AA and FAT16 type string at 0x36
sig=$(dd if="$tmp" bs=1 skip=510 count=2 2>/dev/null | xxd -p)
fstype=$(dd if="$tmp" bs=1 skip=54 count=5 2>/dev/null)
if [ "$sig" != "55aa" ] || [ "$fstype" != "FAT16" ]; then
    diskutil mountDisk "$disk" >/dev/null
    echo "Boot sector of $part doesn't look like FAT16 (sig=$sig type=$fstype), stopping." >&2
    exit 1
fi

before=$(dd if="$tmp" bs=1 skip=64 count=8 2>/dev/null)
if [ "$before" = "BOOTDISK" ]; then
    echo "$part is already marked bootable."
else
    printf 'BOOTDISK' | dd of="$tmp" bs=1 seek=64 count=8 conv=notrunc 2>/dev/null
    dd if="$tmp" of="/dev/r$part" bs=512 count=1 2>/dev/null
    dd if="/dev/r$part" of="$tmp" bs=512 count=1 2>/dev/null     # raw devices only do whole sectors
    after=$(dd if="$tmp" bs=1 skip=64 count=8 2>/dev/null)
    [ "$after" = "BOOTDISK" ] || { diskutil mountDisk "$disk" >/dev/null; echo "Write did not verify." >&2; exit 1; }
    echo "$part marked bootable."
fi

diskutil mountDisk "$disk" >/dev/null
echo "Eject the card, slide its lock switch to LOCK, and power on."
