# Design

GNOME-style shōgi for jailbroken Kindles, in the spirit of the GnomeGames4Kindle ports of glchess and gnomine: GTK2, KUAL launcher, one engine bundled.

## Target device

Measured on the development Kindle (firmware 5.19.2, hard-float):

| | |
|---|---|
| SoC | MediaTek MT8110, 2× ARMv7 with NEON/VFPv4 |
| RAM | 474 MB total, ~25 MB free while the reader runs |
| Screen | 1236×1648 visible (framebuffer 1248 wide) |
| libc | glibc 2.20 |
| Toolkit | GTK 2.20.1, GLib 2.29.18, Cairo 1.10, Pango 1.26, librsvg 2.40.20 in `/app/lib` |
| Missing | `libGL.so.1` (glchess needs a stub) |
| Japanese fonts | TBGothic, TBMincho |
| Window manager | awesome; windows need Kindle title strings (below) |

Color target: Kindle Colorsoft (1264×1680; color layer at half resolution).

## Implementation choices

- **C + GTK2, no Vala.** Modern valac emits code for much newer GLib than 2.29, and the glchess binary in GnomeGames4Kindle was built from an undocumented old valac. Plain C links against the Kindle's own GTK like gnomine does, so no bundled runtime is needed.
- **Structure follows glchess**: game model / scene / 2D view / engine / clock / history, a bar of actions.
- **Engine: Fairy-Stockfish** (GPLv3), `largeboards=yes` build for 9×9, ARMv7 NEON, small hash. Talks USI. Strength via `Skill Level`, shown as levels 1 to 10.
- **Players are interchangeable**: the game controller only sees players that receive the position and answer with a move (USI text) or resign/draw offers. Human, USI engine and remote players (LAN, CSA server, Lishogi Board API) implement the same interface, so online play needs no change to the board, clock or record code.
- **LAN play** (`src/core/net.c`, `src/core/lan.c`): TCP 4081 with UDP and mDNS discovery, every line encrypted after a key exchange; the protocol is in [`CODE.md`](CODE.md).
- **Rules in C, checked by perft** against Fairy-Stockfish (`go perft N`) on a set of SFEN positions, including drops, nifu, uchifuzume and promotion zones.
- **Kifu**: KIF (the default, read by any shōgi program), CSA and SFEN. No file chooser: saves go to a folder of their own (`/mnt/us/documents/shogi/` on the Kindle) and Load Game lists them.

## Kindle specifics (from GnomeGames4Kindle and exact-chess)

- Main window title `L:A_N:application_ID:denboku_PC:N_O:URL`; dialogs `L:A_D:application_ID:denbokuDialog`, and dialogs must be modal and transient for the main window or awesome does not show them.
- KUAL runs scripts from FAT, so engine paths are checked with file existence, not the executable bit.
- No animations: glchess redraws every 10 ms while a piece slides, which is slow and ghosts on e-ink. Moves appear in one redraw; only changed squares are invalidated.
- High-contrast GTK rc style (black on white, no button images), as in exact-chess.
- **Speed** (measured on the device by faking taps with XTEST): a tap shows its piece's moves in ~10 ms and a move's squares in ~8 ms, down from 1-2 s. What it took:
  - The board is drawn into an image in the process and sent per changed rectangle, already gray, in an image shared with the X server (MIT-SHM, as Gambatte-K2 does). Drawn on the window, each piece and text went to the server, which composes in software.
  - `mlockall` at start: the Kindle runs short of memory and dropped the code of GTK, Pango and cairo between taps (hundreds of page faults a move).
  - A move redraws only what changed, the squares first and alone, hands, clocks, move list and the old move's mark 200 ms later: the e-ink updates what is sent together as one box around it all.
  - Nothing large at once: the e-ink driver starts an update over ~420 px both ways only ~4.1 s after the last such one (what comes meanwhile waits with it), and the X server and the window manager (awesome with Lab126's e-ink library) join damage that arrives close together, or over an update still on its way, into one box. So on the Kindle everything is drawn in bands under 360 px on rows fixed on the screen, each once the X server has taken the one before (`in_bands()` in `view.c`): the board's redraws, and what GTK draws (`kindle.c`: an Expose not from the bands is turned back into a pending area; a page is laid out before GTK's own layout step would draw it at once, and drawn once its chips are built and the e-ink has shown what came before). The native windows (touches reach only those) have no background, which the X server filled at once as a page came and went. A drawer's flash is black bands painted on the game's own window (black windows made the window manager hold the screen as for a new window). Measured with a counter of the e-ink controller's interrupts (`hwtcon` in `/proc/interrupts`) and the library's log (`lipc-set-prop com.lab126.winmgr.ligl logMask 0xffffffff`).
  - No GtkLabel whose text changes (its new size stalls redraws while the window manager answers); no text by the X server; ☗☖ from Denboku Sans (the device's fonts lack them, and Pango searched all of them); clocks redrawn only when their text changes.
  - The autosave is written 2 s after the last move (the storage takes ~0.5 s); the engine runs under `nice 19`, after the screen is updated.

## Layout

The board gets as much of the screen as possible, as in glchess. Extra information is behind the ⓘ toggle.

- **One layout for every screen**: a bar (☰, Undo, Hint, ◀ ▶, ⓘ), a panel per player with the name, the pieces in hand and the clock, the board as large as fits, and the moves as a strip (portrait) or a column (landscape). ⓘ shows the game's players, clock and handicap; the menu ☰ is a sheet of tiles, and the other pages are drawers over the game or beside it.
- The side to move has its clock in an inverted pill.
- **Landscape** (any window wider than tall; the PC default): the board at the left, at its full height, and a column at the right with the opponent's panel, the moves, your panel and the bar. The layout follows the window size, so the PC switches between portrait and landscape while resizing. On the Kindle, Preferences → Appearance → Screen rotates the display through the window manager's `orientationLock`.
- **Japanese** text is horizontal everywhere, with no separate vertical mode. A vertical kifu column was tried and dropped: it holds far fewer moves, and the Kindle's Pango 1.26 draws vertical text badly.
- Pieces in hand are always in the order 飛 角 金 銀 桂 香 歩 with a count. Tap a piece in hand, then a square, to drop.
- Promotion: a small popup over the target square with the promoted and unpromoted piece (成 / 不成); forced promotions skip it.
- Selection is a thick frame, legal targets are dots, captures a dashed frame. The last move is shaded in color and marked with corner brackets in mono: e-ink partial updates only draw black and white, so grays would turn white where pieces move.
- Coordinates: 9–1 on top; ranks 一–九 in Japanese, a–i in English.
- UI languages: English, Japanese and Portuguese (`src/core/lang_<code>.c` and `src/shogi/strings_<code>.c`). Notation follows the language: Japanese (▲７六歩, 同　歩) or Western (☗P-7f).

## Pieces and color

Every piece is a shape drawn with Cairo plus one or two glyphs from a font loaded with FreeType from the game folder (nothing is installed system-wide). Each piece is rendered once per square size and cached.

- **Shape**: pentagon computed from the square size; the king is the largest and the pawn the smallest. A theme sets fill (flat or gradient), border and ink colors.
- **Styles**: 1 kanji, 2 kanji (two glyphs stacked and narrowed), pictorial, peanatsu's shogi_printout sets, the cursive Gata Sōsho and animals for every variant (below). The shogi_printout sets are traced from its SVGs with `tools/build_trace_font.py`: the western chess symbols (`--printout 1`), and the 1-kanji and 2-kanji xboard tiles (`--xboard 1`), rendered with the fonts they name so each piece is one glyph as printed, with Hari Seldon's cursive promoted pieces. Those two are named after their fonts, UnGungseo and AR PL UKai; `KeepGlyphSize`, as on the printout, keeps every glyph its size on a smaller koma. Gata Sōsho is a thin brush font, set and traced bolder (`--font FILE --grow 12`) so it reads on a small koma and on e-ink.
- **Tori shōgi**: the birds (燕 鴈 鷹 鵰 鶴 雉 鶉 鵬, one kanji each on every set) are kinds of their own (`PIECE_ART`). The quails, both 鶉, carry 左 or 右 in the lower corner of their side, as on Kanare Abstract's set (2022). Here it is a small red seal in Noto Sans JP (white on red, which reads at a small size and on e-ink), with the 鶉 a little smaller to leave it room, and it is left out with the move marks, which show the side. The sets of pictures have the quails face away from their side, toward the board's middle (`[Mirror]` in the animal set; two drawings in the pictorial one). Only sets with every bird of their own are offered: Noto Sans JP, Zen Antique, AR PL UKai (traced with `build_trace_font.py --tori` as large as its 金, then `[Scale]` to fit one kanji in the koma), the animals, and the pictorial set, with birds drawn in its style by `tools/make-birds.py` (`ShogiBirds.otf`). Gata Sōsho (five birds blank in gatasosyo), UnGungseo (no 鴈, 鵰, 鶉) and western are not offered (the western set would show the same birds as the pictorial one). This is `NotFor=` in a style's `[Style]`; a set chosen for all variants that a variant leaves out gives way there to that variant's default.
- **Move marks** follow shogi_printout's (page 7 of its PDF): a dot for a step, a bar for any distance (along the edge on the animals' square tiles), and for a jump a dot at the end of a stem, the knight's forked like a Y. Tori shōgi's eagle, which goes up to two squares diagonally back, has two dots there. Every mark stays at least 3 units (of the piece's 100) clear of the koma's or the tile's outline, and an animal stays as far inside the marks.
- **Pixel art**: a style may draw the whole piece on a grid of about N pixels without smoothing (`Pixels=N`), its outline a one-pixel inset of the pentagon, then enlarge it by a whole number so every pixel is the same size; `PixelFont` draws a bitmap-like font one font pixel per art pixel. No bundled set uses it.
- **Fonts**: any TTF/OTF in `fonts/`; `tools/font-subset.sh` cuts one down to the ~20 piece characters (12–40 KB). Glyphs are scaled by their real bounds so every font fills the piece the same way. The default set is shogi_printout's 1 kanji (UnGungseo); Noto Sans JP Medium (1 kanji) and Zen Antique (2 kanji) are OFL alternatives.
- **Piece styles** are small key files: for each piece, the text, the font and a scale. Promoted pieces can use a different font each, as on real koma, where the promoted silver, knight, lance and pawn are 金 written in increasingly cursive hands so the original piece stays recognizable.
- **Colored pieces** (Preferences, color screens, for all variants or one; on by default for Dōbutsu): each kind of piece its own background, the same in every set (shogi_printout's colors, `PIECE_ART`; a style may set its own with `[Fill]`); without the piece shape the glyph takes the color, darker. Animals keep their colored pictures either way, and their move marks turn black on colored tiles.
- **Animals** (`dobutsu.ini`, every variant, the default for Dōbutsu): Noto Emoji outlines; on color screens flat colors under them, from `NotoEmoji-shogi-colors.otf` (`tools/make-animal-colors.py` traces each colored area into a glyph in the emoji font's units, and `[Layers]` paints them under the outline at the same size and place, the thick black parts (a mane, the phoenix's flames, a comb) over it within a black rim: vector at any size, a few KB, no pictures to decode). Dōbutsu shōgi's lion, giraffe, elephant, chick and hen; Goro Goro Dōbutsu shōgi's dog (gold) and cat (silver); a rabbit for the jumping knight, a boar for the lance (猪突猛進), a dragon for the dragon king and a horse for the dragon horse; the promoted silver, knight and lance grow up into a tiger, a kangaroo and a rhinoceros.
- **Without the piece shape** every piece is drawn the same size, each glyph scaled to its own bounds (a western pawn as large as the king).
- **Pictorial font**: `tools/build_trace_font.py` traces Lishogi's `intl_monochrome_2d` symbols (CouchTomato87, CC BY 4.0; the kings are the crown of the same author's `international` set) with potrace into `data/fonts/ShogiIntl.otf` (13 KB), one symbol per 1-kanji code point, so it is just another font.
- **Color**: wood gradient, red promoted pieces, green last move, blue move dots. Mono keeps the interface black on white (or white on black): menu tiles, cards, buttons, switches and icons framed on the background, no gray fills, which e-ink dithers; the game behind the menu is not faded there. Gray stays only where the board needs it.

## Code

See [`CODE.md`](CODE.md).
