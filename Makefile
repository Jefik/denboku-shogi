# make            native build (needs GTK 2 development files)
# make test       rules, notation, lessons, online setup, network, encryption, zip, update, layout and history index tests, no GTK needed
# make sign-release   tools/sign-release (the update manifest's keys and signature, tools/release.sh)
# make fuzz       the file readers under random damage (FUZZ=<rounds>)
# make kindle SYSROOT=<dir from tools/sysroot.sh>   cross build for the Kindle (armhf)
# make windows SYSROOT=<dir from tools/windows-sysroot.py>   cross build for Windows (x86_64, mingw-w64)
# make windows-native   the same on Windows, in MSYS2's MINGW64 shell (tools/build-windows-native.sh)
# make android SYSROOT=<dir from tools/android-sysroot.py>/<arch> ANDROID_ARCH=<arch>   the Android program

BUILD ?= build
CC ?= cc
PKG_CONFIG ?= pkg-config
CFLAGS ?= -O2 -g
override CFLAGS += -std=gnu11 -Wall -Wextra -Wno-deprecated-declarations
# Keep to the GLib API of the Kindle (2.29).
override CFLAGS += -DGLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_26 -DGLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_28
PKGS = gtk+-2.0 gio-2.0 gmodule-2.0 cairo-ft freetype2 fontconfig
# The build number in About: when it was built, UTC (tools/build.sh gives every platform the same).
DENBOKU_BUILD ?= $(shell date -u +%Y%m%d.%H%M)
# The version, from the commit's tag (v0.9.0 → 0.9.0); empty between releases.
DENBOKU_VERSION ?= $(shell git describe --tags --exact-match --match 'v[0-9]*' 2>/dev/null | sed 's/^v//')
export DENBOKU_BUILD DENBOKU_VERSION
# The Kindle keyboard window is watched with Xlib (dialogs.c); Windows has no X11.
XLIBS ?= -lX11
WINDRES ?= x86_64-w64-mingw32-windres

# src/core is the app; src/$(GAME) the game it plays (rules, notation, record formats, online
# servers, texts), behind src/$(GAME)/rules.h. A fork for another game replaces src/$(GAME).
GAME ?= shogi
override CFLAGS += -Isrc/core -Isrc/$(GAME)
# What the tests need: no GTK.
RULES = src/core/game.c src/core/lessons.c src/core/i18n.c $(wildcard src/core/lang_*.c) src/core/json.c $(filter-out src/$(GAME)/lishogi.c src/$(GAME)/csa.c,$(wildcard src/$(GAME)/*.c))
SRC = $(wildcard src/core/*.c) $(wildcard src/$(GAME)/*.c) $(wildcard src/third_party/*.c)
HDR = $(wildcard src/core/*.h src/$(GAME)/*.h src/third_party/*.h)
# The LAN's encryption: Monocypher (src/third_party, BSD-2 or CC0) under netcrypt.c.
CRYPT = src/core/netcrypt.c src/third_party/monocypher.c

all: $(BUILD)/denboku

# An object per source, so that make -j compiles them side by side and a change rebuilds only its file
# (any header rebuilds them all). The build number and version go only to the files that show them, rebuilt when they change.
OBJ = $(SRC:%.c=$(BUILD)/obj/%.o)
STAMP = $(BUILD)/obj/build-number
NUMBERED = $(BUILD)/obj/src/core/menus.o $(BUILD)/obj/src/core/updater.o $(BUILD)/obj/src/core/app.o $(BUILD)/obj/src/core/prefs.o
$(shell mkdir -p $(BUILD)/obj; [ "$$(cat $(STAMP) 2>/dev/null)" = "$(DENBOKU_BUILD) $(DENBOKU_VERSION)" ] || echo "$(DENBOKU_BUILD) $(DENBOKU_VERSION)" > $(STAMP))
$(NUMBERED): $(STAMP)
$(NUMBERED): override CFLAGS += -DDENBOKU_BUILD=\"$(DENBOKU_BUILD)\" -DDENBOKU_VERSION=\"$(DENBOKU_VERSION)\"

$(BUILD)/obj/%.o: %.c $(HDR)
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags $(PKGS)) -c -o $@ $<

$(BUILD)/denboku: $(OBJ) $(RES)
	$(CC) $(CFLAGS) -o $@ $(OBJ) $(RES) $(LDFLAGS) $(shell $(PKG_CONFIG) --libs $(PKGS)) $(XLIBS) -lm

# The .exe icon and version (1.2.3 → 1,2,3,0; 0,0,0,0 between releases), shared by Denboku.exe and bin/denboku.exe.
WIN_VERSION = $(shell echo "$(DENBOKU_VERSION)" | sed 's/[^0-9.].*//' | awk -F. '{printf "%d,%d,%d,0", $$1, $$2, $$3}')
$(BUILD)/icon.o: windows/denboku.rc windows/denboku.ico $(STAMP)
	@mkdir -p $(BUILD)
	$(WINDRES) -I windows -DVERSION_NUMBERS=$(WIN_VERSION) -DVERSION_TEXT='\"$(or $(DENBOKU_VERSION),$(DENBOKU_BUILD))\"' $< -O coff -o $@

$(BUILD)/test_%: tests/test_%.c $(RULES) $(HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ $< $(RULES)

# The network door, its encryption and the lines of a network game need GIO, not GTK.
$(BUILD)/test_net $(BUILD)/test_netproto $(BUILD)/test_netcrypt $(BUILD)/test_mdns $(BUILD)/test_problems $(BUILD)/test_zip $(BUILD)/test_update $(BUILD)/test_mixer: $(BUILD)/test_%: tests/test_%.c src/core/%.c src/core/%.h $(CRYPT) src/core/mdns.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(shell $(PKG_CONFIG) --cflags gio-2.0) -o $@ $< $(sort src/core/$*.c $(CRYPT) src/core/mdns.c) $(shell $(PKG_CONFIG) --libs gio-2.0)

# The screen's layout: arithmetic only.
$(BUILD)/test_layout: tests/test_layout.c src/core/layout.c src/core/layout.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -o $@ $< src/core/layout.c

# The history's index: GLib only.
$(BUILD)/test_histindex: tests/test_histindex.c src/core/histindex.c src/core/histindex.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined $(shell $(PKG_CONFIG) --cflags glib-2.0) -o $@ $< src/core/histindex.c $(shell $(PKG_CONFIG) --libs glib-2.0)

# The tests run side by side; their output is shown in order.
test: $(BUILD)/test_shogi $(BUILD)/test_game $(BUILD)/test_json $(BUILD)/test_online $(BUILD)/test_netcrypt $(BUILD)/test_net $(BUILD)/test_netproto $(BUILD)/test_mdns $(BUILD)/test_problems $(BUILD)/test_zip $(BUILD)/test_update $(BUILD)/test_layout $(BUILD)/test_histindex $(BUILD)/test_mixer $(BUILD)/test_lessons
	@fail=0; for t in $^; do ($$t >$$t.log 2>&1; echo $$? >$$t.rc) & done; wait; \
	for t in $^; do cat $$t.log; [ "$$(cat $$t.rc)" = 0 ] || { echo "FAIL $$t"; fail=1; }; done; exit $$fail

sign-release: $(BUILD)/sign-release
$(BUILD)/sign-release: tools/sign-release.c src/core/update.c src/core/update.h src/third_party/monocypher.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(shell $(PKG_CONFIG) --cflags glib-2.0) -o $@ tools/sign-release.c src/core/update.c src/third_party/monocypher.c $(shell $(PKG_CONFIG) --libs glib-2.0)

# The readers of records, positions, moves and JSON under random damage (ASan + UBSan): FUZZ rounds.
FUZZ ?= 1000
$(BUILD)/fuzz: tests/fuzz.c $(RULES) $(HDR)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -fsanitize=address,undefined -fno-sanitize-recover=undefined -o $@ $< $(RULES)

fuzz: $(BUILD)/fuzz
	$(BUILD)/fuzz $(FUZZ)

# Thumb-2 (-mthumb below): a fifth smaller, as fast on ARMv7.
KINDLE_FLAGS = --target=arm-linux-gnueabihf -march=armv7-a -mfpu=neon -mfloat-abi=hard --sysroot=$(SYSROOT)

kindle:
	@test -n "$(SYSROOT)" || { echo "set SYSROOT (see tools/sysroot.sh)"; exit 1; }
	PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) PKG_CONFIG_LIBDIR=$(SYSROOT)/usr/lib/pkgconfig \
	$(MAKE) BUILD=$(BUILD)/kindle CC="clang $(KINDLE_FLAGS)" CFLAGS="-O2 -mthumb -DDENBOKU_KINDLE" \
		LDFLAGS="-fuse-ld=lld -Wl,-rpath-link,$(SYSROOT)/usr/lib:$(SYSROOT)/lib:$(SYSROOT)/app/lib -s"

WIN_FLAGS = CFLAGS="-O2" RES=$(BUILD)/windows/icon.o LDFLAGS="-mwindows -s -lws2_32 -liphlpapi -lwinmm" XLIBS=

windows:
	@test -n "$(SYSROOT)" || { echo "set SYSROOT (see tools/windows-sysroot.py)"; exit 1; }
	PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) PKG_CONFIG_LIBDIR=$(SYSROOT)/mingw64/lib/pkgconfig \
	$(MAKE) BUILD=$(BUILD)/windows CC=x86_64-w64-mingw32-gcc $(WIN_FLAGS)

windows-native:
	$(MAKE) BUILD=$(BUILD)/windows CC=gcc WINDRES=windres $(WIN_FLAGS)

# Android: the Termux packages of tools/android-sysroot.py; tools/build-android.sh packs the APK.
ANDROID_NDK ?= $(HOME)/Android/Sdk/ndk/27.2.12479018
ANDROID_ARCH ?= aarch64
ANDROID_PREFIX = $(SYSROOT)/data/data/com.termux/files/usr
ANDROID_TARGET_aarch64 = aarch64-linux-android24
ANDROID_TARGET_arm = armv7a-linux-androideabi24
ANDROID_TARGET_x86_64 = x86_64-linux-android24

android:
	@test -n "$(SYSROOT)" || { echo "set SYSROOT (see tools/android-sysroot.py)"; exit 1; }
	PKG_CONFIG_SYSROOT_DIR=$(SYSROOT) PKG_CONFIG_LIBDIR=$(ANDROID_PREFIX)/lib/pkgconfig:$(ANDROID_PREFIX)/share/pkgconfig \
	$(MAKE) BUILD=$(BUILD)/android-$(ANDROID_ARCH) CFLAGS="-O2" XLIBS="-lX11 -lXtst -lXdamage -lXfixes" \
		CC="$(ANDROID_NDK)/toolchains/llvm/prebuilt/linux-x86_64/bin/clang --target=$(ANDROID_TARGET_$(ANDROID_ARCH))" \
		LDFLAGS="-L$(ANDROID_PREFIX)/lib -Wl,-rpath-link,$(ANDROID_PREFIX)/lib -s"

clean:
	rm -rf $(BUILD)

.PHONY: all test fuzz sign-release kindle windows windows-native android clean
