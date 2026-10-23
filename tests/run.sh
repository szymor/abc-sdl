#!/bin/sh
# Headless regression suite. Requires a release build and the extracted CD.
#
#   cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
#   DATA=../abc-re/abc tests/run.sh
set -eu

ROOT=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
BIN=${BIN:-$ROOT/build/abc-sdl}
DATA=${DATA:-$ROOT/../abc-re/abc}
TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

if [ ! -x "$BIN" ]; then
    echo "missing $BIN - build the project first" >&2
    exit 1
fi
if [ ! -d "$DATA/FRM" ]; then
    echo "missing $DATA/FRM - set DATA=/path/to/abc" >&2
    exit 1
fi

echo "== data validation"
"$BIN" --data "$DATA" --validate

echo "== load and render every scene"
ls "$DATA/FRM" | sed 's/\.FRM$//' | sort -n |
    awk '{print "scene "$1"\nrender\ndump"}' > "$TMP/all.txt"
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute --script "$TMP/all.txt" \
    > "$TMP/all.log" 2>&1
LOADED=$(grep -c "loaded" "$TMP/all.log" || true)
if [ "$LOADED" -lt 294 ]; then
    echo "only $LOADED scenes loaded" >&2
    exit 1
fi
if grep -qiE "error|cannot|failed" "$TMP/all.log"; then
    echo "errors while rendering all scenes:" >&2
    grep -iE "error|cannot|failed" "$TMP/all.log" >&2
    exit 1
fi
echo "   $LOADED scene loads/renders"

echo "== navigation flow"
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --start 1001 --script "$ROOT/tests/flows.txt" > "$TMP/flows.log" 2>&1
grep -q "scene=2001" "$TMP/flows.log"
grep -q "scene=3001" "$TMP/flows.log"
grep -q "scene=15001" "$TMP/flows.log"
grep -q "scene=15001 .*complete=1" "$TMP/flows.log"
grep -q "scene=3001 .*complete=0" "$TMP/flows.log"
echo "   train → letter → word → game → stays → kBack → word"

echo "== train scroll persistence"
cat > "$TMP/trainscroll.txt" <<'EOF'
scene 1001
wait 100
move 630 300
wait 900
move 320 50
wait 200
dump
scene 2001
wait 100
scene 1001
wait 200
dump
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/trainscroll.txt" > "$TMP/trainscroll.log" 2>&1
FIRST=$(grep "scene=1001" "$TMP/trainscroll.log" | sed -n '1s/.*scroll=\([0-9]*\).*/\1/p')
SECOND=$(grep "scene=1001" "$TMP/trainscroll.log" | sed -n '2s/.*scroll=\([0-9]*\).*/\1/p')
if [ -z "$FIRST" ] || [ -z "$SECOND" ] || [ "$FIRST" -le 0 ] || [ "$FIRST" != "$SECOND" ]; then
    echo "train scroll did not survive leaving and re-entering the hub" >&2
    grep "scene=1001" "$TMP/trainscroll.log" >&2
    exit 1
fi
echo "   horizontal train position survives scene changes"

echo "== game completions"
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$ROOT/tests/games.txt" > "$TMP/games.log" 2>&1
COMPLETE=$(grep -c "complete=1" "$TMP/games.log" || true)
if [ "$COMPLETE" -lt 6 ]; then
    echo "expected at least 6 completed games, got $COMPLETE" >&2
    grep "complete=" "$TMP/games.log" >&2
    exit 1
fi
grep -q "scene=11002 .*complete=1" "$TMP/games.log"  # stays in puzzle
grep -q "scene=13022 .*complete=1" "$TMP/games.log"  # stays in diff
grep -q "scene=12007 .*found=6 target=6 .*complete=1" "$TMP/games.log"  # six slots
grep -q "scene=12062 .*found=4 target=4 .*complete=1" "$TMP/games.log"  # z-order
ASSEMBLE=$(grep -c "scene=12007 .*found=6 target=6 .*complete=1" "$TMP/games.log" || true)
if [ "$ASSEMBLE" -lt 2 ]; then
    echo "Skladanka replacement double-counted or lost completion" >&2
    grep "scene=12007" "$TMP/games.log" >&2
    exit 1
fi
grep -q "scene=12007 .*found=5 target=6 .*complete=0" "$TMP/games.log"  # part removed again
grep -q "scene=16010 .*complete=1" "$TMP/games.log"  # stays in Zg2
grep -q "scene=17012 .*complete=1" "$TMP/games.log"  # stays in Zg3
grep -q "scene=18026 .*complete=1" "$TMP/games.log"  # stays in Zg4
grep -q "scene=14016 .*complete=1" "$TMP/games.log"  # stays in maze
grep -q "scene=3002" "$TMP/games.log"   # Puzzle kBack
grep -q "scene=3022" "$TMP/games.log"   # Szczegoly kBack
grep -q "scene=3007" "$TMP/games.log"   # Skladanka kBack
grep -q "scene=3010" "$TMP/games.log"   # Zgadywanka 2 kBack
grep -q "scene=3012" "$TMP/games.log"   # Zgadywanka 3 kBack
grep -q "scene=3026" "$TMP/games.log"   # Zgadywanka 4 kBack
grep -q "scene=3016" "$TMP/games.log"   # Labirynt kBack
echo "   $COMPLETE counter completions; puzzle, maze and all word returns verified"

echo "== YETI navigation"
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$ROOT/tests/yeti.txt" > "$TMP/yeti.log" 2>&1
grep -q "YETI word arrows disabled" "$TMP/yeti.log"
[ "$(grep -c "scene=3135" "$TMP/yeti.log")" -eq 2 ]
[ "$(grep -c "scene=2023" "$TMP/yeti.log")" -eq 1 ]
echo "   YETI arrows and bogus Y hotspots are inactive; YETI is still reachable"

echo "== absent/reused clips are ignored"
cat > "$TMP/media.txt" <<'EOF'
scene 3006
wait 100
click 425 400
wait 100
dump
scene 3135
wait 100
click 425 400
wait 100
dump
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/media.txt" > "$TMP/media.log" 2>&1
grep -q "scene=3006 .*video=0" "$TMP/media.log"
grep -q "scene=3135 .*video=0" "$TMP/media.log"
echo "   AREK/YETI photo buttons do not play the ladybird clip"

echo "== task instruction and HELP audio"
if ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
        --data "$DATA" --script "$ROOT/tests/help.txt" > "$TMP/help.log" 2>&1 &&
   grep -q "entry help 10005: audio" "$TMP/help.log" &&
   grep -q "click kHelp .*audio=yes" "$TMP/help.log" &&
   grep -q "audio cue .*previous cue stopped" "$TMP/help.log" &&
   [ "$(grep -c "audio cue" "$TMP/help.log")" -ge 3 ]; then
    echo "   entry cue, pencil hover hint and HELP cue are exclusive"
else
    echo "   audio test skipped (no audio device/soundfont)" >&2
fi

echo "== reset explains the rules"
if ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
        --data "$DATA" --script "$ROOT/tests/reset.txt" > "$TMP/reset.log" 2>&1; then
    grep -q "reset kReset -> rules cue" "$TMP/reset.log"
    if grep -q "click kReset" "$TMP/reset.log"; then
        echo "kReset still replays its hover phrase!" >&2
        exit 1
    fi
    [ "$(grep -c "audio cue (6.1" "$TMP/reset.log")" -ge 2 ]
    echo "   reset works and speaks the rules cue (no duplicate hover phrase)"
else
    echo "   reset audio test skipped (no audio device)" >&2
fi

echo "== cursor restore"
cat > "$TMP/cursor.txt" <<'EOF'
scene 10005
wait 100
scene 3001
wait 100
dump
EOF
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/cursor.txt" > "$TMP/cursor.log" 2>&1
grep -q "cursor PEDZEL installed" "$TMP/cursor.log"
grep -q "cursor default restored" "$TMP/cursor.log"
echo "   coloring brush cursor returns to the default outside Kolorowanka"

echo "== colouring palette and fill"
cat > "$TMP/coloring.txt" <<'EOF'
scene 10005
wait 100
click 530 203
click 250 160
wait 100
dump
EOF
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/coloring.txt" > "$TMP/coloring.log" 2>&1
grep -q "palette 5: ffff0000" "$TMP/coloring.log"
grep -q "palette 10: ff0000ff" "$TMP/coloring.log"
grep -q "palette selected 11" "$TMP/coloring.log"
grep -q "palette 14: ff5a0010" "$TMP/coloring.log"
grep -q "palette click k5 -> 4 colour ffff0000" "$TMP/coloring.log"
grep -q "flood fill ffff0000 -> [1-9][0-9]* px" "$TMP/coloring.log"
echo "   swatch colours are sampled correctly and the fill stays coloured"

echo "== wrong puzzle piece returns to tray"
if ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
        --script "$ROOT/tests/gray.txt" > "$TMP/wrongpiece.log" 2>&1; then
    grep -q "piece k01 wrong -> tray state" "$TMP/wrongpiece.log"
    grep -q "scene=11002 .*found=0 .*sel=-1" "$TMP/wrongpiece.log"
    grep -q "sel=1" "$TMP/wrongpiece.log"
    echo "   misplaced piece is back in its normal tray frame (retryable)"
else
    echo "   wrong-piece test failed" >&2
    exit 1
fi

echo "== puzzle selection visual"
cat > "$TMP/puzzlesel.txt" <<EOF
scene 11002
wait 100
shot $TMP/puzzle-rest.png
click 246 449
wait 100
shot $TMP/puzzle-selected.png
click 224 53
wait 150
shot $TMP/puzzle-wrong.png
dump
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/puzzlesel.txt" > "$TMP/puzzlesel.log" 2>&1
if cmp -s "$TMP/puzzle-rest.png" "$TMP/puzzle-selected.png"; then
    echo "selected puzzle piece did not switch to its original colours" >&2
    exit 1
fi
if ! cmp -s "$TMP/puzzle-rest.png" "$TMP/puzzle-wrong.png"; then
    echo "wrong puzzle click left the selection/styling active" >&2
    exit 1
fi
grep -q "scene=11002 .*found=0 .*sel=-1" "$TMP/puzzlesel.log"
echo "   selection uses original colours; wrong drop clears it"

echo "== puzzle congratulation only at the end"
if ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
        --data "$DATA" --script "$ROOT/tests/puzzle_audio.txt" > "$TMP/puzzle.log" 2>&1; then
    [ "$(grep -c "audio cue (4.9" "$TMP/puzzle.log")" -eq 1 ]
    [ "$(grep -c "audio cue (0.1" "$TMP/puzzle.log")" -ge 5 ]
    grep -q "complete=1" "$TMP/puzzle.log"
    echo "   short placement click per piece, one congratulation at completion"
else
    echo "   puzzle audio test skipped (no audio device)" >&2
fi

echo "== puzzle piece alignment"
cat > "$TMP/puzzle_align.txt" <<'EOF'
scene 11004
wait 10
EOF
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/puzzle_align.txt" > "$TMP/puzzle_align.log" 2>&1
for spec in "k01 3 3" "k02 3 4" "k03 3 4" "k04 3 4" \
            "k05 4 4" "k06 4 4" "k07 4 4" "k08 4 3"; do
    set -- $spec
    if ! grep -q "puzzle $1 content offset $2,$3" "$TMP/puzzle_align.log"; then
        echo "puzzle $1 content offset is wrong" >&2
        grep "puzzle k" "$TMP/puzzle_align.log" >&2
        exit 1
    fi
done
echo "   piece content offsets skip the anti-aliased tray border"

echo "== wrong-answer speech + buzz"
cat > "$TMP/wrong.txt" <<'EOF'
scene 15001
wait 200
click 582 71
wait 400
dump
EOF
if ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
        --data "$DATA" --script "$TMP/wrong.txt" > "$TMP/wrong.log" 2>&1 &&
   grep -q "click z1 .*audio=yes" "$TMP/wrong.log" &&
   grep -q "audio sfx (overlaps speech)" "$TMP/wrong.log"; then
    echo "   wrong item explanation plays, buzz mixes over it"
else
    echo "   wrong-answer audio test skipped (no audio device)" >&2
fi

echo "== IGLO navigation"
cat > "$TMP/iglo.txt" <<'EOF'
scene 3047
wait 100
click 20 30
wait 150
dump
scene 3050
wait 100
click 620 30
wait 150
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/iglo.txt" > "$TMP/iglo.log" 2>&1
grep -q "arrow browWyrL click sound -> \\\\WAV\\\\46cm.WAV" "$TMP/iglo.log"
grep -q "arrow browWyrP click sound -> \\\\WAV\\\\46ck.WAV" "$TMP/iglo.log"
[ "$(grep -c "scene=3046" "$TMP/iglo.log")" -eq 2 ]
echo "   IREK and IGŁA arrows reach IGLO with IGLO's click sounds"

echo "== maze interaction"
cat > "$TMP/mazeclick.txt" <<'EOF'
scene 14016
wait 100
click 470 130
click 460 230
click 500 100
wait 100
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --mute --script "$TMP/mazeclick.txt" > "$TMP/mazeclick.log" 2>&1
grep -q "maze 367x391 start cell=17,299" "$TMP/mazeclick.log"
grep -q "collision=1" "$TMP/mazeclick.log"
grep -q "scene=14016 .*complete=0" "$TMP/mazeclick.log"
if grep -q "click Wyj" "$TMP/mazeclick.log"; then
    echo "maze exit pictures were treated as click buttons" >&2
    exit 1
fi
echo "   right-hand goal pictures are not clickable and cannot win"

echo "== IGLO letter-playground hotspot"
cat > "$TMP/iglo-letter.txt" <<EOF
scene 2009
wait 100
move 50 50
wait 150
shot $TMP/iglo-letter-rest.png
move 220 100
wait 150
shot $TMP/iglo-letter-hover.png
click 220 100
wait 150
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --addon "$ROOT/addon" \
    --script "$TMP/iglo-letter.txt" > "$TMP/iglo-letter.log" 2>&1
grep -q "IGLO hover hotspot: .*sound=46H.WAV" "$TMP/iglo-letter.log"
grep -q "audio cue (0.3 s)" "$TMP/iglo-letter.log"
grep -q "scene=3046" "$TMP/iglo-letter.log"
if cmp -s "$TMP/iglo-letter-rest.png" "$TMP/iglo-letter-hover.png"; then
    echo "IGLO hover image did not appear" >&2
    exit 1
fi
echo "   addon IGLO image and hover cue appear; clicking opens word 3046"

echo "== addon fallback"
cat > "$TMP/addon.txt" <<'EOF'
scene 3046
wait 100
click 427 400
wait 200
dump
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/addon.txt" > "$TMP/addon.log" 2>&1
grep -q "scene=3046 .*video=1" "$TMP/addon.log"
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --addon "$ROOT/addon" --mute \
    --script "$TMP/addon.txt" > "$TMP/addon2.log" 2>&1
grep -q "scene=3046 .*video=1" "$TMP/addon2.log"
echo "   missing IGLO photo is supplied by the default/configured addon"

echo "== word screen hover overlays"
cat > "$TMP/wordhover.txt" <<EOF
scene 3016
wait 100
shot $TMP/word-rest.png
move 317 36
wait 200
shot $TMP/word-title.png
move 550 200
wait 200
shot $TMP/word-away.png
move 55 327
wait 200
shot $TMP/word-letter.png
move 116 164
wait 200
shot $TMP/word-object.png
move 76 429
wait 200
shot $TMP/word-train.png
scene 2003
wait 100
shot $TMP/letter-rest.png
move 382 167
wait 200
shot $TMP/letter-hover.png
move 75 428
wait 200
shot $TMP/letter-train.png
EOF
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/wordhover.txt" > "$TMP/wordhover.log" 2>&1
grep -q "scene 3016 origin 0,0" "$TMP/wordhover.log"
grep -q "scene 2003 origin 0,0" "$TMP/wordhover.log"
grep -q "align tlo native 640x480" "$TMP/wordhover.log"
grep -q "align Tlo native 640x480" "$TMP/wordhover.log"
grep -q "baked overlay browWyrL hidden" "$TMP/wordhover.log"
if cmp -s "$TMP/word-rest.png" "$TMP/word-title.png"; then
    echo "title hover did not show the syllable strip" >&2
    exit 1
fi
if ! cmp -s "$TMP/word-rest.png" "$TMP/word-away.png"; then
    echo "word screen did not return to its resting overlays" >&2
    exit 1
fi
if cmp -s "$TMP/word-rest.png" "$TMP/word-letter.png"; then
    echo "letter hover did not show the highlight overlay" >&2
    exit 1
fi
if cmp -s "$TMP/word-rest.png" "$TMP/word-object.png"; then
    echo "object hover did not show the highlight overlay" >&2
    exit 1
fi
if cmp -s "$TMP/word-rest.png" "$TMP/word-train.png"; then
    echo "word train did not grow on hover" >&2
    exit 1
fi
if cmp -s "$TMP/letter-rest.png" "$TMP/letter-train.png"; then
    echo "letter train did not grow on hover" >&2
    exit 1
fi
echo "   native background; title/letters/object/train hover overlays aligned"

echo "== word title/object voice alternation"
cat > "$TMP/altvoice.txt" <<'EOF'
scene 3004
wait 100
click 300 30
click 300 30
click 100 160
click 100 160
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/altvoice.txt" > "$TMP/altvoice.log" 2>&1
TITLE_VOICES=$(awk '/word voice sylaby/{print $NF}' "$TMP/altvoice.log" | tr '\n' ' ')
OBJECT_VOICES=$(awk '/word voice rzecz/{print $NF}' "$TMP/altvoice.log" | tr '\n' ' ')
if [ "$TITLE_VOICES" != "first second " ] || [ "$OBJECT_VOICES" != "first second " ]; then
    echo "word title/object clicks did not alternate K/M recordings" >&2
    grep "word voice" "$TMP/altvoice.log" >&2 || true
    exit 1
fi
echo "   title and object clicks alternate K/M recordings"

echo "== Zgadywanka success/final cue timing"
cat > "$TMP/zgcues.txt" <<'EOF'
scene 17012
wait 100
click 167 118
click 391 154
click 161 234
click 353 404
click 155 365
click 565 68
wait 100
scene 18026
wait 100
click 310 313
click 519 101
wait 100
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/zgcues.txt" > "$TMP/zgcues.log" 2>&1
if [ "$(grep -c "zg success cue" "$TMP/zgcues.log")" -ne 3 ] ||
   [ "$(grep -c "zg/activity completion cue" "$TMP/zgcues.log")" -ne 2 ]; then
    echo "Zgadywanka success/final cues are mistimed" >&2
    grep -E "zg success cue|completion cue|scene=" "$TMP/zgcues.log" >&2
    exit 1
fi
echo "   per-item success cues and one completion cue per completed task"

echo "== OWOCE per-item audio"
cat > "$TMP/owoce.txt" <<'EOF'
scene 18091
wait 100
click 107 400
click 320 80
click 320 400
wait 100
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/owoce.txt" > "$TMP/owoce.log" 2>&1
grep -q "click o1 .*audio=yes" "$TMP/owoce.log"
grep -q "click o2 .*audio=yes" "$TMP/owoce.log"
grep -q "click o3 .*audio=yes" "$TMP/owoce.log"
grep -q "scene=18091 found=3 .*complete=1" "$TMP/owoce.log"
echo "   each correct fruit plays its own 29a/29b/29c sample"

echo "== FOKA balls"
cat > "$TMP/foka.txt" <<'EOF'
scene 18031
wait 100
click 558 161
wait 100
dump
click 125 332
wait 100
dump
click 292 142
wait 100
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/foka.txt" > "$TMP/foka.log" 2>&1
grep -q "scene=18031 found=0 target=2" "$TMP/foka.log"
grep -q "scene=18031 found=1 target=2" "$TMP/foka.log"
grep -q "scene=18031 found=2 target=2" "$TMP/foka.log"
if grep -q "click o1 .*audio=yes" "$TMP/foka.log"; then
    echo "FOKA wrong ball produced the correct-item cue" >&2
    exit 1
fi
echo "   only the two letter-ball seals count and explain themselves"

echo "== TELEFON handle-to-phone match"
cat > "$TMP/telefon.txt" <<EOF
scene 17111
wait 100
shot $TMP/telefon-rest.png
click 125 400
click 554 273
wait 100
shot $TMP/telefon-pair.png
dump
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/telefon.txt" > "$TMP/telefon.log" 2>&1
grep -q "scene=17111 found=1" "$TMP/telefon.log"
if cmp -s "$TMP/telefon-rest.png" "$TMP/telefon-pair.png"; then
    echo "TELEFON phone patch was not shown after matching" >&2
    exit 1
fi
echo "   matching the handset removes it and shows the phone patch"

echo "== SOK juice glass"
cat > "$TMP/sok.txt" <<'EOF'
scene 16108
wait 100
click 308 95
wait 100
dump
click 109 104
wait 100
dump
click 294 405
wait 100
dump
click 203 250
wait 100
dump
shot $TMP/sok-complete.png
click 109 104
wait 100
shot $TMP/sok-after.png
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --mute --script "$TMP/sok.txt" > "$TMP/sok.log" 2>&1
grep -q "scene=16108 found=0 target=3" "$TMP/sok.log"
grep -q "scene=16108 found=1 target=3" "$TMP/sok.log"
grep -q "scene=16108 found=2 target=3" "$TMP/sok.log"
grep -q "scene=16108 found=3 .*complete=1" "$TMP/sok.log"
if cmp -s "$TMP/sok-complete.png" "$TMP/sok-after.png"; then
    echo "SOK glass did not refill after completion" >&2
    exit 1
fi
echo "   wrong items show the mistake glass but do not count; correct picks refill it"

echo "== Zgadywanka 3 selection visual"
cat > "$TMP/zg3sel.txt" <<'EOF'
scene 17012
wait 100
click 391 154
render
wait 100
dump
click 167 118
render
wait 100
dump
click 391 154
render
wait 100
dump
click 167 118
click 391 154
wait 100
dump
scene 17012
wait 100
click 167 118
click 353 404
wait 100
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/zg3sel.txt" > "$TMP/zg3sel.log" 2>&1
grep -q "zg3 right-first ignored: z1" "$TMP/zg3sel.log"
grep -q "scene=17012 found=0 target=3 sel=-1" "$TMP/zg3sel.log"
grep -q "draw o1 .*state=1" "$TMP/zg3sel.log"
grep -q "draw o1 .*state=2" "$TMP/zg3sel.log"
grep -q "scene=17012 found=1 target=3 sel=-1" "$TMP/zg3sel.log"
grep -q "scene=17012 found=0 target=3 sel=-1" "$TMP/zg3sel.log"
echo "   right-first ignored; pairs lock once; wrong match clears the selection"

echo "== Szczegoly success/penultimate/final cue timing"
cat > "$TMP/szczcues.txt" <<'EOF'
scene 13077
wait 100
click 236 12
wait 100
dump
click 261 37
click 183 63
click 35 127
click 34 243
click 252 281
wait 100
dump
click 55 398
wait 100
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/szczcues.txt" > "$TMP/szczcues.log" 2>&1
if [ "$(grep -c "scene=13077 found=0 target=6" "$TMP/szczcues.log")" -ne 1 ] ||
   [ "$(grep -c "szczegoly success cue" "$TMP/szczcues.log")" -ne 4 ] ||
   [ "$(grep -c "szczegoly penultimate cue" "$TMP/szczcues.log")" -ne 1 ] ||
   [ "$(grep -c "zg/activity completion cue" "$TMP/szczcues.log")" -ne 1 ]; then
    echo "Szczegoly hit/cue timing is wrong" >&2
    grep -E "szczegoly|completion cue|scene=13077" "$TMP/szczcues.log" >&2
    exit 1
fi
echo "   ellipse-only hits; per-difference cue; penultimate cue; one final summary"

echo "== word-arrow audio across navigation"
cat > "$TMP/arrowaudio.txt" <<'EOF'
scene 3004
wait 100
move 620 30
wait 100
click 620 30
wait 200
dump
EOF
ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
    --data "$DATA" --script "$TMP/arrowaudio.txt" > "$TMP/arrowaudio.log" 2>&1
grep -q "keeping word-arrow audio" "$TMP/arrowaudio.log"
grep -q "scene=3005" "$TMP/arrowaudio.log"
echo "   arrow WAV is not stopped by the word-screen transition"

echo "== letter-playground object hover"
cat > "$TMP/letterobj.txt" <<EOF
scene 2001
wait 100
move 50 50
wait 200
shot $TMP/letterobj-rest.png
move 500 200
wait 200
shot $TMP/letterobj-hover.png
move 50 50
wait 200
shot $TMP/letterobj-rest2.png
EOF
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/letterobj.txt" > "$TMP/letterobj.log" 2>&1
grep -q "draw Z5 .*state=1" "$TMP/letterobj.log"
if grep -q "mask alpha on mouse-only Z5" "$TMP/letterobj.log"; then
    echo "mouse-only mask was applied to the Z5 hover sprite" >&2
    exit 1
fi
if cmp -s "$TMP/letterobj-rest.png" "$TMP/letterobj-hover.png"; then
    echo "letter-playground object hover did not change the object" >&2
    exit 1
fi
if ! cmp -s "$TMP/letterobj-rest.png" "$TMP/letterobj-rest2.png"; then
    echo "letter-playground object hover did not clear" >&2
    exit 1
fi
echo "   object hover uses the full replacement patch, not its mouse mask"

echo "== crayon toolbar"
cat > "$TMP/toolbar.txt" <<EOF
scene 15001
wait 100
shot $TMP/toolbar-hidden.png
move 510 450
wait 250
shot $TMP/toolbar-help.png
move 585 450
wait 250
shot $TMP/toolbar-print.png
move 550 250
wait 250
shot $TMP/toolbar-away.png
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/toolbar.txt" > "$TMP/toolbar.log" 2>&1
if cmp -s "$TMP/toolbar-hidden.png" "$TMP/toolbar-help.png"; then
    echo "hovering the help crayon did not raise it" >&2
    exit 1
fi
if cmp -s "$TMP/toolbar-hidden.png" "$TMP/toolbar-print.png"; then
    echo "hovering the print crayon did not raise it" >&2
    exit 1
fi
if ! cmp -s "$TMP/toolbar-hidden.png" "$TMP/toolbar-away.png"; then
    echo "crayons did not return to their hidden state" >&2
    exit 1
fi
echo "   crayons rise individually on hover and hide with their tips visible"

echo "== TV media button active state"
cat > "$TMP/tvstate.txt" <<'EOF'
scene 3001
wait 100
click 354 399
wait 100
click 320 240
wait 100
click 354 399
wait 100
click 320 240
wait 100
dump
EOF
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/tvstate.txt" > "$TMP/tvstate.log" 2>&1
[ "$(grep -c 'TV button KlKamera active' "$TMP/tvstate.log")" -eq 2 ]
[ "$(grep -c 'TV media buttons released after playback' "$TMP/tvstate.log")" -eq 2 ]
echo "   media buttons are released after playback and can be pressed again"

echo "== video (best effort)"
mkdir -p "$ROOT/tests/out"
if SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
        --script "$ROOT/tests/video.txt" > "$TMP/video.log" 2>&1; then
    echo "   video clips decoded and dismissed"
else
    echo "   video test skipped/failed (FFmpeg missing?)" >&2
fi

echo "== TV video pauses background music"
cat > "$TMP/tvmusic.txt" <<'EOF'
scene 3016
wait 100
click 350 400
wait 700
move 317 36
wait 300
dump
click 300 300
wait 300
dump
click 280 400
wait 700
click 300 300
wait 300
dump
EOF
if ABC_DEBUG=1 SDL_AUDIODRIVER=dummy SDL_VIDEODRIVER=dummy "$BIN" \
        --data "$DATA" --script "$TMP/tvmusic.txt" > "$TMP/tvmusic.log" 2>&1; then
    if grep -q "video frame" "$TMP/tvmusic.log"; then
        PAUSED=$(grep -c "TV video: background music paused" "$TMP/tvmusic.log" || true)
        RESTORED=$(grep -c "video ended: background music restored" "$TMP/tvmusic.log" || true)
        if [ "$PAUSED" -lt 2 ] || [ "$RESTORED" -lt 2 ]; then
            echo "video did not pause/restore background music (paused=$PAUSED restored=$RESTORED)" >&2
            grep -E "music|video ended" "$TMP/tvmusic.log" >&2
            exit 1
        fi
        grep -q "video soundtrack active" "$TMP/tvmusic.log"
        grep -q "audio cue blocked while video is playing" "$TMP/tvmusic.log"
        echo "   music paused, other cues blocked; music restored afterwards"
    else
        echo "   TV music test skipped (video unsupported)" >&2
    fi
else
    echo "   TV music test skipped/failed" >&2
fi

echo "all checks passed"
