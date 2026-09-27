#!/bin/sh
# Coverage of the ORIGINAL engine over the golden corpus (+ tagged text): per-instruction execution
# counts and call targets, merged by tools/covreport.py into work/coverage.json and work/coverage.txt.
#   tools/cover.sh
set -u
cd "$(dirname "$0")/.."
make -s build/sapi4_cover || exit 1
BIN=build/sapi4_cover
D=work/cover
rm -rf "$D" && mkdir -p "$D"
export SAPI4_COVER_DIR=$D SAPI4_HOOKS=none
T1="The quick brown fox jumps over the lazy dog, and then it runs away into the forest."
T2="On March 3rd, 2021, I paid \$45.99 for 3 items at 10:30 AM; call 555-1234. Dr. Smith's e-mail isn't working!"
T3='Hello \Pit=150\ there, \Spd=250\ quickly \Vol=30000\ and \Emp\ loudly. \Chr=Whisper\ Psst. \Rst\ Done.'
"$BIN" -d data/msttsl -l | sed -E 's/ +speaker=.*//' > "$D/modes.txt"
while read -r mode; do
  "$BIN" -d data/msttsl -m "$mode" -o /dev/null "$T1" 2>/dev/null
  "$BIN" -d data/msttsl -m "$mode" -o /dev/null "$T2" 2>/dev/null
done < "$D/modes.txt"
for mode in "Sam" "Mary in Hall" "Male Whisper" "Mike (for Telephone)"; do
  "$BIN" -d data/msttsl -m "$mode" -s 300 -p 140 -o /dev/null "$T1" 2>/dev/null
  "$BIN" -d data/msttsl -m "$mode" -s 60 -p 60 -o /dev/null "$T1" 2>/dev/null
  "$BIN" -d data/msttsl -m "$mode" -t -o /dev/null "$T3" 2>/dev/null
done
# the wider hookdiff texts (spelling, abbreviations, numbers, SAPI tags, a long sentence) on five modes
T4="Supercalifragilisticexpialidocious! Antidisestablishmentarianism? Pneumonoultramicroscopicsilicovolcanoconiosis."
T5="NASA, the FBI and IBM met at 1600 Pennsylvania Ave. NW, Washington DC 20500, on 12/25/1999 at 11:59:59 PM."
T6="A b c d e f g h i j k l m n o p q r s t u v w x y z. 0 1 2 3 4 5 6 7 8 9 10 100 1000 1000000 3.14159 -42 1/2 50% #1"
T7='\Mrk=1\ One \Mrk=2\ two. \Pau=500\ Three \Prn=h eh l ow\ four \Spd=60\ slowly \Pit=60\ low.'
T8="It was the best of times, it was the worst of times, it was the age of wisdom, it was the age of foolishness, it was the epoch of belief."
for mode in "Sam" "Mary" "Mike" "Female Whisper" "Mary (for Telephone)"; do
  for t in "$T4" "$T5" "$T6" "$T8"; do "$BIN" -d data/msttsl -m "$mode" -r 2 -o /dev/null "$t" 2>/dev/null; done
  "$BIN" -d data/msttsl -m "$mode" -t -o /dev/null "$T7" 2>/dev/null
done
${PYTHON:-/usr/bin/python3} tools/covreport.py data/msttsl/msttssyn.dll work
