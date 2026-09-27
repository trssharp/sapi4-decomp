#!/bin/bash
# The library API (lib/sapi4tts.h) end to end, bit for bit:
#  1. the 46 golden renders, all in one process with a fresh engine each - then again on 4 threads;
#  2. the tagged texts over six modes, against the untouched original in the emulator;
#  3. the user lexicon files (one process per file: a process loads one user lexicon);
#  4. a warm engine speaking twice, against the harness's -r 2;
#  5. each mode's pitch and speed limits, against the harness's;
#  6. stop, reset and clamping (tools/api_extras).
set -u
cd "$(dirname "$0")/.."
BIN=build/api_check
EMU=build/sapi4_speak
DLL=data/msttsl/msttssyn.dll
OUT=work/api
rm -rf "$OUT" && mkdir -p "$OUT/golden" "$OUT/golden4" "$OUT/tags" "$OUT/userlex" "$OUT/warm"
fail=0
T1="The quick brown fox jumps over the lazy dog, and then it runs away into the forest."
T2="On March 3rd, 2021, I paid \$45.99 for 3 items at 10:30 AM; call 555-1234. Dr. Smith's e-mail isn't working!"
MODES="Mary|Mary (for Telephone)|Mike|Mike (for Telephone)|Sam|Female Whisper|Mary in Space|Mary in Hall|Mary in Stadium|RoboSoft Six|RoboSoft Five|RoboSoft Four|Male Whisper|RoboSoft One|RoboSoft Two|RoboSoft Three|Mike in Hall|Mike in Stadium|Mike in Space"
slug() { echo "$1" | tr 'A-Z ' 'a-z_' | tr -d '()'; }

# 1. golden
: > "$OUT/template.jobs"
echo "$MODES" | tr '|' '\n' > "$OUT/modes.txt"
while read -r m; do
  s=$(slug "$m")
  echo "OUTDIR/${s}_1.wav|data/msttsl|$m|0|-1|-1|1|$T1" >> "$OUT/template.jobs"
  echo "OUTDIR/${s}_2.wav|data/msttsl|$m|0|-1|-1|1|$T2" >> "$OUT/template.jobs"
done < "$OUT/modes.txt"
for m in "Sam" "Mary in Hall" "Male Whisper" "Mike (for Telephone)"; do
  s=$(slug "$m")
  echo "OUTDIR/${s}_fastpitch.wav|data/msttsl|$m|0|140|300|1|$T1" >> "$OUT/template.jobs"
  echo "OUTDIR/${s}_slowlow.wav|data/msttsl|$m|0|60|60|1|$T1" >> "$OUT/template.jobs"
done
for d in golden golden4; do
  sed "s#OUTDIR#$OUT/$d#" "$OUT/template.jobs" > "$OUT/$d.jobs"
  th=1; [ $d = golden4 ] && th=4
  "$BIN" "$DLL" "$OUT/$d.jobs" $th > "$OUT/$d.limits" || { echo "API: $d run failed"; fail=1; }
  python3 tools/pcmhash.py "$OUT/$d"/*.wav > "$OUT/$d.hashes"
  if [ "$(sort -k3 tools/golden_hashes.txt)" != "$(sort -k3 "$OUT/$d.hashes")" ]; then echo "API: $d differs from the golden hashes"; diff <(sort -k3 tools/golden_hashes.txt) <(sort -k3 "$OUT/$d.hashes") | head; fail=1; fi
done
echo "API: 46 golden renders in one process ($(grep -c . "$OUT/golden.hashes") checked), and again on 4 threads"

# 2. tags
: > "$OUT/tags.jobs"
n=0
for m in "Sam" "Mary" "Mike in Hall" "RoboSoft One" "Mary (for Telephone)" "Female Whisper"; do
  i=0
  while IFS= read -r t; do
    [ -z "$t" ] && continue
    i=$((i + 1)); n=$((n + 1))
    s=$(slug "$m")_$i
    SAPI4_HOOKS=none "$EMU" -d data/msttsl -m "$m" -t -o "$OUT/tags/$s.orig.wav" "$t" > /dev/null 2>&1
    printf '%s|data/msttsl|%s|1|-1|-1|1|%s\n' "$OUT/tags/$s.api.wav" "$m" "$t" >> "$OUT/tags.jobs"
  done < tools/tag_texts.txt
done
"$BIN" "$DLL" "$OUT/tags.jobs" > /dev/null || { echo "API: tags run failed"; fail=1; }
tf=0
for f in "$OUT"/tags/*.orig.wav; do
  a=$(python3 tools/pcmhash.py "$f" | cut -d' ' -f1,2); b=$(python3 tools/pcmhash.py "${f%.orig.wav}.api.wav" 2>/dev/null | cut -d' ' -f1,2)
  [ "$a" = "$b" ] || { echo "DIFF tags $(basename "$f")"; tf=$((tf + 1)); }
done
[ $tf = 0 ] && echo "API: $n tagged renders identical to the original" || { echo "API: $tf tagged renders differ"; fail=1; }

# 3. user lexicons (the directories tools/userlex_check.sh makes)
[ -d work/userlex/good ] || ./tools/userlex_check.sh > /dev/null
uf=0; un=0
TEXT="The blorp, a flimsy zorgle, met an aardvark from NASA. O'blorp? X-ray zed!"
for d in work/userlex/*/; do
  name=$(basename "$d")
  : > "$OUT/userlex/$name.jobs"
  for m in "Sam" "Mary in Hall"; do
    printf '%s|%s|%s|0|-1|-1|1|%s\n' "$OUT/userlex/${name}_$(slug "$m").wav" "${d%/}" "$m" "$TEXT" >> "$OUT/userlex/$name.jobs"
  done
  "$BIN" "$DLL" "$OUT/userlex/$name.jobs" > /dev/null || { echo "API: userlex $name failed"; uf=$((uf + 1)); }
  for m in "Sam" "Mary in Hall"; do
    un=$((un + 1))
    s="${name}_$(slug "$m")"
    a=$(python3 tools/pcmhash.py "work/userlex/$s.none.wav" | cut -d' ' -f1,2); b=$(python3 tools/pcmhash.py "$OUT/userlex/$s.wav" 2>/dev/null | cut -d' ' -f1,2)
    [ "$a" = "$b" ] || { echo "DIFF userlex $s"; uf=$((uf + 1)); }
  done
done
[ $uf = 0 ] && echo "API: $un user-lexicon renders identical to the original" || { echo "API: $uf user-lexicon renders differ"; fail=1; }

# 4. a warm engine
: > "$OUT/warm.jobs"
for m in "Sam" "RoboSoft One" "Mary in Hall"; do
  s=$(slug "$m")
  SAPI4_HOOKS=none "$EMU" -d data/msttsl -m "$m" -r 2 -o "$OUT/warm/$s.orig.wav" "$T1" > /dev/null 2>&1
  echo "$OUT/warm/$s.api.wav|data/msttsl|$m|0|-1|-1|2|$T1" >> "$OUT/warm.jobs"
done
"$BIN" "$DLL" "$OUT/warm.jobs" > /dev/null || { echo "API: warm run failed"; fail=1; }
wf=0
for f in "$OUT"/warm/*.orig.wav; do
  a=$(python3 tools/pcmhash.py "$f" | cut -d' ' -f1,2); b=$(python3 tools/pcmhash.py "${f%.orig.wav}.api.wav" 2>/dev/null | cut -d' ' -f1,2)
  [ "$a" = "$b" ] || { echo "DIFF warm $(basename "$f")"; wf=$((wf + 1)); }
done
[ $wf = 0 ] && echo "API: a warm engine's second utterance identical to the original's (3 modes)" || { echo "API: $wf warm renders differ"; fail=1; }

# 5. limits
lf=0
while read -r m; do
  want=$("$EMU" -d data/msttsl -m "$m" -o /dev/null "x" 2>&1 | sed -n 's/.*pitch \([0-9]*\) (\([0-9]*\)\.\.\([0-9]*\)), speed \([0-9]*\) (\([0-9]*\)\.\.\([0-9]*\)).*/pitch \1 \2 \3 speed \4 \5 \6/p')
  got=$(grep "^limits $m pitch" "$OUT/golden.limits" | sed "s/^limits $m //")
  [ "$want" = "$got" ] || { echo "DIFF limits $m: want '$want' got '$got'"; lf=$((lf + 1)); }
done < "$OUT/modes.txt"
[ $lf = 0 ] && echo "API: pitch and speed limits of all 19 modes as the original reports them" || { echo "API: $lf limits differ"; fail=1; }

# 6. extras
build/api_extras "$DLL" data/msttsl > "$OUT/extras.txt" || { cat "$OUT/extras.txt"; echo "API: extras failed"; fail=1; }
echo "API: $(grep -c '^ok' "$OUT/extras.txt") behaviour checks (stop, reset, clamping, many engines)"
[ $fail = 0 ] && echo "API-CHECK: all identical" || { echo "API-CHECK: FAILED"; exit 1; }
