#!/usr/bin/env python3
"""Render the original MIDI playlists to OGG Vorbis for the addon folder.

The Android build does not ship FluidSynth.  Rendering the 16 tracks once on
a development machine keeps the APK small and gives the same soundtrack on
every device.  The generated files live in addon/MIDI/ and are also accepted
by the desktop build, which then does not need FluidSynth at runtime.

Usage:
    tools/render_midi.py --abc ../abc-re/abc --addon addon \
        --soundfont /usr/share/soundfonts/FluidR3_GM.sf2
"""
from __future__ import annotations

import argparse
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


def find_soundfont(explicit: str | None) -> Path:
    if explicit:
        path = Path(explicit)
        if not path.is_file():
            sys.exit(f"soundfont not found: {path}")
        return path
    candidates = [
        Path("/usr/share/soundfonts/FluidR3_GM.sf2"),
        Path("/usr/share/soundfonts/SC-55.SoundFont.v1.2b.sf2"),
        Path("/usr/share/soundfonts/freepats-general-midi.sf2"),
        Path("/usr/share/soundfonts/FluidR3_GS.sf2"),
        Path("/usr/share/soundfonts/Roland_SC-88.sf2"),
    ]
    for candidate in candidates:
        if candidate.is_file():
            return candidate
    sys.exit("no soundfont found; pass --soundfont FILE.sf2")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--abc", default="../abc-re/abc", help="extracted CD tree")
    parser.add_argument("--addon", default="addon", help="addon output root")
    parser.add_argument("--soundfont", default=None, help="GM soundfont (.sf2)")
    parser.add_argument("--quality", default="4", help="Vorbis quality (default 4)")
    parser.add_argument("--keep-wav", action="store_true")
    args = parser.parse_args()

    abc = Path(args.abc)
    midi_dir = abc / "MIDI"
    if not midi_dir.is_dir():
        sys.exit(f"MIDI directory not found: {midi_dir}")
    files = sorted(list(midi_dir.glob("*.MID")) + list(midi_dir.glob("*.mid")))
    if not files:
        sys.exit(f"no MIDI files in {midi_dir}")

    soundfont = find_soundfont(args.soundfont)
    out_dir = Path(args.addon) / "MIDI"
    out_dir.mkdir(parents=True, exist_ok=True)

    fluidsynth = shutil.which("fluidsynth")
    ffmpeg = shutil.which("ffmpeg")
    if not fluidsynth or not ffmpeg:
        sys.exit("fluidsynth and ffmpeg are required")

    print(f"soundfont: {soundfont}")
    print(f"tracks: {len(files)} -> {out_dir}")
    with tempfile.TemporaryDirectory(prefix="abc-midi-") as tmp:
        tmpdir = Path(tmp)
        for midi in files:
            wav = tmpdir / (midi.stem + ".wav")
            ogg = out_dir / (midi.stem + ".ogg")
            subprocess.run(
                [fluidsynth, "-ni", "-F", str(wav), "-r", "44100", "-g", "0.7",
                 str(soundfont), str(midi)],
                check=True,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
            subprocess.run(
                [ffmpeg, "-y", "-loglevel", "error", "-i", str(wav),
                 "-c:a", "libvorbis", "-q:a", args.quality, str(ogg)],
                check=True,
            )
            if args.keep_wav:
                shutil.copy2(wav, out_dir / wav.name)
            print(f"  {midi.name} -> {ogg.name} ({ogg.stat().st_size} bytes)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
