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
${PYTHON:-/usr/bin/python3} tools/covreport.py data/msttsl/msttssyn.dll work
