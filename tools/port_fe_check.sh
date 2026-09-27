#!/bin/sh
# The portable front end (build/port_fe) against the original front end in the emulator: for each voice
# mode and text, the phone lists the unit stage receives must be identical (the emulator run hooks only
# the unit stage's entry, to record them, so its front end is the original's). The voice data is not
# ported yet: a voice's pitch and character variant are taken from the emulator run.
set -u
cd "$(dirname "$0")/.."
OUT=work/port_fe
rm -rf "$OUT" && mkdir -p "$OUT"
cat > "$OUT/texts.txt" <<'T'
0|Hello, my name is Microsoft Sam.
0|The quick brown fox jumps over the lazy dog, and then it runs away into the forest.
0|On March 3rd, 2021, I paid $45.99 for 3 items at 10:30 AM; call 555-1234. Dr. Smith's e-mail isn't working!
0|Supercalifragilisticexpialidocious! Antidisestablishmentarianism? Pneumonoultramicroscopicsilicovolcanoconiosis.
0|NASA, the FBI and IBM met at 1600 Pennsylvania Ave. NW, Washington DC 20500, on 12/25/1999 at 11:59:59 PM.
0|A b c d e f g h i j k l m n o p q r s t u v w x y z. 0 1 2 3 4 5 6 7 8 9 10 100 1000 1000000 3.14159 -42 1/2 50% #1
0|It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief, it was the epoch of incredulity, it was the season of Light, it was the season of Darkness.
0|"Quoted," she said (in brackets) [and more] {and braces}. Mr. and Mrs. J. R. R. Tolkien lived at 221B Baker St. in the U.K.
0|What?! No... yes; maybe: 7 x 8 = 56. Visit www.example.com or mail me at bob@example.com, ok?
1|Hello \Pit=150\ there, \Spd=250\ quickly \Vol=30000\ and \Emp\ loudly. \Rst\ Done.
1|\Mrk=1\ One \Mrk=2\ two. \Pau=500\ Three \Spd=60\ slowly \Pit=60\ low.
T
fail=0; n=0; skip=0
for mode in "Sam" "Mary" "Mike" "RoboSoft One" "Mary in Hall" "Mike (for Telephone)"; do
  i=0
  while IFS='|' read -r tagged text; do
    i=$((i + 1))
    slug=$(echo "$mode" | tr 'A-Z ' 'a-z_' | tr -d '()')_$i
    t=""; [ "$tagged" = 1 ] && t="-t"
    SAPI4_HOOKS=unit_phrase,fe_setup,fe_text_input SAPI4_TRACE_PHONES="$OUT/$slug.orig" \
      build/sapi4_speak -d data/msttsl -m "$mode" $t -o /dev/null "$text" > /dev/null 2>&1
    set -- $(sed -n 's/^setup pitch \([0-9]*\) f50 \([0-9-]*\) flags \([0-9a-f]*\).*/\1 \2 \3/p' "$OUT/$slug.orig" | head -1)
    if [ "$2" -lt 0 ] && [ "$tagged" = 1 ]; then skip=$((skip + 1)); continue; fi   # (\Rst needs the voice data)
    printf '%s\n' "$text" | build/port_fe data/msttsl $t -p "$1" -v "$2" "$3" > "$OUT/$slug.port" 2>/dev/null
    n=$((n + 1))
    if cmp -s "$OUT/$slug.orig" "$OUT/$slug.port"; then :; else echo "DIFF $slug"; fail=$((fail + 1)); fi
  done < "$OUT/texts.txt"
done
if [ $fail = 0 ]; then echo "PORT-FE: $n texts, phone lists identical to the original front end ($(cat $OUT/*.port | grep -c '^[^pst]') phones; $skip tagged texts of voice variants skipped)"; else echo "PORT-FE: $fail of $n differ"; exit 1; fi
