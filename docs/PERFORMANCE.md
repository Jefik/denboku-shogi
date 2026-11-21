# Performance on the Kindle

Plan for faster response on e-ink, staying within GTK2/X11 and Cairo (no direct framebuffer, no waveform control). References: Gambatte-K2, exact-chess and GnomeGames4Kindle.

## Where the time went (before the first pass)

Pipeline (`src/core/view.c`):

1. `expose_cb` draws with Cairo into an in-process RGB24 surface (`v->frame`), one rectangle at a time with `cairo_clip`.
2. `blit_frame` on an 8-bit gray screen (the Kindle) converts RGB32→gray per pixel (77/150/29) into a `GdkImage` (`GDK_IMAGE_FASTEST`, which becomes MIT-SHM) and sends it with `gdk_draw_image`; color screens blit with Cairo.

Measured with `DENBOKU_DEBUG`:

- First draw of a style: 1–1.7 s (rasterizing ~16 piece glyphs into the cache); 348 ms by the third pass.
- Steady draws ~30 ms; after a style change ~280 ms draw + 10 ms send.
- Tap→marks ~10 ms, a move ~8 ms; the rest is the e-ink update the Kindle's window manager chooses: ~30 ms to start for a small one, but one over ~420 px both ways waits ~4.1 s after the last such one (hence the bands, `docs/DESIGN.md`).

## What Gambatte-K2 does (the "clean" mode)

- GTK2/XCB + MIT-SHM: draws straight into a shared **8-bit grayscale** buffer and commits rectangles, so there is no RGB→gray pass.
- A 65536-entry LUT for color→gray, NEON, one reused buffer, worker thread (FBInk mode only).
- A second mode (FBInk, direct framebuffer) is faster but ghosts; that is the hack ("gambiarra") this plan avoids.

Our code already follows the clean path (MIT-SHM via `GdkImage` + `gdk_draw_image`, per-rectangle draw, `mlockall`, squares first then the rest).

## Out of scope (hacks)

FBInk/direct framebuffer, waveform modes (A2/DU), `eips` for the board, and sharing FreeType faces across threads without a lock.

## First pass (2026-11-06)

Implemented:

- `style_cache()` renders one piece into the cache without painting; the view prewarms the board and hand sizes in idle turns (`warm_step`), so a later draw does not rasterize those glyphs.
- On mono frames the gray copy takes one channel instead of the 77/150/29 formula.
- The gray `GdkImage` asks for `GDK_IMAGE_SHARED` (MIT-SHM) first, falling back to `GDK_IMAGE_FASTEST`.

Measured on the Kindle (style change, full redraw): drawing unchanged at 257–280 ms, **sending 10 ms → 6 ms**; partial updates 1 ms. The initial board draw and the style-change rasterization (257–280 ms) still need the atlas/base-size work below. The prewarm helps the hand pieces, rotation and later styles.

## Second pass (2026-11-09)

Measured on the Kindle with taps sent through XTest (a small `xclick` built against the sysroot) and `DENBOKU_DEBUG`, which prints each draw's box, its number of rectangles and its time.

- A regression since the analysis marks: `view_set_board_marks()` queued the whole view at every move and `view_refresh()` took the "marks on the board" option, on by default, as a reason to redraw everything. Each move drew the whole screen twice (190 ms + 205 ms), and the e-ink updated all of it. Now only an analyzed game does; a move is back to its squares (**8 ms**), then the clocks and the moves 200 ms later (30 ms).
- The bar of actions was queued before the squares went to the screen, which made the first e-ink update as tall as the screen; it is queued after them, and only when a button's state or label changed.
- ◀ ▶ on black and white: only the squares that differ from the position shown (**12–15 ms**, was 186 ms). A hint queues its two squares (**19 ms**, was 186 ms).
- The menu and the drawers no longer redraw the whole screen; see the third pass.

## Third pass (2026-11-18)

Measured on the Kindle (build 20261119, Thumb-2) with `DENBOKU_DEBUG`:

- Start: the first whole-screen draw 348 ms, later whole-screen draws 180–215 ms; the game 32 MB resident, all of it locked by `mlockall` (no swap), the engine 21 MB (48 MB after a search).
- The ☰ menu is drawn by the view (an overlay): opening it drew the board under its opaque box for nothing. An overlay may now say what it covers with paint (`ViewOverlay.opaque`), and only the overlay is drawn there: **172 → 126 ms**. A press on one of its buttons redraws that button, not the sheet: **92 → 7 ms** before it closes.
- The firewall's `iptables -D` of a rule that is not there no longer writes to the log at each start.

## Plan

- **P0: instrument first.** Split the debug log into: glyph rasterization (cache miss), RGB→gray conversion, `gdk_draw_image`/SHM put, e-ink wait. Measure before/after each step below.
- **P1: pre-warm the piece cache (done for style changes).** `style_cache()` and the idle warm are in. `style_prewarm()` fills the board size synchronously when a style is applied behind Preferences, so the redraw after the dialog closes is fast. Still open: the first draw at start (348 ms in the third pass), which a worker with its own FreeType faces or an atlas could shorten.
- **P2: gray frame, no per-pixel conversion (done enough).** The mono fast path (one channel) is in and sending is ~6 ms. A full `CAIRO_FORMAT_A8` frame would at most save another 1–3 ms and would force every drawing helper to switch color models; not worth the risk now. Color stays RGB24.
- **P3: harden and trim the SHM path (done).** `GDK_IMAGE_SHARED` is requested first, with `GDK_IMAGE_FASTEST` as fallback.
- **P4: coalesce regions and reduce flushes (measured, not worth it).** `GdkRegion` already merges overlaps and the Kindle's X server merges the box; the double flush exists because the squares must reach the screen before the second batch.
- **P5: smarter piece cache (measured, not worth it).** The 96-entry linear scan is a few thousand integer comparisons per full draw (<0.1 ms); a hash would add code for no visible gain.
- **P6: general code (only if a profile says so).** The hot path is the point above, not `layout()` or the notation buffers.
- **P7: measure again and record.** Each pass above has its numbers; add a pass when the device or the drawing changes.

Order: P0 → P1 → P2 → P3 → P5 → P4 → P6 → P7. P1 and P2 are the two big wins; P0 stops us from optimizing the wrong part.

The physical e-ink refresh (~100–300 ms per region) is decided by the X server and the controller; gains come from less CPU per frame, less memory copied, and never rasterizing a glyph on the tap path.
