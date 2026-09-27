#!/bin/sh
# The portable build end to end (build/port_render: no emulator) against the golden renders: every
# voice mode, both golden texts, PCM hashes compared with tools/golden_hashes.txt (made by the
# original in the emulator), and the golden set's speed / pitch renders.
set -u
cd "$(dirname "$0")/.."
BIN=${1:-build/port_render}
OUT=work/port_render
rm -rf "$OUT" && mkdir -p "$OUT"
T1="The quick brown fox jumps over the lazy dog, and then it runs away into the forest."
T2="On March 3rd, 2021, I paid \$45.99 for 3 items at 10:30 AM; call 555-1234. Dr. Smith's e-mail isn't working!"
# the modes the golden set covers (the harness lists these; the engine also offers three more)
cat > "$OUT/modes.txt" <<'M'
Mary
Mary (for Telephone)
Mike
Mike (for Telephone)
Sam
Female Whisper
Mary in Space
Mary in Hall
Mary in Stadium
RoboSoft Six
RoboSoft Five
RoboSoft Four
Male Whisper
RoboSoft One
RoboSoft Two
RoboSoft Three
Mike in Hall
Mike in Stadium
Mike in Space
M
n=0; fail=0
while read -r mode; do
  slug=$(echo "$mode" | tr 'A-Z ' 'a-z_' | tr -d '()')
  "$BIN" data/msttsl -m "$mode" -o "$OUT/${slug}_1.wav" "$T1" 2>/dev/null
  "$BIN" data/msttsl -m "$mode" -o "$OUT/${slug}_2.wav" "$T2" 2>/dev/null
  for k in 1 2; do
    n=$((n + 1))
    want=$(grep " ${slug}_$k.wav\$" tools/golden_hashes.txt)
    got=$(python3 tools/pcmhash.py "$OUT/${slug}_$k.wav" 2>/dev/null)
    if [ "$want" != "$got" ]; then echo "DIFF ${slug}_$k: want '$want' got '$got'"; fail=$((fail + 1)); fi
  done
done < "$OUT/modes.txt"
for mode in "Sam" "Mary in Hall" "Male Whisper" "Mike (for Telephone)"; do
  slug=$(echo "$mode" | tr 'A-Z ' 'a-z_' | tr -d '()')
  "$BIN" data/msttsl -m "$mode" -s 300 -p 140 -o "$OUT/${slug}_fastpitch.wav" "$T1" 2>/dev/null
  "$BIN" data/msttsl -m "$mode" -s 60 -p 60 -o "$OUT/${slug}_slowlow.wav" "$T1" 2>/dev/null
  for k in fastpitch slowlow; do
    n=$((n + 1))
    want=$(grep " ${slug}_$k.wav\$" tools/golden_hashes.txt)
    got=$(python3 tools/pcmhash.py "$OUT/${slug}_$k.wav" 2>/dev/null)
    if [ "$want" != "$got" ]; then echo "DIFF ${slug}_$k: want '$want' got '$got'"; fail=$((fail + 1)); fi
  done
done
if [ $fail = 0 ]; then echo "PORT-RENDER: all $n golden renders (19 modes x 2 texts, 4 modes x speed/pitch extremes) identical, with no emulator"; else echo "PORT-RENDER: $fail of $n differ"; exit 1; fi
