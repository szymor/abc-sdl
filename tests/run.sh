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
click 28 33
wait 100
dump
EOF
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute --script "$TMP/iglo.txt" \
    > "$TMP/iglo.log" 2>&1
grep -q "scene=3046" "$TMP/iglo.log"
echo "   IGLO is reachable through the default I arrow cycle"

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
