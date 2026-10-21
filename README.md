# Moje Pierwsze ABC — SDL3 re-implementation

A from-scratch C11/SDL3 port of the 1996 Polish children's CD-ROM
**Moje Pierwsze ABC** (“My First ABC”, OPTIMUS NEXUS / “WiT & Luk”).

The original is a 16-bit Windows 3.1 / Delphi 1 application running at
640×480 in 256 colours. This port is **data-driven**: it reads the original
`FRM` scene files, embedded bitmaps and WAVs, and the `WAV/`, `MIDI/` and
`AVI/` directories directly, and interprets the same component model at
runtime. There is no per-scene code.

The reverse-engineering specification this port is based on lives in
[`abc-re/docs`](../abc-re/docs).

---

## Status

| area | state |
|------|-------|
| DFM/FRM parser | complete — all **294 scenes / 6330 component streams** parse with 0 errors |
| Scene manager | complete — ID-driven navigation, 640×480 logical canvas, letterboxed scaling |
| Splash (Winietka) | complete — 4 s timer, click to skip |
| Train hub (Ciuchcia) | complete — 24 calibrated wagon hitboxes, edge scrolling, persistent scroll position, mouse wheel, exit, credits video |
| Letter playground (Literka) | complete — hover-reveal objects, all 24 letters, touch-friendly reveal-first click |
| Word screen (Telewizorek) | complete — titles, sprites, word arrows, letter buttons, train icon, MIDI |
| Colouring book (Kolorowanka) | complete — 14-colour palette with normal/selected swatch shapes, centre sampling, line-mask flood fill, reset, PNG export |
| Puzzle (Puzzle) | complete — drag or click-to-place, index pairing, completion |
| Assemble (Skladanka) | complete — one-click part placement into `_MapHS` slots, map-relative images, alternative designs, completion |
| Spot the difference (Szczegoly) | complete — paired differences, highlights, completion |
| Maze (Labirynt) | complete — 1-bpp collision map, cursor-following player, good/bad exits |
| Zgadywanka 1–4 | complete — all 35 scenes, counters and feedback |
| Audio | WAV one-shots + spoken task cues (SDL3_mixer), shuffled MIDI playlists (FluidSynth); one spoken cue at a time, while short SFX such as the wrong-answer buzz may mix over speech |
| Video (AVI) | complete — Cinepak/MSVideo1/Indeo3 decoded by FFmpeg into the `TEkran` rect, with audio |
| Cursors (`PEDZEL`, `LIT_*`) | brush cursor extracted from `ABC.EXE`; the maze draws the word's letter as a sprite (the `LIT_*` resources are available in `src/cursors_data.h`) |
| Printing | exports a PNG instead of GDI printing |

Every scene was loaded and drawn in a headless pass (`--script` over all 294
IDs) without errors. Every game module was driven to completion by scripted
clicks and returned to the expected word scene.

---

## Build

Requirements:

* C11 compiler, CMake ≥ 3.20
* SDL3, SDL3_image, SDL3_mixer
* FFmpeg development libraries (libavformat/avcodec/swscale/swresample) for AVI
* FluidSynth (optional, for MIDI music) + a GM soundfont

On Arch:

```bash
sudo pacman -S base-devel cmake sdl3 sdl3_image sdl3_mixer ffmpeg \
               fluidsynth soundfont-fluid
```

Build:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

`src/cursors_data.h` is generated from the original executable (requires
`icoutils`); regenerate it with:

```bash
python3 tools/extract_cursors.py ../abc-re/abc/ABC.EXE
```

## Run

```bash
./build/abc-sdl --data /path/to/abc
```

`--data` points at the extracted CD tree (the directory containing `FRM/`,
`WAV/`, `MIDI/`, `AVI/`). If omitted, the app tries, in order:

1. `$ABC_DATA`
2. `Glowny=` in `ABC.INI` in the current directory
3. `./abc`
4. `../abc-re/abc`

Assets missing from the disc tree are looked up in an addon directory. The
default is `./addon` in the repository root (also found as `../addon` when
running from `build/`); `$ABC_ADDON` or `--addon DIR` overrides it. The disc
tree always wins when it contains the file. The bundled addon currently
supplies the missing IGLO photo `AVI/46_2.AVI`.

Default start scene is `100` (splash). Useful options:

```
--addon DIR       fallback asset root (default: ./addon, then ../addon)
--start ID        open a specific scene (e.g. 1001 train, 3016 CYMBALKI)
--scale N         integer window scale (default 2)
--fullscreen      start fullscreen
--mute            disable audio
--validate        parse all scenes and print statistics
--shot ID         render one scene to a PNG and exit
--out FILE        output for --shot
--script FILE     replay a click script (see Testing)
```

### Controls

| input | action |
|-------|--------|
| mouse / click | interact |
| hover | reveals objects in the letter playground; raises individual crayons in the game toolbar at the bottom-right |
| wheel / ← → (train) | scroll the train |
| ESC | close overlay, go back one scene, or quit |
| F11 | fullscreen |
| F12 | save a screenshot to `screenshots/` |

The train strip keeps its horizontal position for the whole session: leaving
the hub for a letter/game and returning restores the same scroll offset.

### Audio

WAV playback uses SDL3_mixer. MIDI playlists are rendered with FluidSynth into
an SDL audio stream. The first existing soundfont among
`/usr/share/soundfonts/FluidR3_GM.sf2`, `SC-55`, `freepats`, … is used; set
`ABC_SOUNDFONT=/path/file.sf2` to override.

---

## Project layout

```
src/main.c      args, scene manager loop, window/canvas scaling, script runner
src/dfm.c       Delphi binary DFM (TPF0) parser
src/scene.c     FRM loader → components, bitmaps/masks, rendering, hit testing,
                generic component state machine, transitions
src/modules.c   per-module behaviour for all 10 module families
src/audio.c     WAV cache/one-shots, MIDI playlists (FluidSynth)
src/video.c     AVI demux/decode (FFmpeg), frame timing, audio extraction
src/font.c      tiny 5×7 bitmap font for placeholders and toasts
src/util.c      file loading, case-insensitive Windows path resolution
```

The engine mirrors the original's component semantics:

* `_BMPNumerNormal/Abnormal/Special` frame selectors (`-2` hidden, `0` = first
  frame, 1-based otherwise).
* `_AutoClickChange`, `_AutoMoveChange`, `_AutoClickExit`, `_ModalResult`.
* `_WaveClickFile`, `_WaveClickFile2`, `_WaveAutoPlay`, embedded
  `_WaveNormal/_WaveAbnormal/Wave.Data`.
* `TWysunBMP` slide-in toolbar, `TAnimujBMP` frame sequences,
  `TPrzesunBMP` computed strip, `TLabirynt`, `TSprawdzanie`,
  `TMapowanie`/`TMapowHotSpot`, `TEkran`, `TRandomMediaPlayer`.

Navigation is exactly the original model: a clicked component returns
`_ModalResult` (the next scene ID), `9999` exits.

### Bitmaps and masks

BMPs are decoded with SDL and uploaded as nearest-neighbour textures. 1-bpp
`_BMPMap*` blobs are parsed directly from their BMP payload so that palette
quirks cannot flip the meaning of the mask:

* the train's wagon hitboxes are derived from the 4050×145 mask (25 content
  runs: locomotive + A…Z);
* the maze's collision map is indexed in BMP/bottom-up row order, which is the
  order its `wejscieX/Y` entrance coordinates expect;
* sprite masks are used for mouse hit testing when they form a silhouette;
* Skladanka placed parts use their mask as an alpha silhouette; the mask
  palette is inspected because most use white=0/black=1 but some (AUTO `m4`)
  store the black silhouette in palette slot 0.

---

## Content fidelity

The port follows the original disc's data, with the explicitly requested
navigation/media fixes listed below:

* word 113 does not exist;
* YETI is still word 135, after the Z words;
* unused letter hotspots in the other letters keep their fallback links to
  words 1/5.

Requested fixes that are active by default:

* **IGLO navigation:** the I word arrows are re-linked through IGLO (3046), so
  it is reachable by default; the word screen and puzzle already exist, and
  the bundled addon supplies its missing photo. The I letter playground itself
  still has no IGLO/igloo hotspot.
* **Letter-playground object hover:** `_BMPMapMouse` masks are used for hit
  testing only, not as sprite alpha. Hovering the A playground pineapple now
  draws its complete replacement patch instead of compositing the new fruit
  over the baked one.
* **YETI navigation:** YETI is the only Y word, so its two arrow hotspots are
  inactive instead of inventing a destination (the shipped data points them
  back at 3135). The arrow artwork is baked into `tlo`; it is left visible,
  but the controls cannot be clicked. The six bogus fallback hotspots in the Y
  letter scene (2023) are hidden and inactive.
* **Absent/reused clips:** a film/photo button is enabled only if the clip
  belonging to that word (`<word>_1.AVI` / `<word>_2.AVI`) exists. This
  disables `4_1` and `135_1` and also stops words 6 and 135 from playing the
  unrelated `9_2.AVI` (the ladybird) through their photo buttons. The missing
  `46_2.AVI` photo is supplied by the bundled addon. Disabled buttons use the
  flat yellow placeholder baked into `tlo`; no translucent hotspot tint is
  drawn over it.
* **Puzzle board:** the `p01…p14` slots are filled with their VCL `Color` and
  a black grid, matching the original red board instead of showing the word
  picture through the cells.
* **Transparency:** the train strip and the pencil toolbar use their 1-bpp
  silhouette masks as alpha (this preserves the crayons' graphite tips and
  black dots); shape masks (e.g. the pineapple in letter A) are applied the
  same way. Animation backdrops remove only border-connected black, so
  interior black artwork is kept.
* **Crayon toolbar:** `kHelp`, `kReset`, `kPrint` and `kBack` animate
  independently, not as a batch. Each starts hidden with its triangular tip
  visible (`_DoKiedyWsowac` pixels), rises when hovered, and slides back down
  when the pointer leaves.
* **Task instructions:** entering a game scene plays the same spoken cue as
  its HELP pencil (`WAV/01.WAV`, `02.WAV`, …), and HELP now reliably replays
  it instead of the generic sound.
* **Reset:** the `kReset` pencil resets the activity and then speaks the same
  rules cue as HELP. It no longer replays its own shorter hover phrase.
* **Puzzle feedback:** placing each piece plays a short placement click; the
  long spoken congratulation (`_WaveLicznik`, `02a.wav`) plays only once,
  when the last piece is in place. A piece dropped on the wrong slot returns
  to its normal tray frame and stays retryable; clicking it again selects it
  and restores normal interaction (no permanent gray state).
* **Layout and word-screen overlays:** `Align=alClient` backgrounds use their
  native 640×480 bitmap; several scenes keep a stale 648×488 design size in the
  DFM, but the artwork is 640×480. Word and letter forms are positioned at
  origin `(0,0)`. `tlo` already contains the whole word, both `Aa` forms, the
  object, arrows, media buttons and train icon. The arrows are invisible click
  targets; the syllable strip, letter highlights, object highlight and the
  larger train icon are hover-only overlays. TV media buttons have no hover
  change, but a click switches that button to its pressed/active frame; the
  four buttons are mutually exclusive. Unlike the original (which kept the
  last button pressed), the button is released when the clip ends or is
  dismissed, so it visibly offers itself for another playback. The title is
  whole at rest and separates
  into syllables (`CYM BAL KI`) only while hovered. The syllable strip is a
  separate pre-rendered bitmap blitted at native size, not runtime text; its
  lettering is slightly lighter than the baked title, as on the disc.
* **Puzzle borders and alignment:** the piece sheets use their per-scene
  corner colour as the VCL transparent colour, so the pink/purple/green
  borders no longer show between placed pieces. The placed frame's transparent
  border is compensated by its measured content offset, and the piece is
  clipped to its slot, so picture chunks line up instead of shifting by
  2–4 px. Anti-aliased edge pixels are excluded from the content rectangle,
  and only border-connected tray-colour pixels are keyed, so a one-pixel
  fringe cannot leave the background colour inside the assembled picture.
  Selecting a piece switches its tinted tray frame to the original-colour
  frame; no selection rectangle is drawn. Wrong assignments clear the
  selection and restore the tray frame. While dragged, the piece is rendered
  above the assembled picture. Its measured content rectangle stays opaque;
  only pixels outside it are made transparent,
  so picture areas matching the tray colour never become holes. The same
  measurement excludes one-pixel tray-colour border columns that would
  otherwise show as vertical bars in the assembled picture.
* **Colouring palette and fill:** the rounded swatches are baked into `tlo`;
  the selected colour is the separate bow-tie-shaped sprite and is the only
  palette sprite shown (initial selection: green). Swatch colours come from
  the centre of the palette chip, and the flood fill uses the original black
  line art as its barrier. Anti-aliased grey edge pixels are recoloured as
  well, so fills meet the ink instead of leaving a light rim.
* **One voice at a time:** spoken cues are exclusive — starting a new speech
  cue stops the previous speech cue. Choosing an item therefore cuts the
  rules explanation. Non-speech effects are allowed to mix over speech: the
  wrong-answer buzz is an SFX track, so a wrongly chosen item's spoken
  explanation continues under the buzz. Pencil hover hints still play, but
  only after the pointer has dwelt on the pencil for 250 ms. MIDI music runs
  independently of speech and SFX.
* **Video/audio:** video frames are scheduled by PTS so long clips stay in
  sync with their audio; leaving a scene or closing a video stops its sound.
  TV and credits videos pause the background MIDI playlist (including any
  buffered synth notes) and resume it at the same tick when playback ends or
  the clip is dismissed. While a video is active its soundtrack is exclusive:
  speech and SFX requests are ignored, and generic stop-all operations cannot
  stop the video track.
* **Activity completion:** a solved game now stays on its completed screen;
  the child leaves with the blue `kBack` pencil (or the train icon where
  present). There is no automatic return to the word scene.
* **Correct/wrong-answer feedback:** Zgadywanka 1 plays the selected
  hotspot's spoken cue (`WAV/1d.WAV` for the correct record, `1a/1b/1c` for
  the distractors) instead of the empty `waveK` player. The wrong-answer buzz
  is mixed over the distractor's explanation, so the child hears both “what
  this is” and the wrong feedback.

Where the original data is ambiguous, the port uses the observable behaviour
and documents the inference:

* **Zgadywanka 1** — visible `o_i` is the correct object, visible `z_i` are
  distractors; correct plays `AnimOK`, wrong plays `AnimZle`.
* **Zgadywanka 2** — `o_i` are the correct object hotspots, `n_i` the wrong
  ones, `z_i` the result sprites revealed in the scene. When `_Licznik` is 0
  the visible `o` count is used so the two affected pages stay playable.
* **Zgadywanka 3** — `o_i` silhouettes and `z_i` objects pair by index.
* **Zgadywanka 4** — visible `o_i` are the targets; `_Licznik = 100` is kept
  as the original “no target” sentinel.
* **Skladanka** — clicking a tray part places it immediately in its `_MapHS`
  slot (the original has no second drop click); `TMapowanie` coordinates are
  relative to the `map` picture, and a part's `_BMPNumerSpecial` selects which
  target frame is shown. Alternative parts replace the same slot; clicking the
  same part again takes it out. Completion counts distinct filled slots, not
  pieces.
* **Puzzle** — pieces `kNN` and slots `pNN` pair by index; the tray state keeps
  the original magenta key rectangle, as the original does, and the board uses
  the per-cell VCL colours plus black grid lines.
* **Spot the difference** — found pairs are highlighted with the components'
  `Color` (red) outlines on both pictures.

---

## Testing

A headless regression suite lives in [`tests/run.sh`](tests/run.sh). It checks
the parser counts, loads and renders every scene, walks the train→letter→word
→game→word flow, completes every activity, verifies the YETI fix, absent-clip
handling and task-cue playback, checks IGLO navigation, and plays video clips:

```bash
DATA=../abc-re/abc tests/run.sh
```

Individual tools:

Data validation (no window needed):

```bash
./build/abc-sdl --data ../abc-re/abc --validate
# scenes: 294
# streams: 6330
# parse errors: 0
```

Render a scene:

```bash
SDL_VIDEODRIVER=dummy ./build/abc-sdl --data ../abc-re/abc --mute \
    --shot 3016 --out /tmp/3016.png
```

Scripted interaction (headless). Commands: `scene ID`, `move X Y`,
`click X Y`, `down/up X Y`, `wait MS`, `player X Y`, `render`, `dump`,
`shot FILE`.

```
scene 1001
wait 100
click 317 308        # wagon A
wait 200
move 280 360         # hover the record player
click 280 360        # open ADAPTER (3001)
click 500 398        # KlGra → Zgadywanka 1 (15001)
click 560 180        # correct answer
wait 9000
dump                 # back in 3001
```

```bash
SDL_VIDEODRIVER=dummy ./build/abc-sdl --data ../abc-re/abc --mute \
    --script /tmp/flow.txt
```

Set `ABC_DEBUG=1` for component-draw, palette and train-box diagnostics.

---

## Video playback

AVI clips are demuxed and decoded with FFmpeg. Video frames are scaled into
the scene's `TEkran` rectangle (320×240 in the original layout); the credits
clip (`AUTORZY.AVI`) plays full-screen from the train hub's rabbit. The
stream's audio is decoded once to 44.1 kHz stereo PCM and played through the
same mixer as the WAV one-shots. Frame presentation is driven by the stream's
PTS (not a fixed frame counter), so long clips remain in sync; closing the
video or changing scenes stops its audio. TV and credits clips pause the
scene's MIDI playlist while they play and resume it afterwards. When FFmpeg is
unavailable at build time the old placeholder card is used instead.

## Cursors

The brush (`PEDZEL`) and the twelve maze letters (`LIT_C`, `LIT_D`, …) are
extracted from `ABC.EXE` into `src/cursors_data.h` by
`tools/extract_cursors.py` (requires `wrestool` from `icoutils`). The brush is
installed while a colouring page is open, and scenes without a custom cursor
restore SDL's default arrow (SDL3's `SDL_SetCursor(NULL)` only redraws the
current cursor). The maze currently draws the word's initial as a sprite at the
player position rather than replacing the OS pointer; the extracted `LIT_*`
arrays are ready for a future cursor mode.

**Next milestones**

1. Use the `LIT_*` cursors as the actual maze pointer (and revisit whether the
   player letter should follow the pointer or replace it).
2. **Polish glyphs in the tiny font** (currently ASCII only) and a proper
   print dialog/export layout.
3. Add an IGLO hotspot/overlay to the I letter playground; remaining content
   decisions (YETI numbering, fallback links) are still original data.
