#!/usr/bin/env python3
"""Generate Android launcher icons from the original ABC train icon.

The source is the 32x32, 16-colour MAINICON resource from the game's
ABC.EXE, extracted with wrestool/icotool and saved as
android/icon/ic_launcher_32.png.  Legacy launcher icons keep its white
background; the adaptive foreground removes that background so the icon
sits on the white adaptive background without a visible white square.

Run from anywhere:

    python3 tools/make_android_icon.py
"""

from collections import deque
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SOURCE = ROOT / "android/icon/ic_launcher_32.png"
RES = ROOT / "android/app/src/main/res"

LEGACY_SIZES = {
    "mdpi": 48,
    "hdpi": 72,
    "xhdpi": 96,
    "xxhdpi": 144,
    "xxxhdpi": 192,
}

# Adaptive icons are 108x108 dp; keep the artwork inside the circular safe
# zone (about 66 dp) so launcher masks cannot clip the train.
ADAPTIVE_SIZES = {
    "mdpi": 108,
    "hdpi": 162,
    "xhdpi": 216,
    "xxhdpi": 324,
    "xxxhdpi": 432,
}
SAFE_CONTENT = 66 / 108


def nearest(image: Image.Image, size: int) -> Image.Image:
    return image.resize((size, size), Image.Resampling.NEAREST)


def transparent_background(image: Image.Image) -> Image.Image:
    """Remove only white pixels connected to the image border."""
    out = image.copy()
    px = out.load()
    w, h = out.size
    queue = deque()

    def is_white(x: int, y: int) -> bool:
        return px[x, y][:3] == (255, 255, 255) and px[x, y][3] != 0

    for x in range(w):
        for y in (0, h - 1):
            if is_white(x, y):
                queue.append((x, y))
    for y in range(h):
        for x in (0, w - 1):
            if is_white(x, y):
                queue.append((x, y))

    while queue:
        x, y = queue.popleft()
        if not (0 <= x < w and 0 <= y < h) or not is_white(x, y):
            continue
        px[x, y] = (255, 255, 255, 0)
        queue.extend(((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)))

    return out


def main() -> None:
    src = Image.open(SOURCE).convert("RGBA")
    if src.size != (32, 32):
        raise SystemExit(f"expected a 32x32 source icon, got {src.size}")

    for density, size in LEGACY_SIZES.items():
        outdir = RES / f"mipmap-{density}"
        outdir.mkdir(parents=True, exist_ok=True)
        nearest(src, size).save(outdir / "ic_launcher.png")

    foreground = transparent_background(src)
    for density, canvas_size in ADAPTIVE_SIZES.items():
        outdir = RES / f"mipmap-{density}"
        outdir.mkdir(parents=True, exist_ok=True)
        content_size = round(canvas_size * SAFE_CONTENT)
        train = nearest(foreground, content_size)
        canvas = Image.new("RGBA", (canvas_size, canvas_size), (0, 0, 0, 0))
        offset = (canvas_size - content_size) // 2
        canvas.alpha_composite(train, (offset, offset))
        canvas.save(outdir / "ic_launcher_foreground.png")

    print(f"wrote launcher icons from {SOURCE.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
