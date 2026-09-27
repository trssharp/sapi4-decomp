#!/bin/sh
# Tagged text through the portable renderer against the untouched original in the emulator
# (SAPI4_HOOKS=none): every SAPI 4 tag the engine knows, well and badly formed, over several modes.
set -u
cd "$(dirname "$0")/.."
BIN=${1:-build/port_render}
OUT=work/port_tags
rm -rf "$OUT" && mkdir -p "$OUT"
cat > "$OUT/texts.txt" <<'T'
Hello \Pit=150\ there, \Spd=250\ quickly \Vol=30000\ and \Emp\ loudly. \Rst\ Done.
\Mrk=1\ One \Mrk=2\ two. \Pau=500\ Three \Spd=60\ slowly \Pit=60\ low.
\Chr="Whisper"\ A quiet word, \Chr="Normal"\ and a normal one.
\Chr="Monotone"\ Flat speech here. \Chr="Excited"\ Now this is exciting!
\RSpd=150\ Relative speed, \RPit=80\ relative pitch, \RSpd=100\ \RPit=100\ back.
\Prn=hello=hh ah l ow\ Say hello. \Pro=0\ No prosody, \Pro=1\ prosody again.
\Ctx="Address"\ 1 Microsoft Way \Ctx="E-mail"\ bob@example.com \Ctx="Unknown"\ done.
\Com=this is a comment\ Words \Eng;GUID:1\ after. \Dem\ Deemphasized. \Prt="N"\ noun.
\Vce=Speaker="Mike"\ Voice change request. \Vol=65535\ Loud. \Vol=10000\ Quiet.
\Pau=0\ No pause\Pau=2000\ long pause. \RmS=1\ Read mode on, \RmS=0\ off.
Broken \Pit=\ tags \Spd=abc\ and \Xyz=1\ unknown \Mrk\ ones \ here.
\Chr="Excited"\ The quick brown fox jumps over the lazy dog! Is it? Yes; on March 3rd, 2021, I paid $45.99.
\Chr="Excited"\ \Spd=250\ Hello \Pit=150\ there, \Chr="Normal"\ and back \Chr="Excited"\ again - wow.
\Spd=300\ \Pit=200\ Fast and high, \Spd=30\ \Pit=30\ then slow and low.
T
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
