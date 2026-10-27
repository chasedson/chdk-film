#!/bin/sh
# Copy a chdk-film build onto a mounted SD card and verify it.
#
#   film-tools/install-card.sh                 # autoboot card: CHDKBOOT + FILM
#   film-tools/install-card.sh /Volumes/NAME   # any other mounted volume(s)
#
# Saved settings (CHDK/*.CFG) and photos on the card are left alone.
set -e

root=$(cd "$(dirname "$0")/.." && pwd)
cd "$root"

if [ ! -f bin/DISKBOOT.BIN ] || [ ! -f bin/PS.FIR ]; then
    echo "No build in bin/. Run: make PLATFORM=a570 PLATFORMSUB=101a fir" >&2
    exit 1
fi

if [ $# -gt 0 ]; then
    vols="$*"
else
    vols=""
    for v in /Volumes/CHDKBOOT /Volumes/FILM; do
        [ -d "$v" ] && vols="$vols $v"
    done
    if [ -z "$vols" ]; then
        echo "No /Volumes/CHDKBOOT or /Volumes/FILM mounted. Pass the card's volume path." >&2
        exit 1
    fi
fi

for v in $vols; do
    [ -d "$v" ] || { echo "$v is not mounted" >&2; exit 1; }
    echo "-> $v"
    rsync -r --exclude Makefile --exclude 'logo*.dat' --exclude '.git*' --exclude '*.CFG' CHDK/ "$v/CHDK/"
    cp -X bin/DISKBOOT.BIN bin/PS.FIR "$v/"
    dot_clean -m "$v" 2>/dev/null || true

    bad=0
    cmp -s bin/DISKBOOT.BIN "$v/DISKBOOT.BIN" || bad=1
    cmp -s bin/PS.FIR "$v/PS.FIR" || bad=1
    for f in $(cd CHDK && find . -type f ! -name Makefile ! -name 'logo*.dat'); do
        cmp -s "CHDK/$f" "$v/CHDK/$f" || { echo "   differs: $f"; bad=1; }
    done
    [ $bad -eq 0 ] && echo "   verified" || { echo "   verification FAILED" >&2; exit 1; }
done

echo "Done. Eject the card before removing it."
