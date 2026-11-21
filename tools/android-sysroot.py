#!/usr/bin/env python3
"""Download the Termux packages the Android build runs on (GTK 2, Xvfb and their
dependencies) and unpack them:  android-sysroot.py [--update] <arch> <out dir>
<arch> is a Termux one (aarch64, arm, x86_64). The packages are the ones tools/android-packages.lock names, each
checked against its SHA-256 (Termux drops old versions, so the .deb files are kept in <out dir>/debs/<arch> and
taken from there first); --update takes Termux's current versions instead and writes them to the lock. Files land in <out>/<arch>/data/data/com.termux/files/usr,
the prefix Termux builds for; tools/build-android.sh moves them to the app's own one."""
import hashlib, io, os, re, sys, tarfile, urllib.request

REPOS = ["https://packages.termux.dev/apt/termux-main/dists/stable/main/binary-{arch}/Packages",
         "https://packages.termux.dev/apt/termux-x11/dists/x11/main/binary-{arch}/Packages"]
WANT = ["gtk2", "libxtst", "libxdamage", "xorg-server-xvfb", "xorg-xkbcomp", "xkeyboard-config",
        "ca-certificates", "glib", "libandroid-shmem"]
# Pulled in by the packages above but not used by the game.
SKIP = {"termux-tools", "termux-am", "dash", "bash", "coreutils", "busybox", "termux-exec", "termux-keyring",
        "termux-licenses", "ncurses", "readline", "python", "perl", "dbus", "shared-mime-info", "gtk-update-icon-cache",
        "resolv-conf", "less", "dialog", "procps", "psmisc", "grep", "sed", "gawk", "findutils",
        "xorg-xauth", "xorg-xkbcomp-dev", "hicolor-icon-theme", "adwaita-icon-theme", "desktop-file-utils",
        "gsettings-desktop-schemas", "libcrypt", "libgcrypt", "libgpg-error", "xkeyboard-config-dev", "xorg-fonts-alias",
        "xorg-fonts-encodings", "xorg-font-util", "xorg-mkfontscale", "ttf-dejavu", "libdbus"}


def packages(arch):
    pk = {}
    for url in REPOS:
        text = urllib.request.urlopen(url.format(arch=arch)).read().decode()
        base = url.split("/dists/")[0]
        for block in text.split("\n\n"):
            f = dict(re.findall(r"^([\w-]+): (.*)$", block, re.M))
            if "Package" in f:
                f["url"] = base + "/" + f["Filename"]
                pk[f["Package"]] = f
    return pk


def deps(f):
    out = []
    for field in ("Depends", "Pre-Depends"):
        for d in f.get(field, "").split(","):
            d = d.split("|")[0].strip().split(" ")[0]
            if d:
                out.append(d)
    return out


def ar_members(data):
    assert data[:8] == b"!<arch>\n"
    i = 8
    while i < len(data):
        name, size = data[i:i + 16].decode().strip().rstrip("/"), int(data[i + 48:i + 58])
        yield name, data[i + 60:i + 60 + size]
        i += 60 + size + (size & 1)


def relative_links(m, dest):
    """Absolute symlinks point into the Termux prefix: make them relative, so they stay inside."""
    if m.issym() and m.linkname.startswith("/"):
        here = os.path.dirname("/" + m.name.lstrip("./"))
        m = m.replace(linkname=os.path.relpath(m.linkname, here), deep=False)
    return tarfile.data_filter(m, dest)


def unpack(deb, out):
    for name, body in ar_members(deb):
        if name.startswith("data.tar"):
            with tarfile.open(fileobj=io.BytesIO(body)) as t:
                t.extractall(out, filter=relative_links)


LOCK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "android-packages.lock")


def resolve(arch):
    """Termux's current versions of WANT and what it needs: [(name, version, sha256, url)]."""
    pk = packages(arch)
    todo, done = list(WANT), set()
    while todo:
        n = todo.pop()
        if n in done or n in SKIP:
            continue
        if n not in pk:
            print("missing:", n, file=sys.stderr)
            continue
        done.add(n)
        todo += deps(pk[n])
    return [(n, pk[n]["Version"], pk[n]["SHA256"], pk[n]["url"]) for n in sorted(done)]


def main():
    update = sys.argv[1] == "--update"
    arch, top = sys.argv[1 + update:3 + update]
    out = os.path.join(top, arch)
    lines = [l.split() for l in open(LOCK)] if os.path.exists(LOCK) else []
    if update:
        mine = resolve(arch)
        lines = [l for l in lines if l[0] != arch] + [[arch, *p] for p in mine]
        with open(LOCK, "w") as o:
            o.write("# Termux packages of the Android build (tools/android-sysroot.py --update rewrites an architecture's)\n")
            o.writelines(" ".join(l) + "\n" for l in sorted(lines) if not l[0].startswith("#"))
    else:
        mine = [tuple(l[1:]) for l in lines if l[0] == arch]
        if not mine:
            sys.exit(f"no {arch} packages in {LOCK}: run with --update")
    cache = os.path.join(top, "debs", arch)
    os.makedirs(cache, exist_ok=True)
    for name, version, sha, url in mine:
        path = os.path.join(cache, os.path.basename(url))
        if not os.path.exists(path):
            print("get", name, version)
            data = urllib.request.urlopen(url).read()
            with open(path + ".part", "wb") as o:
                o.write(data)
            os.rename(path + ".part", path)
        data = open(path, "rb").read()
        if hashlib.sha256(data).hexdigest() != sha:
            sys.exit(f"{path}: not the SHA-256 in the lock")
        unpack(data, out)
    with open(os.path.join(out, "packages.txt"), "w") as o:
        o.writelines(f"{name} {version}\n" for name, version, _, _ in mine)
    print(len(mine), "packages in", out)

main()
