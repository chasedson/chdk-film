# chdk-film

A film simulator for the Canon PowerShot A570 IS, built on [CHDK](https://chdk.fandom.com/wiki/CHDK).

Pick a film stock and every JPEG the camera takes is developed through it: tone curve, colour response, grain and vignette. RAW (DNG or CRW) still works and is saved *before* the film look is applied, so the raw file stays clean.

Like stock CHDK it runs from the SD card only. Nothing is written to the camera, so taking the card out (or unlocking it) gives you a stock camera back.

## Film stocks

| Stock | Look |
|---|---|
| Portra 400 | soft contrast, warm highlights, lifted blacks, gentle saturation |
| Ektar 100 | punchy, saturated, fine grain |
| Gold 200 | warm golden highlights, consumer-film colour |
| Superia 400 | green-leaning shadows, cooler highlights |
| Velvia 50 | very saturated slide film, deep contrast, light vignette |
| Ektachrome E100 | clean slide film with cool blues |
| Cinestill 800T | tungsten film: teal shadows, heavier grain |
| Tri-X 400 | contrasty black and white, classic grain |
| HP5 Plus | softer black and white, coarse grain |
| T-Max 100 | smooth, fine-grained black and white |
| Expired Lomo | faded, cross-processed tints, heavy vignette and grain |

## On the camera

### Film picker

Press **Print** in shooting mode and a film picker appears:

![film picker](film-tools/ui-picker_portra.png)

| Key | Does |
|---|---|
| ◀ ▶ | change film (grain resets to that film's own) |
| ▲ ▼ | grain amount, Off to 10 |
| SET | grain size: fine, medium, coarse |
| DISP | RAW: Off, DNG, CRW |
| MENU | all settings |
| Print | back to shooting |

### Developing

The screen goes dark while the camera captures, so the blue **Print** light blinks while the film is developed, getting faster as it nears done.

### Menu

Print, then **MENU**:

- **FILM**: Film stock, Grain (`auto` uses the film's own amount), Grain size, Vignette (`Auto` = only films that have one)
- **CAPTURE**: Save RAW (Off / DNG / CRW); Auto camera colour (sets Canon *My Colors* to suit the film, e.g. B/W for Tri-X. Use **P** mode: Auto ignores My Colors)
- **SCREEN**: Film badge (film, grain, RAW, battery and free card space in one pill at the top; replaces CHDK's separate battery/space/RAW indicators), Blink light while developing
- **All CHDK settings**: the full stock CHDK menu

Each shot takes a second or two longer to save while the film is developed.

## Card setup (autoboot)

The A570 can only boot from a small FAT16 partition, so the card is split in two:

| Partition | Size | Holds |
|---|---|---|
| 1 `CHDKBOOT` (FAT16) | 32 MB | `DISKBOOT.BIN`, `PS.FIR`, `CHDK/` – what the camera boots |
| 2 `FILM` (FAT32) | rest | `DISKBOOT.BIN`, `PS.FIR`, `CHDK/`, photos – CHDK switches to this after loading |

```bash
diskutil partitionDisk diskN MBR "MS-DOS FAT16" CHDKBOOT 32M "MS-DOS FAT32" FILM R
```

Copy `DISKBOOT.BIN`, `PS.FIR` and `CHDK/` onto both. Then once, in the camera: start it with **Firm Update**, then **Print → MENU → All CHDK settings → Miscellaneous → SD Card → Make card bootable**. Power off, slide the card's lock switch to **LOCK**, power on: chdk-film starts by itself from then on. To update, copy the new files onto both partitions.

## How it works

`core/film.c` runs in CHDK's raw hook, after any RAW/DNG has been written and before the camera turns the sensor data into a JPEG. For every 2×2 Bayer quad it:

1. white-balances with a grey-world estimate taken from the frame,
2. applies the vignette,
3. adjusts saturation around the quad's luminance,
4. adds luminance grain (shaped to peak in the mid tones, optionally spatially correlated for larger grain),
5. maps each channel through the stock's tone curve (built in a gamma-2 space: contrast S-curve, black lift, white point, shadow and highlight tints),
6. undoes the white balance so the camera's own pipeline develops the result.

Everything is integer maths, roughly 24 KB of lookup tables.

## Building

Needs an ARM GCC (`brew install arm-none-eabi-gcc`) and GNU make.

```bash
make PLATFORM=a570 PLATFORMSUB=101a DISABLE_GCC_VERSION_CHECK=1 fir
```

The build ends up in `bin/`. Copy `DISKBOOT.BIN` and the `CHDK/` folder to the card.

### Previewing looks on a Mac

`film-tools/host` compiles the same `film.c` for the Mac:

- `preview` simulates an A570 raw frame from any image and renders every stock plus a contact sheet
- `ui_preview` draws the picker and badge at the camera's 360×240 with CHDK's font

```bash
film-tools/host/build.sh /tmp/filmprev
sips -s format bmp photo.jpg --out /tmp/in.bmp
/tmp/filmprev/preview /tmp/in.bmp /tmp/filmprev
/tmp/filmprev/ui_preview /tmp/in.bmp /tmp/filmprev
```

## Source

Based on CHDK trunk (r6346), from the [strobecat/chdk](https://github.com/strobecat/chdk) mirror of the official Assembla SVN. GPL v2, see `COPYING`.
