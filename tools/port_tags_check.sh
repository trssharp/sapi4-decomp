#!/bin/sh
# Tagged text through the portable renderer against the untouched original in the emulator
# (SAPI4_HOOKS=none): every SAPI 4 tag the engine knows, well and badly formed, over several modes.
set -u
cd "$(dirname "$0")/.."
BIN=${1:-build/port_render}
OUT=work/port_tags
rm -rf "$OUT" && mkdir -p "$OUT"
cp tools/tag_texts.txt "$OUT/texts.txt"   # (shared with tools/api_check.sh)
n=0; fail=0
for mode in "Sam" "Mary" "Mike in Hall" "RoboSoft One" "Mary (for Telephone)" "Female Whisper"; do
  i=0
  while IFS= read -r text; do
    i=$((i + 1)); n=$((n + 1))
    slug=$(echo "$mode" | tr 'A-Z ' 'a-z_' | tr -d '()')_$i
    SAPI4_HOOKS=none build/sapi4_speak -d data/msttsl -m "$mode" -t -o "$OUT/$slug.orig.wav" "$text" > /dev/null 2>&1
    "$BIN" data/msttsl -m "$mode" -t -o "$OUT/$slug.port.wav" "$text" > /dev/null 2>&1
    a=$(python3 tools/pcmhash.py "$OUT/$slug.orig.wav" 2>/dev/null | cut -d' ' -f1,2)
    b=$(python3 tools/pcmhash.py "$OUT/$slug.port.wav" 2>/dev/null | cut -d' ' -f1,2)
    if [ -z "$a" ] || [ "$a" != "$b" ]; then echo "DIFF $slug: orig '$a' port '$b'"; fail=$((fail + 1)); fi
  done < "$OUT/texts.txt"
done
if [ $fail = 0 ]; then echo "PORT-TAGS: $n tagged renders identical to the original"; else echo "PORT-TAGS: $fail of $n differ"; exit 1; fi
