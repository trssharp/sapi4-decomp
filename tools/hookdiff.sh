#!/bin/sh
# Differential test over a wider corpus than the golden hashes: every mode x several texts (tags,
# spelling, numbers, long sentences, repeated utterances on a warm engine), rendered with the original
# (SAPI4_HOOKS=none) and with every decompiled function hooked in; the PCM must be identical.
#   tools/hookdiff.sh [modes-filter]
set -u
cd "$(dirname "$0")/.."
BIN=build/sapi4_speak
OUT=work/hookdiff
rm -rf "$OUT" && mkdir -p "$OUT/none" "$OUT/all"
cat > "$OUT/texts.txt" <<'T'
0|Hello, my name is Microsoft Sam.
0|The quick brown fox jumps over the lazy dog, and then it runs away into the forest.
0|On March 3rd, 2021, I paid $45.99 for 3 items at 10:30 AM; call 555-1234. Dr. Smith's e-mail isn't working!
0|Supercalifragilisticexpialidocious! Antidisestablishmentarianism? Pneumonoultramicroscopicsilicovolcanoconiosis.
0|NASA, the FBI and IBM met at 1600 Pennsylvania Ave. NW, Washington DC 20500, on 12/25/1999 at 11:59:59 PM.
0|A b c d e f g h i j k l m n o p q r s t u v w x y z. 0 1 2 3 4 5 6 7 8 9 10 100 1000 1000000 3.14159 -42 1/2 50% #1
1|Hello \Pit=150\ there, \Spd=250\ quickly \Vol=30000\ and \Emp\ loudly. \Chr=Whisper\ Psst. \Rst\ Done.
1|\Mrk=1\ One \Mrk=2\ two. \Pau=500\ Three \Prn=h eh l ow\ four \Spd=60\ slowly \Pit=60\ low.
0|It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of Light, it was the season of Darkness.
T
"$BIN" -d data/msttsl -l | sed -E 's/ +speaker=.*//' | grep -i -- "${1:-.}" > "$OUT/modes.txt"
fail=0; n=0
while read -r mode; do
  i=0
  while IFS='|' read -r tagged text; do
    i=$((i + 1))
    slug=$(echo "$mode" | tr 'A-Z ' 'a-z_' | tr -d '()')_$i
    t=""; [ "$tagged" = 1 ] && t="-t"
    for h in none all; do
      # shellcheck disable=SC2086
      SAPI4_HOOKS=$h timeout 120 "$BIN" -d data/msttsl -m "$mode" $t -r 2 -o "$OUT/$h/$slug.wav" "$text" 2>/dev/null
    done
    n=$((n + 1))
    if ! cmp -s "$OUT/none/$slug.wav" "$OUT/all/$slug.wav"; then echo "DIFF $slug"; fail=1; fi
  done < "$OUT/texts.txt"
done < "$OUT/modes.txt"
[ $fail = 0 ] && echo "HOOKDIFF: $n renders identical with and without hooks" || { echo "HOOKDIFF: MISMATCH"; exit 1; }
