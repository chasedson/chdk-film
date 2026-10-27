# Installing chdk-film

chdk-film runs from the SD card, like stock CHDK. Nothing is written to the camera's firmware: unlock the card (or take it out) and the camera is completely stock again.

These steps are for macOS. Linux works the same way apart from the disk commands.

**Contents**

1. [What you need](#1-what-you-need)
2. [Check your camera's firmware version](#2-check-your-cameras-firmware-version)
3. [Build chdk-film](#3-build-chdk-film)
4. [Prepare the SD card](#4-prepare-the-sd-card)
5. [Install onto the card](#5-install-onto-the-card)
6. [Start it on the camera](#6-start-it-on-the-camera)
7. [Updating](#7-updating)
8. [Using it](#8-using-it)
9. [Troubleshooting](#9-troubleshooting)
10. [Removing it](#10-removing-it)

---

## 1. What you need

| | |
|---|---|
| Camera | Canon PowerShot **A570 IS**, firmware **1.01A** (1.00E builds too, but is untested) |
| SD card | Any SD or SDHC card. It will be **erased** if you set up autoboot (step 4B) |
| Mac | Homebrew, Xcode command line tools (`xcode-select --install`) |
| Compiler | ARM GCC: `brew install arm-none-eabi-gcc arm-none-eabi-binutils` |

## 2. Check your camera's firmware version

You only need to do this once.

1. Put an empty file named `ver.req` in the root of the card:

   ```bash
   touch /Volumes/<CARD>/ver.req
   ```

2. Put the card in the camera and turn it on in **playback** mode.
3. Hold **FUNC/SET**, then press **DISP**.
4. Read the `Firmware Ver GM1.01A` line. `GM1.01A` means `101a`; `GM1.00E` means `100e`.

## 3. Build chdk-film

```bash
git clone <this repo> chdk-film
cd chdk-film
make PLATFORM=a570 PLATFORMSUB=101a DISABLE_GCC_VERSION_CHECK=1 fir
```

The build ends with `Firmware creation completed successfully` and leaves:

| File | What it is |
|---|---|
| `bin/DISKBOOT.BIN` | chdk-film itself, for autoboot |
| `bin/PS.FIR` | the same, packaged for the camera's **Firm Update** menu |
| `CHDK/` | modules, fonts, language files and scripts CHDK loads at runtime |

> **About the compiler version.** CHDK officially supports GCC 4, 5 and 8–11. Homebrew currently ships a newer GCC, so the build passes `DISABLE_GCC_VERSION_CHECK=1`, and `arm_rules.inc` adds flags that keep CHDK's older C compiling. chdk-film has been run on an A570 built this way. CHDK warns that very new GCCs have produced builds that don't boot on *some* cameras. If yours doesn't start, build with ARM's GCC 10.3 instead ([Arm GNU Toolchain 10.3-2021.10](https://developer.arm.com/downloads/-/gnu-rm)). Put its `bin/` first on your `PATH` and drop `DISABLE_GCC_VERSION_CHECK=1`.

## 4. Prepare the SD card

Pick one:

| | Start-up | Card layout |
|---|---|---|
| **A. Firm Update** | MENU → Firm Update every time you turn the camera on | Card as it is (one FAT32 partition is fine) |
| **B. Autoboot** (recommended) | Starts by itself whenever the card is locked | Card split into two partitions. **Erases the card** |

### A. Firm Update

Nothing to prepare. Go to [step 5](#5-install-onto-the-card).

### B. Autoboot

The A570 can only boot from a small FAT16 partition, so the card gets two:

| Partition | Size | Format | Holds |
|---|---|---|---|
| 1 `CHDKBOOT` | 32 MB | FAT16 | what the camera boots |
| 2 `FILM` | the rest | FAT32 | photos. CHDK switches the camera to this partition once it has loaded |

**Back up the card first.** This erases everything on it:

```bash
rsync -a --exclude '.Spotlight-V100' --exclude '.fseventsd' /Volumes/<CARD>/ ~/Desktop/"SD card backup"/
```

Find the card's disk number. Look for the `Secure Digital` disk of the right size and **make sure it isn't your Mac's drive**:

```bash
diskutil list
diskutil info diskN | grep -E 'Protocol|Disk Size'
```

Partition it, replacing `diskN`:

```bash
diskutil partitionDisk diskN MBR "MS-DOS FAT16" CHDKBOOT 32M "MS-DOS FAT32" FILM R
```

`diskutil list diskN` should now show `DOS_FAT_16 CHDKBOOT` and `DOS_FAT_32 FILM`.

To restore your old photos, copy the backed-up `DCIM` folder onto **FILM**.

## 5. Install onto the card

```bash
film-tools/install-card.sh
```

With no arguments, it copies the build onto `CHDKBOOT` and `FILM` (autoboot layout). For a single-partition card, pass the volume instead:

```bash
film-tools/install-card.sh /Volumes/<CARD>
```

It copies `DISKBOOT.BIN`, `PS.FIR` and `CHDK/`, then checks every file against the build. It never overwrites your saved settings (`CHDK/CCHDK4.CFG`) or photos.

Eject the card when it's done:

```bash
diskutil eject diskN
```

## 6. Start it on the camera

### First start (both layouts)

1. Card **unlocked**, camera on in **playback** mode.
2. **MENU** → settings (wrench) tab → **Firm Update…** → **OK**.
3. A dark splash screen shows **chdk-film** and the current film in red.

With layout A, that's it. Repeat this every time you turn the camera on.

### Turn on autoboot (layout B, once)

The camera boots from `CHDKBOOT` only if that partition's boot sector is marked with `BOOTDISK`. Mark it from the Mac (needs your password). Replace `diskN` with the card's disk from `diskutil list`:

```bash
sudo film-tools/make-bootable.sh diskN
```

The script refuses to run unless `diskN` is an SD card whose first partition is FAT16. It changes only those 8 bytes, reads them back to confirm, and leaves the files alone.

Then:

1. Eject the card and slide its lock switch to **LOCK**.
2. Put it in the camera and turn it on. chdk-film starts by itself, now and every time the card is locked.

The camera still saves photos to a locked card. The lock only tells it to boot chdk-film.

> **Why not CHDK's "Make card bootable" menu item?** On this two-partition layout it doesn't work. Once chdk-film is running, CHDK has switched the camera to the `FILM` partition, so the menu item marks `FILM` rather than `CHDKBOOT`, and the camera never boots from `FILM`.

## 7. Updating

```bash
git pull
make PLATFORM=a570 PLATFORMSUB=101a DISABLE_GCC_VERSION_CHECK=1 fir
film-tools/install-card.sh
```

Your settings and photos stay. On an autoboot card you don't need to run `make-bootable.sh` again: copying files doesn't touch the boot sector. Lock the card and power on.

## 8. Using it

Use **P** mode. Auto mode ignores the camera colour setting each film relies on.

| On the camera | |
|---|---|
| **Print** | film picker: ◀▶ film, ▲▼ grain, SET grain size, DISP RAW. Print again to go back |
| **Print, then MENU** | full settings: film, grain, vignette, RAW, camera colour, badge, blink light, and **All CHDK settings** |
| Top of screen | `● PORTRA G3 DNG \| 87% 29.2G` – film, grain, RAW, battery, free card space |
| Blue Print light | blinks while a shot develops, faster as it finishes (about 1–2 s per shot) |

RAW files (DNG or CRW) are saved **before** the film look is applied, so they stay clean. Only the JPEG gets the film.

See [README.md](README.md) for the film stocks and how the processing works.

## 9. Troubleshooting

| Problem | Fix |
|---|---|
| Nothing happens / black screen at start-up | Pull the battery and take the card out: the camera is stock again. Check the firmware version (step 2) matches your build, then try a GCC 10 build (see step 3) |
| **Firm Update** isn't in the menu | The camera must be in **playback** mode, and `PS.FIR` must be in the root of the card it booted from. On the autoboot layout, that's `CHDKBOOT` |
| Locked card doesn't autoboot | Run `sudo film-tools/make-bootable.sh diskN` (step 6). Check the card's lock switch is fully at LOCK. Use the Mac script rather than CHDK's own *Make card bootable* menu item, which marks the wrong partition on this layout |
| Film colours look the same in every mode | Switch to **P** mode, and check **Auto camera colour** is on in the menu |
| No photos when the card is in the Mac | On the autoboot layout, photos are on the **FILM** drive, not CHDKBOOT |
| Shots take longer to save | Expected: about 1–2 s per shot to develop the film. Set the film to **Off** to skip processing |
| Want CHDK's own battery/space readouts back | **All CHDK settings → OSD parameters**. They're off by default because the film badge shows the same information |

## 10. Removing it

- **For a shoot:** slide the card's lock switch to unlocked. The camera starts stock and chdk-film doesn't load.
- **For good:** reformat the card in the camera (**MENU → Format**), or on the Mac with one partition:

  ```bash
  diskutil eraseDisk FAT32 CARD MBRFormat diskN
  ```

  Copy your photos off first.
