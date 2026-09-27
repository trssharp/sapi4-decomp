#!/bin/sh
# The user lexicon file (<engine dir>\Lex\User.lex; the emulator's user is "User"): several files, good
# and broken, each in a copy of the voice directory (symlinks), rendered by the original
# (SAPI4_HOOKS=none), by the hook build (PCM and final heap compared) and by the portable renderer.
set -u
cd "$(dirname "$0")/.."
OUT=work/userlex
rm -rf "$OUT" && mkdir -p "$OUT"
H='[MSTTSUSERLEXVER100]'
lex() {  # name, then file content (printf format)
  mkdir -p "$OUT/$1/Lex"
  for f in data/msttsl/*; do ln -s "$PWD/$f" "$OUT/$1/"; done
  printf "$2" > "$OUT/$1/Lex/User.lex"
}
lex good "$H\r\nblorp\tB L AO1 R P \r\nflimsy\tF L IH1 M Z IY0 \r\nzorgle\tZ AO1 R G AH0 L \r\n"
lex caps "$H\r\nAardvark\tAA1 R D V AA2 R K \r\nNASA\tN AE1 S AH0 \r\nzed\tZ EH1 D \r\n"
lex unsorted "$H\r\nzorgle\tZ AO1 R G AH0 L \r\nblorp\tB L AO1 R P \r\n"
lex badword "$H\r\nblorp\tB L AO1 R P \r\nzo9gle\tZ AO1 R G AH0 L \r\n"
lex badpron "$H\r\nblorp\tB L AO1 R P \r\nzorgle\tZ QQ R G \r\n"
lex nocrlf "$H\r\nblorp\tB L AO1 R P \nzorgle\tZ AO1 R G AH0 L \r\n"
lex notab "$H\r\nblorp B L AO1 R P \r\n"
lex header "[MSTTSUSERLEXVER099]\r\nblorp\tB L AO1 R P \r\n"
lex empty "$H\r\n"
lex trailing "$H\r\nblorp\tB L AO1 R P \r\nextra"
lex apos "$H\r\no'blorp\tOW1 B L AO1 R P \r\nx-ray\tEH1 K S R EY2 \r\n"
TEXT="The blorp, a flimsy zorgle, met an aardvark from NASA. O'blorp? X-ray zed!"
n=0; fail=0
for d in "$OUT"/*/; do
  name=$(basename "$d")
  for mode in "Sam" "Mary in Hall"; do
    slug="${name}_$(echo "$mode" | tr 'A-Z ' 'a-z_')"
    for h in none all; do
      SAPI4_HOOKS=$h SAPI4_HOOK_STATS=1 build/sapi4_speak -d "$d" -m "$mode" -o "$OUT/$slug.$h.wav" "$TEXT" 2>&1 >/dev/null | grep -E "^guest heap|WARNING" > "$OUT/$slug.$h.heap"
    done
    build/port_render "$d" -m "$mode" -o "$OUT/$slug.port.wav" "$TEXT" > /dev/null 2>&1
    n=$((n + 1))
    a=$(python3 tools/pcmhash.py "$OUT/$slug.none.wav" | cut -d' ' -f1,2)
    b=$(python3 tools/pcmhash.py "$OUT/$slug.all.wav" | cut -d' ' -f1,2)
    c=$(python3 tools/pcmhash.py "$OUT/$slug.port.wav" 2>/dev/null | cut -d' ' -f1,2)
    if [ "$a" != "$b" ] || [ "$a" != "$c" ] || ! cmp -s "$OUT/$slug.none.heap" "$OUT/$slug.all.heap"; then
      echo "DIFF $slug: original $a, hooks $b, portable $c"; fail=$((fail + 1))
    fi
  done
done
plain=$(python3 tools/pcmhash.py "$OUT/empty_sam.none.wav" | cut -d' ' -f1); used=$(python3 tools/pcmhash.py "$OUT/good_sam.none.wav" | cut -d' ' -f1)
[ "$plain" = "$used" ] && { echo "USERLEX: the good lexicon changed nothing - the test does not exercise it"; exit 1; }
if [ $fail = 0 ]; then echo "USERLEX: $n renders over $(ls -d "$OUT"/*/ | wc -l | tr -d ' ') lexicon files identical: original, hook build (PCM and heap), portable"; else echo "USERLEX: $fail of $n differ"; exit 1; fi
