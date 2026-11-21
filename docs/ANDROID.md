# Android

The Android app is the PC program itself. The same GTK 2 code, built for Android, draws on an X server (Xvfb) inside the app, and a thin Java shell shows that screen and passes touches and keys to it. So features, looks and behavior are the PC's; only the way the screen, the input and a few system services reach the program differs.

## How it fits together

```
android/app/src/main/java/jp/denboku/  MainActivity (system bars, links), Sounds, Screen (the view, touch, keys, IME), Game (start, messages), Pad (controllers), Online (the foreground service)
android/app/src/main/c/screen.c        copies Xvfb's framebuffer (an XWD image, -fbdir) to the Surface
android/app/src/main/c/fbshim.c        loaded into Xvfb: that framebuffer in memory (a memfd), handed to the app
src/core/android.c                     in the game: input through XTest, redraws through XDamage, screen size through RandR
tools/android-sysroot.py               downloads the Termux packages (GTK 2, Xvfb, xkbcomp and what they need)
tools/android-packages.lock            their versions and SHA-256 (Termux keeps only the latest; --update moves to them)
tools/build-curl-android.sh            curl, static on mbed TLS (the submodules third_party/curl, third_party/mbedtls)
android/app/src/main/c/crypto.c        the SHA-1 Xvfb takes from OpenSSL, as libsha1.so
tools/android-stage.py                 lays out jniLibs/<abi> and runtime.zip from them
tools/build-android.sh                 builds everything and the APK
```

- The programs (`libdenboku.so` is the game, `libxvfb.so`, `libxkbcomp.so`, `libcurlbin.so`, `libfairystockfish.so`) are executables named as libraries, so the installer unpacks them into the library folder, the one place Android lets an app run programs from. Their libraries come from Termux, renamed to `lib*.so` (the installer drops `libfoo.so.1`); curl is our own build. No library may take the name of one of the system's (`libcrypto.so`, `libz.so`…): the game starts the engine through the system's `nice`, which would load ours through `LD_LIBRARY_PATH`.
- Termux builds for `/data/data/com.termux/files/usr`. The app ID, `jp.denboku`, has as many letters as `com.termux`, so its files folder plus `usr` is a path of the same length, and `android-stage.py` writes it in place of Termux's in every file. A compiler can split a path into pieces (an 8-byte tail inside an instruction); the tail after the ID is the same in both paths, and pieces cut inside the ID are fixed one by one. **The app ID must keep ten letters.**
- Libraries the game never calls are replaced by stubs of empty functions that `android-stage.py` compiles with the NDK: `libGL` (GLX is off in Xvfb), `libgraphite2` (no font has Graphite tables), so `libGLX` and `libGLdispatch` stay out of the APK; Xvfb's OpenSSL is `crypto.c`'s SHA-1. A stub exports only what the other files import from the real library.
- `runtime.zip` (assets) holds the keymap files Xvfb reads for its US keyboard (`XKB` in `android-stage.py`), `fonts.conf`, the certificates and the game's `data/`; the app unpacks it into `files/usr` and `files/denboku` after each install or update.
- Xvfb starts as large as the screen in either orientation; the game sets the X screen to the view's size with RandR (one row more, below the view, where the pointer waits between taps so no button stays lit). Rotation, a notch or the on-screen keyboard resize the view and the game's window as on a computer.
- Android refuses `/dev/ashmem` and System V shared memory to apps, and Xvfb's `-fbdir` file in the cache had its changed pages written to storage over and over (Xvfb calls `msync()` on all of it each time round its loop). `fbshim.c`, loaded into Xvfb with `LD_PRELOAD`, gives it a memfd instead of that file and hands it to the app over an abstract local socket that only the app's user may use (`SO_PEERCRED`); without it, `screen.c` opens the file as before.
- Text sizes follow the screen: `DENBOKU_DPI` is 96 × the Android density, and the sizes GTK gives in pixels (check boxes, arrows, scroll bars, borders) are multiplied by as much, so everything keeps the proportions it has on a computer.
- The game and the app talk over the game's stdin/stdout, one line per message (the list is at the top of `src/core/android.c`): touches, wheel, keys, typed text, Back and the view size in; redrawn areas, the on-screen keyboard, sounds, links and whether a network game is on out. The game outlives the activity: one that Android closes and opens again (after the app was in the background) takes over the screen and the messages.
- With no window manager, `android.c` gives the X focus as one would: to a window when it opens, back when it closes, and to the window under a tap after the finger lifts (before the press, a focus change would close the list a combo box just opened). The on-screen keyboard cuts the game's view, so dialogs center in the space it leaves.
- Two fingers scroll (as a mouse wheel); a mouse points and scrolls as on a computer. Back closes the open menu or dialog and goes back a page in a drawer (as Escape), or with only the board puts the app in the background. The on-screen keyboard opens when a text field takes the focus; composed text (Japanese input) is typed into the field as it is composed.
- Sounds play at the game's volume from static `AudioTrack`s in low-latency mode (Android's fast mixer; `SoundPool` goes through the normal one, with a longer delay), one per sound of the set in use (`Sounds.java`); links open in the browser. LAN discovery holds a Wi-Fi multicast lock. While a network game is on (a LAN room, or a Lishogi or CSA game being played) the app runs a foreground service, with a notification, a wake lock and a Wi-Fi lock: Android cuts the network of an app in the background, which dropped the connection. Swiping the app away from the recent apps ends it, notification and all.

## Build and run

```sh
tools/build-android.sh                    # -> dist/Denboku.apk (arm64-v8a), dist/Denboku-x86_64.apk
ABIS=x86_64 tools/build-android.sh        # the emulator only, faster
```

Prerequisites: JDK 17, the SDK (`ANDROID_HOME`, default `~/Android/Sdk`) with platform 35 and NDK 27.2, and network access the first time (Termux packages, kept in `$INV/android-sysroot/`, see [`BUILD.md`](BUILD.md)). The engine comes from `tools/build-engine-android.sh` (the same Fairy-Stockfish and shōgi-only patch as the other builds) into `build/android/engine/`.

```sh
emulator -avd denboku -no-window -no-audio -gpu swiftshader_indirect &
adb install -r dist/Denboku-x86_64.apk   # the emulator; a phone takes dist/Denboku.apk
adb shell am start -n jp.denboku/.MainActivity
adb exec-out run-as jp.denboku cat cache/denboku.log     # the game's messages
adb shell run-as jp.denboku touch files/debug             # DENBOKU_DEBUG from the next start
adb shell run-as jp.denboku touch files/dev               # developer mode (DENBOKU_DEV) for the next start only
```

## Limits

- Controllers (`/dev/input` is closed to apps): `Pad.java` reads their keys and joystick motion (Xbox-style buttons, the numbered buttons of plain DirectInput controllers, sticks, hat, triggers) and sends the game one bit set, `g BITS`, after `gn DRIVER NAME` for another controller; `rumble MS STRONG WEAK` goes back to the controller's motors, when Android exposes them (not the 8BitDo Ultimate's over Bluetooth).
- Paste buttons send `paste`; the app answers with its clipboard's text on one line, `clip TEXT`, which goes into the field that asked (the X server's clipboard is not Android's).
