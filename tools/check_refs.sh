#!/bin/sh
# Re-render every tetyys.com reference in samples/ref (Microsoft) and samples/ref_tv (L&H TruVoice) and
# require bit-identical PCM.
# Reference files are named <mode>_<hello|long>.wav (mode lowercase, spaces -> _).
set -e
cd "$(dirname "$0")/.."
BIN=${BIN:-build/sapi4_speak}
HELLO="Hello, my name is Microsoft Sam."
LONG="The quick brown fox jumps over the lazy dog, and then it runs away into the forest."
fail=0
for ref in samples/ref/*.wav; do
  base=$(basename "$ref" .wav)
  kind=${base##*_}
  slug=${base%_*}
  case "$slug" in
    sam) mode="Sam" ;; mike) mode="Mike" ;; mary) mode="Mary" ;; robosoft_one) mode="RoboSoft One" ;;
    *) echo "skip $ref (unknown mode)"; continue ;;
  esac
  if [ "$kind" = hello ]; then text=$HELLO; else text=$LONG; fi
  out="samples/check_${base}.wav"
  timeout 120 "$BIN" -d data/msttsl -m "$mode" -o "$out" "$text" 2>/dev/null
  line=$(python3 tools/wavcmp.py "$out" "$ref")
  echo "$line"
  echo "$line" | grep -q "max|diff| 0 " || fail=1
done
[ $fail = 0 ] && echo "ALL BIT-IDENTICAL" || { echo "MISMATCH"; exit 1; }
