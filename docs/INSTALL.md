# Installing Denboku Shōgi

Download the package for your device from the GitHub release. Every package carries its engine (Fairy-Stockfish, GPLv3) and, on Kindle, Linux and Windows, the GTK 2 libraries it needs. No account is needed except for Lishogi and CSA servers.

## Checking a download

The release page shows each file's SHA-256 beside it. Compare it with what you downloaded before running it:

- Linux: `sha256sum <file>`.
- Windows: `certutil -hashfile <file> SHA256`.

## Kindle

The game is a KUAL extension. It needs a jailbroken Kindle with KUAL or Jobman.

1. Unzip `denboku-kindle.zip` and copy its `denboku/` folder to `/mnt/us/extensions/` (over USB or SSH).
2. In KUAL, open **電墨将棋 Denboku Shōgi → Play**.
3. To put the game in the library, use **Add shortcut to the library** in the KUAL menu, or copy `denboku/shortcut_denboku.sh` to `/mnt/us/documents/`. On Jobman Kindles the same script is the shortcut.

Settings, the game in progress and the history are kept in `/mnt/us/extensions/denboku/`, saved games in `/mnt/us/documents/shogi/` (see [`MANUAL.md`](MANUAL.md#files)).

## Windows

Unpack `denboku-windows.zip` into a folder of its own (its files are at the zip's root) and run `Denboku.exe`. Keep the `bin/`, `etc/`, `art/`, `fonts/`, `styles/`, `sounds/`, `problems/` and `lessons/` folders next to it; the engine and the GTK DLLs are in `bin/`. Windows on ARM runs it through x64 emulation; there is no native ARM64 build.

Or open `Denboku-x86_64.msix` (signed, from the releases that have it) to install it like any Windows app, with a Start menu entry and an uninstaller; a newer MSIX installs over it.

## Linux x86_64

- **AppImage**: `chmod +x Denboku-x86_64.AppImage && ./Denboku-x86_64.AppImage`.
- **Folder**: unpack `denboku-linux.tar.xz` and run `./denboku-linux/denboku.sh`, which points `LD_LIBRARY_PATH` at the bundled GTK 2 libraries.
- **Flatpak**: `flatpak install --user Denboku-x86_64.flatpak`, then launch **Denboku Shōgi** from the menu or run `flatpak run org.denboku.shogi`. It needs the `org.freedesktop.Platform//24.08` runtime; GTK 2 is inside the app.

The AppImage and the folder keep their data in `~/.local/share/denboku/`, the Flatpak in its own app data directory.

## Linux ARM64

There is only a tarball, with its own GTK 2 libraries and ARM64 engine:

```sh
tar xJf denboku-linux-arm64.tar.xz
./denboku-linux/denboku.sh
```

## Android

Android 7 or later: open `Denboku.apk` on the phone or tablet (arm64; allow installing from unknown sources), or `Denboku-x86_64.apk` on an x86_64 emulator or Chromebook. `adb install -r` works too. It is the PC program, so everything works as there: touch acts as a mouse, two fingers scroll and Back closes what is open.
