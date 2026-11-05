# Android port

The Android build is a normal SDL3 app with a small Java content-import
front-end.  **The APK contains no game data.**  On first launch the user
selects the `abc/` folder extracted from their own CD; the app copies it into
private app storage and the native engine then reads ordinary filesystem
paths.

The add-on data (`addon/`: IGLO hover artwork/WAV, the IGLO photo, and the
pre-rendered MIDI OGGs) is embedded in the APK.  The app refreshes its private
copy every time the game starts; there is no add-on folder picker.

---

## Requirements

* Android SDK 36, build-tools 36
* Android NDK **28.2.13676358** (r28c)
* JDK 17
* CMake 3.22.1 (installed in the SDK)
* Gradle wrapper 8.12 (already in `android/`)
* SDL3 3.4.18, SDL3_image 3.4.8, SDL3_mixer 3.2.4 Android AARs

The AARs are downloaded by:

```bash
tools/fetch_android_deps.sh
```

They are placed in `android/app/libs/` and are intentionally not committed.

## FFmpeg

The Android video path needs a small decode-only FFmpeg.  The original 210 AVI
clips use only `cinepak`, `msvideo1`, `indeo3`, and PCM `s16le`/`u8` audio.
Build it once for the ABIs you need:

```bash
NDK=$ANDROID_HOME/ndk/28.2.13676358 tools/ndk-build-ffmpeg.sh
# or a single ABI:
NDK=$ANDROID_HOME/ndk/28.2.13676358 tools/ndk-build-ffmpeg.sh arm64-v8a
```

The script downloads FFmpeg 7.1.5 if necessary and writes static libraries to
`third_party/ffmpeg/<abi>/`.  If those libraries are absent, CMake simply
builds without video support and the existing placeholder card is used.

The build is LGPL-2.1-or-later (no GPL/non-free components, no encoders).

## Build

```bash
tools/fetch_android_deps.sh
NDK=$ANDROID_HOME/ndk/28.2.13676358 tools/ndk-build-ffmpeg.sh
cd android
./gradlew assembleDebug
# or
./gradlew assembleRelease
```

Install:

```bash
adb install -r app/build/outputs/apk/debug/app-debug.apk
```

The APK is signed with the debug key for both debug and release builds.  Replace
the signing configuration before publishing.

## Launcher icon

The launcher icon is the train from the original `ABC.EXE` (`MAINICON`,
32×32, 16 colours), saved as `android/icon/ic_launcher_32.png`.  Legacy
density-specific icons and the adaptive foreground are generated from it with
nearest-neighbour scaling:

```bash
python3 tools/make_android_icon.py
```

The adaptive icon uses a white background (`res/values/colors.xml`); the
foreground removes only the border-connected white of the source icon so the
train remains crisp inside Android's circular safe zone.

## First run: importing the CD data

The launcher activity is `SetupActivity`.

1. Choose **Wybierz folder danych** and select the folder that contains
   `FRM/`, `WAV/`, `AVI/` and `MIDI/` (it may also be a parent folder that
   contains `abc/`).
2. The app validates the selection and shows counts/size.
3. Choose **Importuj**.  The contents are copied to:

   ```text
   /sdcard/Android/data/pl.szymor.abc/files/abc/
   ```

4. Press **Graj**.  The embedded add-on is installed automatically.

The copy is staged in `abc.importing/` and swapped into place only after it
passes validation.  An interrupted import is cleaned up on the next launch.
The app needs roughly 650 MB of free space during import (the source and the
copy coexist temporarily).

### Developer shortcut

For fast iteration you can skip the file picker and push content directly:

```bash
adb push docs/abc-re/abc /sdcard/Android/data/pl.szymor.abc/files/abc
adb shell am force-stop pl.szymor.abc
adb shell am start -n pl.szymor.abc/.SetupActivity
```

On some emulator images, shell-created directories need:

```bash
adb shell chmod -R 777 /sdcard/Android/data/pl.szymor.abc/files/abc
```

### Debug start scene

`SetupActivity` accepts an optional `start` extra and forwards it to the game:

```bash
adb shell am start -n pl.szymor.abc/.SetupActivity --es start 11002
```

Useful scene IDs: `1001` train, `2001` letter A, `3016` CYMBALKI word,
`11002` puzzle, `13022` spot-the-difference, `14016` maze.

## MIDI music

Android does not build FluidSynth.  `tools/render_midi.py` renders the 16
original MIDI files to `addon/MIDI/*.ogg` using FluidSynth on the development
machine:

```bash
python3 tools/render_midi.py \
  --abc docs/abc-re/abc \
  --addon addon \
  --soundfont /usr/share/soundfonts/FluidR3_GM.sf2
```

The game prefers `addon/MIDI/<name>.ogg` over `abc/MIDI/<name>.MID`.  If the
OGGs are missing, Android runs without background music; speech and SFX still
work.  Desktop still uses FluidSynth when the playlists point at `.mid` files.

## UI and touch controls

* **Landscape**, 4:3 stage, letterboxed and safe-area aware.
* There is no persistent Android-only HUD or side rail; the game uses the
  whole stage, like the original and the desktop port.
* The original pencil toolbar is always raised in touch mode.
* **Tap** = click.  **Press** = hover (word-screen syllables/highlights).
* **Swipe/drag the train** to scroll; tapping a wagon opens it.
* **Letter playground:** first tap reveals a hidden object; second tap opens
  its word.
* **Puzzle:** tap a piece, then tap its slot; dragging also works.
* **Maze:** tap or drag to set a destination; the letter walks there and the
  target is marked with a ring.
* **Colouring palette / spot-the-difference:** nearest-hit touch logic with a
  small slop, because the original targets are small.
* Android Back/gesture: closes overlays, then goes back, then requires a second
  back within 2 seconds to exit.
* Backgrounding pauses music and the game; returning to the foreground resumes
  automatically, with no pause panel.
* Exported pictures (`kPrint`) are saved to the Pictures collection
  (`Pictures/Moje Pierwsze ABC`) on Android 10+.

## Content and licensing

The original CD data is © 1996 OPTIMUS NEXUS / “WiT & Luk”.  It is not part of
the APK and must be supplied by the user from their own disc.  Do not publish
a build that bundles `abc/` unless you have the necessary rights.  The bundled
`addon/` is small, is embedded in the APK, and supplies the IGLO material and
the rendered MIDI tracks; review its distribution separately if you publish
the app.

## Known limitations

* The content folder must be already extracted (`abc/`); the app does not read
  `ABC.mdf` directly.
* Android 9 and older cannot save exported images to the public Pictures
  collection without additional storage permission; they are written to the
  app's external Pictures directory instead.
* Activity state is not serialized.  If Android kills the process during a
  game, the app starts again at the splash/train; completed data is not lost
  because no progress is stored.
* There is no in-game content manager after the first run.  To replace the
  imported CD data, clear the app's storage and import it again.
