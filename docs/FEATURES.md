# Features

What a complete shōgi app should have, checked against Lishogi (the open-source reference, cloned beside this repo: `../lishogi`, its rules in `../scalashogi` and `../shogiops`) and 81Dojo's documented rules. [x] is done, [ ] is not yet; "out" marks what is not for this project.

## Play

- [x] Standard rules: drops, promotion, nifu, uchifuzume, sennichite, perpetual check, impasse.
- [x] Handicaps (駒落ち), with the official side (Gote/uwate gives and moves first).
- [x] Clocks: sudden death, byoyomi, increment, presets and custom values.
- [ ] Correspondence clocks (days per turn) and resume after days away.
- [x] Undo and redo, takeback requests in LAN games, resign (confirmed), impasse declaration, history navigation.
- [x] Premoves, plan arrows/circles, board flip, legal-move hints.
- [x] Sandbox/position editor for every variant: piece palette, move/erase tools, drag and paint, rectangle selection with promote/flip/delete, pieces to and from the hands, clear by side, undo, and a Play button that plays the position at once; KIF/CSA/SFEN round trip.
- [x] Sounds (synthesized) and silent mode.
- [x] Variants: Standard, Minishōgi (5×5), Kyotoshōgi (5×5), Checkshōgi (9×9, wins on check), Dōbutsu (3×4, king capture/try rule), Goro Goro (5×6), Tori shōgi (7×7 with birds), Judkins shōgi (6×6) and Euroshōgi (8×8). Goro Goro, Tori, Judkins and Euroshōgi perfts are checked against Fairy-Stockfish; the board and rules are generic. Dōbutsu and Goro Goro (ごろごろどうぶつしょうぎ, played with the same animal pieces) default to the animal piece set with move marks and the Bubbles sounds. The engine plays every variant. KIF/CSA and autosave carry a variant header and the variant's start. Reference: `shogiops/src/position/rules/`.

## Engine

- [x] USI client, Fairy-Stockfish, levels, hints, engine vs engine.
- [x] Another USI program (YaneuraOu…) for the standard game on computers (not Android), in Preferences → Game.
- [x] Game analysis as on Lishogi and lichess: evaluation per move, ?!/?/?? marks in the list and on the board, best moves as arrows (1–5 lines), eval graph, move times graph, average centipawn loss and accuracy per side, jumps to each side's next mark, options for time, CPUs, memory and lines.
- [ ] Analysis mode: continuous eval of any position, principal variation text, an evaluation gauge beside the board.
- [ ] Mate search in any position (`go mate N`).
- [ ] Opening book (optional) and tablebases (out).

## Records and data

- [x] KIF (with BOD) and CSA save/load; SFEN; autosave/resume.
- [x] Game history: finished games kept as KIF (the last 200), listed with date, players and result; delete one or clear them all.
- [ ] KI2 and PSN import/export; USI move list.
- [ ] Export the position as an image and share it.
- [ ] Annotations/comments and variations in the kifu (Lishogi study tree).
- [ ] Cloud/account sync (out for now).

## Online

- [x] LAN: host/join, lobby, chat, password, every line encrypted (the password never sent), reconnection, rematch (with notices), kick/ban, editor positions. LAN, Lishogi and CSA check for a network interface first (airplane mode).
- [x] CSA servers (Floodgate-style), Lishogi Board API (seek, AI, challenge, moves, chat).
- [ ] Lishogi login with OAuth PKCE (no token paste).
- [ ] Correspondence games (server keeps them, move notifications).
- [ ] Spectate/TV, tournaments, simuls, studies, puzzles (Lishogi features).
- [ ] Internet peer-to-peer (NAT traversal or relay) and a room/matchmaking server.
- [ ] Draw offers; abort before the first moves (partly done on Lishogi).

## Interface

- [x] Portrait and landscape layouts, e-ink-first drawing, move list with scroll.
- [x] Piece styles (1 and 2 kanji, cursive, pictorial, western, animals), with or without the piece shape, colored or not, themes, palettes, dark/mono.
- [x] Coordinates, Japanese/Western notation, English/Japanese/Brazilian Portuguese UI, automatic by default (the system locale; the Kindle's from `/var/local/system/locale`).
- [x] Tutorial: lessons played on the board (pieces, promotion, drops, check and mate patterns, mates in two and three with the other side defending, brinkmate, reading Japanese notation, castles and breaking them, openings, piece values, tesuji, handicap book lines; every variant has its own), wrong moves explained by the rule they break, graded with stars, offered on the first start.
- [x] The game's strategies named (each side's rook and castle, the Bishop Exchange) in ⓘ.
- [x] Mate problems (tsume shōgi), standard and Minishōgi: 1 to 9 plies, solved ones remembered, the engine defending and checking each move.
- [x] Custom time controls, sound choice, About with banner.
- [x] Your own piece sets (fonts and style files, or pictures in a folder or zip) and sound sets from a folder, with the built-in ones as fallback; LAN discovery also over mDNS.
- [x] Keyboard and game controller play (evdev on Linux, XInput and winmm/DirectInput on Windows), every key and button rebindable, Japanese confirm/cancel by default, Xbox/Nintendo/PlayStation button names and colors beside the buttons on screen.
- [x] Colors for color blindness (Appearance): the last move blue and the hint vermilion, for any palette.
- [ ] Accessibility: screen-reader labels, larger targets.
- [ ] More languages (each is a pair of files and a line in `LANGUAGES` and `GAME_LANGUAGES`).
- [x] Build number in About (the UTC date and time of the build).
- [ ] Version in About (when versioning starts).

## Platforms

- [x] Kindle (GTK2, KUAL/Jobman), Linux x86_64 (GTK2 bundle/AppImage/Flatpak), Linux ARM64 (tarball), Windows (portable GTK2), Android (the same GTK2 program on an X server inside the APK; [`ANDROID.md`](ANDROID.md)).
- Web: shelved (2026-11-10).
- [ ] iOS/macOS (out).

## Data and privacy

- [x] Settings in `settings.ini`; autosave; offline play.
- [ ] Token storage hardening (Android keystore/keychain) when the ports exist.
- [ ] Crash/error reporting (out; keep local logs).

## References

- Lishogi views/modules: `analyse`, `puzzle`, `study`, `tournament`, `simul`, `coach`, `tv`, `learn`, `insight`, `coordinate`, `editor`, `importer`, `export`, `search`, `timeline`.
- Lishogi rules/variants: `shogiops/src/position/rules/` and `src/constants.ts`.
- 81Dojo rules and manners (handicaps, clocks, etiquette).

This list feeds `TODO.md` (pending) and `DONE.md` (done); keep it at the feature level.
