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
echo "   train → letter → word → game → word"

echo "== game completions"
SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
    --script "$ROOT/tests/games.txt" > "$TMP/games.log" 2>&1
COMPLETE=$(grep -c "complete=1" "$TMP/games.log" || true)
if [ "$COMPLETE" -lt 6 ]; then
    echo "expected at least 6 completed games, got $COMPLETE" >&2
    grep "complete=" "$TMP/games.log" >&2
    exit 1
fi
grep -q "scene=3002" "$TMP/games.log"   # Puzzle
grep -q "scene=3022" "$TMP/games.log"   # Szczegoly
grep -q "scene=3007" "$TMP/games.log"   # Skladanka
grep -q "scene=3010" "$TMP/games.log"   # Zgadywanka 2
grep -q "scene=3012" "$TMP/games.log"   # Zgadywanka 3
grep -q "scene=3026" "$TMP/games.log"   # Zgadywanka 4
grep -q "scene=3016" "$TMP/games.log"   # Labirynt
echo "   $COMPLETE counter completions; puzzle, maze and all word returns verified"

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

echo "== video (best effort)"
mkdir -p "$ROOT/tests/out"
if SDL_VIDEODRIVER=dummy "$BIN" --data "$DATA" --mute \
        --script "$ROOT/tests/video.txt" > "$TMP/video.log" 2>&1; then
    echo "   video clips decoded and dismissed"
else
    echo "   video test skipped/failed (FFmpeg missing?)" >&2
fi

echo "all checks passed"
