# Building Denboku Shōgi

`tools/build.sh` runs the tests and builds the platforms named, Linux when none is, then installs on the Kindle when its SSH answers and on the phone when adb reaches it (over Wi-Fi or USB):

```sh
tools/build.sh                  # Linux: the folder, the tarball and the AppImage
tools/build.sh kindle android   # those two
tools/build.sh all              # Linux, Kindle, Windows and Android, with the checks
tools/build.sh --release        # every format, Flatpak and ARM64 too
```

Options:

- `--checks` adds the fuzzer and the game under ASan (`tools/asan-test.sh`, about 2.5 minutes).
- `--no-restart` keeps a running Kindle game.
- `--dev` starts the game on the Kindle and the phone in developer mode (`DENBOKU_DEV`, below) for that start only.

The inputs live outside the repo, in the folder `INV` (`../denboku-shogi-investigation` by default). The devices come from variables, which `$INV/devices.env` may set: the Kindle is `KINDLE` (its address; password `KINDLE_PASSWORD`, `kindle` by default), the phone `PHONE` (its address, the port found through `adb mdns services`; without it, a phone on USB). Without them the install is skipped, as Flatpak is without `flatpak` and Android without the SDK.

Each step prints how long it took. At the end the script says which platforms it updated and which it did not, with the build they still have; `$INV/builds.txt` keeps the latest build of each platform and what each device has. Each platform's script also runs alone, its inputs defaulting to `$INV` and its output to `dist/`: `tools/build-linux.sh`, `tools/build-windows.sh`, `tools/package-kindle.sh` (after `make kindle`), `tools/build-android.sh`.

The program builds an object per source file, so `make -j` compiles them side by side and a change rebuilds only what it touches.

About shows the version, from the commit's tag (`v0.9.0` → `0.9.0`, `DENBOKU_VERSION`; none between releases), and the build number, `YYYYMMDD.HHMM` in UTC (`DENBOKU_BUILD`): the tagged commit's time on a tagged commit, so the Windows build on GitHub gets the same one, else the time of the build; `build.sh` prints both and gives every platform the same ones. The Windows exes carry the version in their file properties too. Android's `versionCode` is major × 10000 + minor × 100 + patch.

Third-party sources built here are git submodules in `third_party/` (curl, mbed TLS, gdk-pixbuf), pinned to a release tag: `git submodule update --init --recursive --depth 1` after cloning.

The desktop and Kindle steps need the inputs in `$INV` (Kindle sysroot, Windows sysroot, engine builds and AppImage tools):

| Input                                                                         | Command                                                                                         | Used by                         |
| ----------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------- | ------------------------------- |
| Fairy-Stockfish for Kindle, Linux (x86_64, ARM64) and Windows, at the commit `FSF_COMMIT` in the script | `tools/build-engine.sh <Fairy-Stockfish source> $INV/fsf-build-shogi [armv7 x86_64 arm64 windows]` | all desktop and Kindle packages |
| Kindle sysroot (libraries copied from a Kindle over SSH, headers from Debian) | `tools/sysroot.sh $INV/sysroot [kindle address]`                      | Kindle                          |
| Windows sysroot (MSYS2 GTK 2), pinned in `tools/windows-packages.lock` (`--update` moves to MSYS2's current ones) | `tools/windows-sysroot.py $INV/win`                                   | Windows                         |
| Termux's GTK 2 packages, pinned in `tools/android-packages.lock`              | `tools/android-sysroot.py` (run by `build-android.sh`)                                          | Android                         |
| appimagetool and its runtime                                                  | download into `$INV/appimage/`                                        | AppImage                        |

Docker is needed for the Linux, Windows and engine builds, clang and lld for the Kindle, JDK 17 and the Android SDK (platform 35, NDK 27.2) for Android, and `flatpak` for the Flatpak. With a Kindle reachable over SSH or a phone over adb, `build.sh` also installs the new build on them.

`tools/fsf-shogi-only.patch` (with `-DSHOGI_ONLY`, also on Android and ARM64) frees the tables shōgi never reads, keeps a 2 MB pawn table instead of 18 MB and starts with the 4 MB hash the game asks for: 49 MB resident instead of 66 (78 at the peak) on x86-64, with the same moves and nodes.

## Outputs

| File | Platform |
|---|---|
| `dist/denboku/` and `dist/denboku-kindle.zip` | Kindle KUAL extension |
| `dist/Denboku-x86_64.AppImage` | Linux x86_64, one file |
| `dist/denboku-linux/` and `dist/denboku-linux.tar.xz` | Linux x86_64 folder |
| `dist/denboku-linux-arm64.tar.xz` | Linux ARM64 folder |
| `dist/Denboku-x86_64.flatpak` | Linux x86_64 Flatpak |
| `dist/denboku-windows/` and `dist/denboku-windows.zip` | Windows portable |
| `dist/Denboku.apk`, `dist/Denboku-x86_64.apk` | Android 7+, arm64 and x86_64 (signed with the release key, see below) |

## Kindle

Prerequisites: the Kindle sysroot from `tools/sysroot.sh` (in `../denboku-shogi-investigation/sysroot`), the ARMv7 Fairy-Stockfish build, clang and lld. The Kindle step is a cross build against the sysroot and needs no Docker.

```sh
make test
make kindle SYSROOT=../denboku-shogi-investigation/sysroot
tools/package-kindle.sh build/kindle/denboku ../denboku-shogi-investigation/fsf-build-shogi/fairy-stockfish-armv7 dist
```

`tools/build.sh kindle` runs these, creates `dist/denboku-kindle.zip` and installs on the Kindle when SSH answers. It copies the new binary as `.new` and renames it over the running one, and updates the library shortcut if there is one. To stop a game started over SSH, use `kill $(pidof denboku)`: `pkill -f` with the game's path would kill the SSH command too. The Kindle (firmware 5.19.2) has glibc 2.20, GTK 2.20 and GLib 2.29, so no newer API may be used.

## Linux x86_64

```sh
tools/build-linux.sh ../denboku-shogi-investigation/fsf-build-shogi/fairy-stockfish-x86_64 dist ../denboku-shogi-investigation/appimage
```

The script builds in Docker (Debian bookworm), bundles the GTK 2 libraries except the glibc family, makes `dist/denboku-linux/`, `dist/denboku-linux.tar.xz` and, when the third argument points to `appimagetool` and `runtime-x86_64`, `dist/Denboku-x86_64.AppImage` (squashfs with zstd at level 22 in 1 MB blocks).

### Flatpak

```sh
tools/build-flatpak.sh dist/denboku-linux dist
```

This uses the prebuilt Linux folder and does not need an SDK or compiler. It requires `flatpak` and the `org.freedesktop.Platform//24.08` runtime; the app bundles GTK 2. The output is `dist/Denboku-x86_64.flatpak` (`flatpak install --user`). The script builds an app directory with `flatpak build-init`, exports it to a local ostree repo and bundles it with `flatpak build-bundle`.

## Windows portable

```sh
tools/build-windows.sh ../denboku-shogi-investigation/win ../denboku-shogi-investigation/fsf-build-shogi/fairy-stockfish-x86_64.exe dist
```

Uses the Windows sysroot and the mingw-w64 Docker image; output is `dist/denboku-windows/` and `dist/denboku-windows.zip`. The zip holds `Denboku.exe`, `bin/`, `etc/` and the data folders at its root. gdk-pixbuf is not MSYS2's but `tools/build-gdkpixbuf-windows.sh`'s (the submodule `third_party/gdk-pixbuf`, the same version, with only the PNG loader, built in), made once into `../denboku-shogi-investigation/win-pixbuf/`: MSYS2's JPEG and TIFF loaders brought nine more DLLs (4.7 MB).

On Windows itself, `tools\build-windows.ps1` (PowerShell) downloads MSYS2 into `build\msys64` and runs `tools/build-windows-native.sh` there: the same package from MSYS2's compiler, its GTK 2 (no longer in its database: `pacman -U` takes the last package from the mirror), the engine at the same commit and gdk-pixbuf with only PNG (`build-gdkpixbuf-windows.sh` runs meson natively in MSYS2). Both ways bundle with `tools/package-windows.sh`.

### Signed zip and MSIX

The release's Windows packages are built on GitHub, since the SignPath Foundation signs only what a GitHub-hosted runner built from the repo: `.github/workflows/windows.yml` runs on each `v*` tag (or by hand) and builds everything from scratch (the sysroot from the lock, the engine at `FSF_COMMIT`, gdk-pixbuf from the submodule), then the zip and, with `tools/package-msix.sh` (the folder plus `windows/AppxManifest.xml` and the logos from `data/art/icon.svg`) and `makeappx` on a Windows runner, `Denboku-x86_64.msix`. Only our own `Denboku.exe` and `bin/denboku.exe` are signed, inside both, and the MSIX itself (`windows/signpath-artifact.xml`, the artifact configuration pasted into SignPath). The repository's settings hold the secret `SIGNPATH_API_TOKEN` and the variables `SIGNPATH_ORGANIZATION_ID`, `SIGNPATH_PROJECT_SLUG`, `SIGNPATH_SIGNING_POLICY_SLUG` and `MSIX_PUBLISHER`, the certificate's subject exactly (the MSIX's publisher must match it). Without them the run leaves the unsigned packages as the artifact `windows-unsigned`, whose MSIX has a test publisher and installs with `Add-AppxPackage -AllowUnsigned Denboku-x86_64.msix` in an administrator PowerShell, for testing (recent Windows 11 only). Elsewhere, with Developer Mode on: copy it to a `.zip`, `Expand-Archive` it, rename `bin/libstdc%2B%2B-6.dll` to `libstdc++-6.dll` (the package encodes the `+`), put `CN=Jefik` as the `Publisher` in `AppxManifest.xml` and `Add-AppxPackage -Register AppxManifest.xml`. Signed, the artifact is `windows-signed`: `gh run download <run> -n windows-signed -D dist` before `tools/release.sh` (and `echo <build> > dist/denboku-windows.zip.build`, the run's build number). Installed from the MSIX, the game only tells of a new version: Windows installs the new MSIX over it, and its settings stay.

## Android

```sh
tools/build-android.sh
```

The script builds the game against Termux's GTK 2 packages. `tools/android-sysroot.py` downloads them once into `$INV/android-sysroot/`, at the versions and SHA-256 of `tools/android-packages.lock`; the `.deb` files stay in `android-sysroot/debs/`, since Termux drops old versions, and `--update` moves to the current ones. When `build/android/` lacks them, it builds the engine with `tools/build-engine-android.sh` and curl with `tools/build-curl-android.sh`. Then `tools/android-stage.py` lays everything out and Gradle makes `dist/Denboku.apk` (arm64-v8a) and `dist/Denboku-x86_64.apk`, one architecture each so each is about half the size (`ABIS=x86_64` builds only the emulator's). Prerequisites: JDK 17 and `ANDROID_HOME` (default `~/Android/Sdk`) with platform 35 and NDK 27.2. How it works and how to run it on the emulator: [`ANDROID.md`](ANDROID.md).

## Linux ARM64

The ARM64 package is cross-built on the computer, in Docker (Debian's `crossbuild-essential-arm64` and the arm64 GTK 2 packages):

```sh
ARCH=arm64 tools/build-linux.sh
```

It uses `fairy-stockfish-arm64` from `tools/build-engine.sh` and writes `dist/denboku-linux-arm64/` and `dist/denboku-linux-arm64.tar.xz`, whose folder is `denboku-linux` as on x86_64. The libraries to bundle are found through the programs' `NEEDED` entries (`readelf`), since `ldd` cannot read another architecture.

## Assets

Every image, sound and font in `data/` (and `windows/denboku.ico`) is made by a script from sources kept in `$INV`, so it is changed there and remade, not edited by hand:

- `tools/make-art.py <brush font> <title font>`: the art as SVG, its originals, with the letters as outlines from the fonts (Yuji Syuku and Marcellus, in `fonts-candidates/`): `data/art/banner.svg` (the README's) and `icon.svg`, `kindle/cover.svg`; from them, with rsvg-convert, `banner.png` (About), `icon.png` (also the Flatpak's and the AppImage's), the Android app's icon, `windows/denboku.ico` and the Kindle library shortcut's cover.
- `tools/readme-screenshots.sh`: the README's screenshots (`docs/screenshots/`, WebP at quality 90), taken from the game in `ui-test.sh`, each with a different palette, piece set and variant; the Kindle's from its look on the computer (`--kindle-look`, a Paperwhite's screen: the same picture as the device's but for a few pixels of text).
- `tools/make-sounds.py`: every sound, synthesized (numpy; the koto from the levels in `tools/strings.json`, measured on recordings), mono 16-bit at 22.05 kHz when nothing above 10 kHz is heard in it (44.1 kHz for the clicks), without the tail below −60 dB.
- `tools/pick-problems.py <engine> <parquet>`: standard shōgi's mate problems (`data/problems/problems.txt`), picked from Generated Tsume-Shogi (`tsume-dataset/`, the v4 `test` split and its card) and checked with Fairy-Stockfish; needs pyarrow.
- `tools/make-problems.py <engine>`: Minishōgi's (`data/problems/problems-minishogi.txt`), generated with Fairy-Stockfish.
- `tools/make-fonts.sh`: every font, from the originals in `fonts-candidates/`, `ui-fonts/`, Lishogi's pieces in `../lishogi/` and shogi_printout in `piece-sets/` (through `font-subset.sh`, `build_trace_font.py`, `make-ui-font.py`, `make-animal-colors.py` and `make-birds.py`, which draws the pictorial set's Tori shōgi birds). Run it again when the game writes a character the UI font lacks.

## Releases and updates

Each change players would notice gets a line under Unreleased in `CHANGELOG.md` as it is made; at a release that section takes the version and date and becomes the release notes. The version comes from an annotated tag on the release commit (`git tag -a v0.9.1`), made before `tools/build.sh --release`. `tools/release.sh <build> [version]` writes `dist/update.txt`, with each package's size and BLAKE2b-256, and signs it into `dist/update.txt.sig` with `build/sign-release` (`make sign-release`). Both go up with the packages to the GitHub release, tagged `v<version>`: the game reads `releases/latest/download/update.txt` and takes the package from `releases/download/v<version>/`, so a release made meanwhile does not mix in. The keys were made by `sign-release keygen`. The secret ones are in `$INV/keys/` (`update-main.key` signs; `update-reserve.key` is kept to move to if the first is lost or leaks) and must have a copy off this computer. The public ones are in `src/shogi/release.h`. A test server stands in for GitHub with `DENBOKU_DEBUG=1 DENBOKU_UPDATE_URL=http://…/` (plain HTTP is allowed only then). `tools/update-test.sh dist/denboku-linux.tar.xz <out>` runs that end to end in Docker (`AS_ZIP=1` serves the package as a zip, as the Kindle's is): the Linux folder updates itself, starts again and drops the version before, and a release whose program fails is undone by the start script.

The Android APKs are signed with the release key when `$INV/keys/android-release.properties` (or the file `DENBOKU_ANDROID_KEY` names) and its `android-release.jks` are there (`android/app/build.gradle`), else with the debug key; Android installs an update only over an APK signed with the same key, so that key must not change once an APK is out (keep a copy of it off this computer too).

## Developer flags

`denboku --help` (`Denboku.exe --help` on Windows, the AppImage too) lists them without starting the game. Most are also options: `--dev`, `--debug`, `--home DIR`, `--engine PROGRAM`, `--sfen SFEN`, `--net host[:PASSWORD]` or `join[:ADDRESS]`, `--kindle-look`, `--svg FILE[:SECONDS]`; each sets its variable. As variables (on the Kindle given before `./bin/denboku` over SSH, on Android through the files below):

| Variable | What it does |
|---|---|
| `DENBOKU_DEV=1` | Developer mode: a profile of its own, `dev/` in the data folder (settings, game in progress, history, saves), with the default settings at every start; `DENBOKU_DEV=keep` keeps them from the last start. The player's own settings and game are left alone. `tools/build.sh --dev` starts the Kindle's and the phone's game this way; on Android it is the file `files/dev` (`adb shell run-as jp.denboku touch files/dev`), for the next start only (the app removes it). |
| `DENBOKU_DEBUG=1` | Timings of each event and redraw, the HTTP requests, the LAN discovery and the Kindle's memory lock on stderr (`denboku.log` on the Kindle, `cache/denboku.log` on Android); allows `DENBOKU_UPDATE_URL`. On Android, the file `files/debug`. |
| `DENBOKU_HOME=<dir>` | The data folder (settings, game in progress, history) elsewhere, over the one chosen in Preferences. |
| `DENBOKU_SFEN=<sfen>` | Every new game starts from this position. |
| `DENBOKU_NET=host[:<password>]` or `join[:<address>]` | Hosts a LAN room (with that password) or joins one (at that address) as soon as the game starts. |
| `DENBOKU_UPDATE_URL=<url>` | With `DENBOKU_DEBUG`: the updates are looked for there (plain HTTP allowed), as `tools/update-test.sh` does. On Android, the file `files/update-url` holds it. |
| `DENBOKU_ENGINE=<program>` | Another USI engine for everything, over the Preferences. |
| `DENBOKU_BASE=<dir>` | The game's own files (pieces, sounds, fonts) from another folder. |
| `DENBOKU_KINDLE_LOOK=1` | On a computer, the Kindle's look and sizes (black and white, its fonts and touch sizes): with a Paperwhite's virtual screen (`SCREEN=1236x1648x8 DPI=298` in `ui-test.sh`) the picture is the device's but for a few pixels of text. |
| `DENBOKU_SVG=<file>[:<seconds>]` | The game's view (not the drawers, which are GTK's) as vectors into the file that many seconds (4) after the start, then the game quits; the README's screenshots are made so. Not on the Kindle, whose cairo has no SVG. |
| `LANG=ja_JP.UTF-8` | The language the system would give (Linux), when Preferences → Language is the system's. |

The Android app sets `DENBOKU_DPI`, `DENBOKU_VIEW`, `DENBOKU_DEVICE` and `DENBOKU_CUSTOM` itself (see [`ANDROID.md`](ANDROID.md)); `DENBOKU_BUILD` is the build's number, given to `make`.

## Checks

- `make test`: the rules, notation, network and layout tests, without GTK (the network ones need GIO).
- `make fuzz`: damages records, positions, moves and JSON at random and reads them back under ASan and UBSan (`FUZZ=<rounds>`, 1000 by default). `build.sh` runs it with `--checks`.
- `tools/ui-test.sh`: runs the GTK UI under Xvfb in Docker (clicks, keys, the window's close button, screenshots). `follow` takes further steps from a file while it runs, and `DOCKER_ARGS="--network host"` puts the game on the computer's network, for LAN games against a phone or the Kindle.
- `tools/asan-test.sh <engine>`: drives a build with ASan and UBSan through a move, the engine's answer, Preferences and the menu's pages the same way, and fails on a sanitizer report. `build.sh --checks` or `all` runs it after the Linux build.
- `tools/kindle-screenshot.sh <out.png> [host]`: grabs the Kindle's screen over SSH.
- Android is tested on the Android 35 x86_64 emulator (`denboku` AVD).
