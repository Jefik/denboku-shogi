#!/usr/bin/env python3
"""Lay out the Android build's native part:
  android-stage.py <sysroot>/<arch> <denboku program> <libs out> <runtime.zip out> <NDK clang> <target>
<libs out> (the APK's jniLibs/<abi>) gets the programs and every library they need, under names the
installer keeps (lib*.so); runtime.zip has the files they read (keymaps, fonts.conf, certificates)
and data/ of the game. Termux builds for the prefix /data/data/com.termux/files/usr: the app's ID has as many
letters, so its own prefix (/data/data/jp.denboku/files/usr) takes the place of that one in the files.
The libraries in STUBS are never called by the game (GLX is off in Xvfb, no font has Graphite tables, curl
only speaks HTTPS over TCP): each is replaced by one of empty functions, so the libraries they need stay out."""
import os, re, shutil, struct, subprocess, sys, tempfile, zipfile

TERMUX = b"/data/data/com.termux/files/usr"
PREFIX = b"/data/data/jp.denboku/files/usr"
assert len(TERMUX) == len(PREFIX)
# The compiler can keep a path in pieces (16-byte constants, an 8-byte tail in an instruction): the
# tail is the same in both prefixes, and a piece cut inside the ID is fixed on its own.
PIECES = [(TERMUX[:k], PREFIX[:k]) for k in range(12, 21)] + [(TERMUX[k:], PREFIX[k:]) for k in range(12, 21)]
# From the system; every other library comes with the app.
SYSTEM = {"libc.so", "libm.so", "libdl.so", "liblog.so", "libandroid.so", "libz.so.1"}
STUBS = {"libGL.so.1", "libgraphite2.so"}
PROGRAMS = {"bin/Xvfb": "libxvfb.so", "bin/xkbcomp": "libxkbcomp.so"}
# Only the keymap files the X server reads for its "us" keyboard (with either default rules; the game types
# through XTest and maps keysyms itself), not the 4 MB of every layout and the XML for setting tools.
XKB = ["compat/accessx", "compat/basic", "compat/caps", "compat/complete", "compat/iso9995", "compat/ledcaps", "compat/lednum", "compat/ledscroll", "compat/level5", "compat/misc", "compat/mousekeys", "compat/xfree86", "geometry/pc", "keycodes/aliases", "keycodes/evdev", "keycodes/xfree86", "rules/base", "rules/evdev", "symbols/inet", "symbols/keypad", "symbols/pc", "symbols/srvr_ctrl", "symbols/us", "types/basic", "types/complete", "types/extra", "types/iso9995", "types/level5", "types/mousekeys", "types/numpad", "types/pc"]
DATA = [f"share/X11/xkb/{f}" for f in XKB] + ["share/X11/locale/locale.alias", "share/X11/locale/locale.dir",
        "share/X11/locale/compose.dir", "share/X11/locale/C", "share/X11/locale/en_US.UTF-8", "etc/fonts",
        "etc/tls/cert.pem"]
ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def needed(path):
    """DT_NEEDED of a 32- or 64-bit little-endian ELF."""
    d = open(path, "rb").read()
    is64 = d[4] == 2
    shoff, = struct.unpack_from("<Q" if is64 else "<I", d, 0x28 if is64 else 0x20)
    shentsize, shnum = struct.unpack_from("<HH", d, 0x3a if is64 else 0x2e)
    secs = []
    for i in range(shnum):
        o = shoff + i * shentsize
        if is64:
            _, typ, _, _, off, size, link = struct.unpack_from("<IIQQQQI", d, o)
        else:
            _, typ, _, _, off, size, link = struct.unpack_from("<IIIIIII", d, o)
        secs.append((typ, off, size, link))
    out = []
    for typ, off, size, link in secs:
        if typ != 6:  # SHT_DYNAMIC
            continue
        stroff = secs[link][1]
        fmt, step = ("<qQ", 16) if is64 else ("<iI", 8)
        for o in range(off, off + size, step):
            tag, val = struct.unpack_from(fmt, d, o)
            if tag == 0:
                break
            if tag == 1:
                out.append(d[stroff + val:d.index(b"\0", stroff + val)].decode())
    return out


def flat(name):
    """libfoo.so.1.2 -> libfoo.so: the installer only unpacks lib*.so."""
    return re.sub(r"\.so(\.\d+)+$", ".so", name)


def symbols(nm, path, defined):
    out = subprocess.run([nm, "-D", "--defined-only" if defined else "--undefined-only", path],
                         check=True, capture_output=True, text=True).stdout
    return {line.split()[-1] for line in out.splitlines() if line.strip()}


def stub(clang, target, real, users, out):
    """A library with the functions of `real` that `users` call, each doing nothing: 0, or for a
    version query a zeroed buffer (an empty string, a struct of zeros)."""
    nm = os.path.join(os.path.dirname(clang), "llvm-nm")
    wanted = sorted(symbols(nm, real, True) & set().union(*(symbols(nm, u, False) for u in users)))
    src = ["static char zero[256];"] + [f"void *{s}(void) {{ return {'zero' if 'version' in s else '0'}; }}" for s in wanted]
    with tempfile.NamedTemporaryFile("w", suffix=".c", delete=False) as c:
        c.write("\n".join(src) + "\n")
    try:
        subprocess.run([clang, f"--target={target}", "-O2", "-shared", "-fPIC", "-s", "-fno-builtin", "-w",
                        f"-Wl,-soname,{os.path.basename(out)}", "-o", out, c.name], check=True)
    finally:
        os.unlink(c.name)


def main():
    sysroot, program, libs_out, zip_out, clang, target, curl, crypto = sys.argv[1:9]
    p = os.path.join(sysroot, TERMUX.decode().lstrip("/"))
    lib = os.path.join(p, "lib")
    files = {os.path.abspath(program): "libdenboku.so"}
    files.update({os.path.join(p, k): v for k, v in PROGRAMS.items()})
    # Our own curl, static on mbed TLS (tools/build-curl-android.sh), and a libcrypto with only the SHA-1 Xvfb
    # takes (android/app/src/main/c/crypto.c): Termux's curl pulled in OpenSSL and HTTP/3, some 7 MB.
    files[os.path.abspath(curl)] = "libcurlbin.so"
    # Named apart: a libcrypto.so in the app's folder would be the one the system's own programs (toybox's nice,
    # which starts the engine) load through LD_LIBRARY_PATH.
    own = {"libcrypto.so.3": (os.path.abspath(crypto), "libsha1.so")}
    todo, libs = list(files), set()
    while todo:
        for n in needed(todo.pop()):
            if n in SYSTEM or n in libs:
                continue
            if n in own:
                libs.add(n)
                continue
            if not os.path.exists(os.path.join(lib, n)):
                sys.exit(f"missing library {n}")
            libs.add(n)
            if n not in STUBS:
                todo.append(os.path.join(lib, n))
    files.update({own[n][0] if n in own else os.path.realpath(os.path.join(lib, n)): own[n][1] if n in own else flat(n)
                  for n in libs})
    renames = {n: own[n][1] if n in own else flat(n) for n in libs if n in own or flat(n) != n}
    renames["libz.so.1"] = "libz.so"
    pattern = re.compile(rb"(?<=[\0/])(" + b"|".join(re.escape(n.encode()) for n in renames) + rb")\0")

    def fix(data):
        data = data.replace(TERMUX[:21], PREFIX[:21])
        for old, new in PIECES:
            data = data.replace(old, new)
        # A shorter name, padded with NULs: the strings around it stay where they were.
        return pattern.sub(lambda m: renames[m.group(1).decode()].encode() + b"\0" * (len(m.group(1)) - len(renames[m.group(1).decode()]) + 1), data)

    shutil.rmtree(libs_out, ignore_errors=True)
    os.makedirs(libs_out)
    stubs = {os.path.realpath(os.path.join(lib, n)) for n in libs & STUBS}
    for src, name in files.items():
        if src not in stubs:
            with open(os.path.join(libs_out, name), "wb") as o:
                o.write(fix(open(src, "rb").read()))
            os.chmod(os.path.join(libs_out, name), 0o755)
    users = [os.path.join(libs_out, name) for src, name in files.items() if src not in stubs]
    for src in stubs:
        stub(clang, target, src, users, os.path.join(libs_out, files[src]))
    # Kept from the engine build (tools/build-engine-android.sh).
    abi = os.path.basename(libs_out.rstrip("/"))
    engine = os.path.join(ROOT, "build", "android", "engine", abi, "libfairystockfish.so")
    if os.path.exists(engine):
        shutil.copy(engine, libs_out)

    with zipfile.ZipFile(zip_out, "w", zipfile.ZIP_DEFLATED) as z:
        for d in DATA:
            top = os.path.join(p, d)
            walk = [(top, None, [os.path.basename(top)])] if os.path.isfile(top) else os.walk(top, followlinks=True)
            for base, _, names in walk:
                for n in names:
                    f = os.path.join(base if os.path.isdir(top) else os.path.dirname(top), n)
                    rel = os.path.relpath(f, p)
                    z.writestr("usr/" + rel, fix(open(f, "rb").read()))
        for d in ["art", "fonts", "styles", "sounds", "problems", "lessons"]:
            for base, _, names in os.walk(os.path.join(ROOT, "data", d)):
                for n in names:
                    f = os.path.join(base, n)
                    z.write(f, "denboku/" + os.path.relpath(f, os.path.join(ROOT, "data")))
    print(len(files), "programs and libraries in", libs_out)


main()
