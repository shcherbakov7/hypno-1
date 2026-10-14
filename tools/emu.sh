#!/bin/sh
# SPDX-License-Identifier: GPL-3.0-only
# HYPNO-1 host emulator: runs the firmware's DSP, sequencer and UI on the computer, no FM-1,
# JieLi toolchain or Docker needed. Writes the sound as WAV and the 240x240 screens as PNG
# into build/emu/.
#   tools/emu.sh            the sequencer, song, drum kits and UI pages
#   tools/emu.sh seq2_test  only the named tests (any tests/*_test.c built on hostsim.c)
set -e
cd "$(dirname "$0")/.."
OUT=build/emu
mkdir -p build/gen "$OUT"

# the generated headers tools/build.py makes for the firmware build
gen() { [ "build/gen/$2.h" -nt "tools/gen_$1.py" ] || python3 "tools/gen_$1.py" "build/gen/$2.h"; }
gen font felucca_font
gen icons felucca_icons
gen tables felucca_tables
gen samples felucca_samples
gen drumkits felucca_drumkits
gen fm6_patches felucca_fm6
gen logo sloop_logo

CC="${CC:-cc} -O2 -w -Ibuild/gen -Ifirmware/src -Ifirmware/hal"
TESTS="${*:-seq2_test song_audio_test drumkit_test ui_pages_test}"
fail=0
for t in $TESTS; do
    echo "== $t"
    $CC -o "$OUT/$t" "tests/$t.c" -lm || { fail=1; continue; }
    case $t in
        song_audio_test) "$OUT/$t" "$OUT/song-demo.wav" || fail=1 ;;
        drumkit_test) "$OUT/$t" "$OUT/drum-kits.wav" "$OUT/drum-kits.txt" || fail=1 ;;
        ui_pages_test) "$OUT/$t" "$OUT" || fail=1 ;;
        *) "$OUT/$t" || fail=1 ;;
    esac
done

# the screens at 2x as PNG, for a quick look
python3 -I - "$OUT" <<'EOF'
import pathlib, sys
from PIL import Image
for p in pathlib.Path(sys.argv[1]).glob('*.ppm'):
    im = Image.open(p)
    im.resize((im.width * 2, im.height * 2), Image.NEAREST).save(p.with_suffix('.png'))
EOF
echo "emu: sound and screens in $OUT/"
exit $fail
