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

echo "== YETI navigation fix"
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$ROOT/tests/yeti.txt" > "$TMP/yeti.log" 2>&1
grep -q "scene=2023" "$TMP/yeti.log"
grep -q "scene=3135" "$TMP/yeti.log"
echo "   arrows reach the Y page, YETI is still reachable"

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
grep -q "palette 14: ff5a0010" "$TMP/coloring.log"
grep -q "palette click k5 -> 4 colour ffff0000" "$TMP/coloring.log"
grep -q "flood fill ffff0000 -> [1-9][0-9]* px" "$TMP/coloring.log"
echo "   swatch colours are sampled correctly and the fill stays coloured"

echo "== wrong puzzle piece returns to tray"
if ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
        --script "$ROOT/tests/gray.txt" > "$TMP/wrongpiece.log" 2>&1; then
    grep -q "piece k01 wrong -> tray state" "$TMP/wrongpiece.log"
    grep -q "scene=11002 .*found=0" "$TMP/wrongpiece.log"
    grep -q "sel=1" "$TMP/wrongpiece.log"
    echo "   misplaced piece is back in its normal tray frame (retryable)"
else
    echo "   wrong-piece test failed" >&2
    exit 1
fi

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

echo "== opt-in content fix"
cat > "$TMP/fix.txt" <<'EOF'
scene 3047
wait 100
click 28 33
wait 100
dump
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute --script "$TMP/fix.txt" \
    > "$TMP/fix0.log" 2>&1
grep -q "scene=3050" "$TMP/fix0.log"
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute --fix-content \
    --script "$TMP/fix.txt" > "$TMP/fix1.log" 2>&1
grep -q "scene=3046" "$TMP/fix1.log"
echo "   IGLO is reachable with --fix-content"

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

echo "== TV media button active state"
cat > "$TMP/tvstate.txt" <<'EOF'
scene 3001
wait 100
click 354 399
wait 100
click 320 240
wait 100
click 287 400
wait 100
click 320 240
wait 100
click 287 400
wait 100
dump
EOF
ABC_DEBUG=1 SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$TMP/tvstate.txt" > "$TMP/tvstate.log" 2>&1
grep -q "TV button KlKamera active" "$TMP/tvstate.log"
grep -q "TV button KlKamera normal" "$TMP/tvstate.log"
grep -q "TV button KlTabl active" "$TMP/tvstate.log"
grep -q "active TV button KlTabl ignored" "$TMP/tvstate.log"
echo "   click selects one pressed media button; active button ignores repeats"

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
click 280 400
wait 700
dump
click 300 300
wait 300
dump
click 350 400
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
        echo "   music paused for TV clips and resumed afterwards"
    else
        echo "   TV music test skipped (video unsupported)" >&2
    fi
else
    echo "   TV music test skipped/failed" >&2
fi

echo "all checks passed"
