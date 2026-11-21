#!/usr/bin/env python3
"""Fetch the MSYS2 mingw64 packages for a GTK 2 Windows build and unpack them into a sysroot:
    windows-sysroot.py [--update] <out dir>
The packages are the ones tools/windows-packages.lock names, each checked against its SHA-256; --update
resolves MSYS2's current ones instead and writes them to the lock. MSYS2 dropped gtk2 from its database,
but the last package is still on the mirror."""
import hashlib, io, os, re, subprocess, sys, tarfile, urllib.request

REPO = "https://repo.msys2.org/mingw/mingw64/"
GTK2 = "mingw-w64-x86_64-gtk2-2.24.33-8-any.pkg.tar.zst"
WANT = ["mingw-w64-x86_64-gcc-libs", "mingw-w64-x86_64-freetype", "mingw-w64-x86_64-fontconfig"]
LOCK = os.path.join(os.path.dirname(os.path.abspath(__file__)), "windows-packages.lock")
update = sys.argv[1] == "--update"
out = os.path.abspath(sys.argv[1 + update])
cache = os.path.join(out, "pkgs")
os.makedirs(cache, exist_ok=True)


def fetch(name, fresh=False):
    path = os.path.join(cache, name)
    if fresh or not os.path.exists(path):
        urllib.request.urlretrieve(REPO + name, path)
    return path


def sha256(path):
    with open(path, "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def untar(path):
    data = subprocess.run(["zstd", "-dc", path], check=True, capture_output=True).stdout
    return tarfile.open(fileobj=io.BytesIO(data))


def resolve():
    db = {}
    with untar(fetch("mingw64.db", fresh=True)) as t:
        for m in t.getmembers():
            if m.name.endswith("/desc"):
                fields, key = {}, None
                for line in t.extractfile(m).read().decode().splitlines():
                    if line.startswith("%"):
                        key = line.strip("%")
                        fields[key] = []
                    elif line and key:
                        fields[key].append(line)
                entry = (fields["FILENAME"][0], fields.get("DEPENDS", []))
                db[fields["NAME"][0]] = entry
                for p in fields.get("PROVIDES", []):
                    db.setdefault(re.split("[<>=]", p)[0], entry)
    with untar(fetch(GTK2)) as t:
        info = t.extractfile(".PKGINFO").read().decode()
    todo = [re.split("[<>=]", l.split(" = ", 1)[1])[0] for l in info.splitlines() if l.startswith("depend = ")]
    todo += WANT
    done, files = set(), [GTK2]
    while todo:
        name = todo.pop()
        if name in done or name not in db:
            continue
        done.add(name)
        filename, deps = db[name]
        files.append(filename)
        todo += [re.split("[<>=]", d)[0] for d in deps]
    return sorted(files)


if update:
    files = resolve()
    with open(LOCK, "w") as o:
        o.write("# MSYS2 mingw64 packages of the Windows build (tools/windows-sysroot.py --update rewrites it)\n")
        for f in files:
            o.write(f"{f} {sha256(fetch(f))}\n")
else:
    with open(LOCK) as f:
        pinned = [l.split() for l in f if l.strip() and not l.startswith("#")]
    files = []
    for name, want in pinned:
        path = fetch(name)
        if sha256(path) != want:
            os.remove(path)
            sys.exit(f"{name}: not the SHA-256 in the lock")
        files.append(name)
for f in files:
    with untar(fetch(f)) as t:
        t.extractall(out, filter="tar") if hasattr(tarfile, "data_filter") else t.extractall(out)
print("\n".join(files))
