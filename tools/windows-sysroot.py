#!/usr/bin/env python3
"""Fetch the MSYS2 mingw64 packages for a GTK 2 Windows build and unpack them into a sysroot:
    windows-sysroot.py <out dir>
MSYS2 dropped gtk2 from its database, but the last package is still on the mirror."""
import io, os, re, subprocess, sys, tarfile, urllib.request

REPO = "https://repo.msys2.org/mingw/mingw64/"
GTK2 = "mingw-w64-x86_64-gtk2-2.24.33-8-any.pkg.tar.zst"
WANT = ["mingw-w64-x86_64-gcc-libs", "mingw-w64-x86_64-freetype", "mingw-w64-x86_64-fontconfig"]
out = os.path.abspath(sys.argv[1])
cache = os.path.join(out, "pkgs")
os.makedirs(cache, exist_ok=True)

def fetch(name):
    path = os.path.join(cache, name)
    if not os.path.exists(path):
        urllib.request.urlretrieve(REPO + name, path)
    return path

def untar(path):
    data = subprocess.run(["zstd", "-dc", path], check=True, capture_output=True).stdout
    return tarfile.open(fileobj=io.BytesIO(data))

db = {}
with untar(fetch("mingw64.db")) as t:
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

def gtk2_depends():
    with untar(fetch(GTK2)) as t:
        info = t.extractfile(".PKGINFO").read().decode()
    return [l.split(" = ", 1)[1] for l in info.splitlines() if l.startswith("depend = ")]

todo = [re.split("[<>=]", d)[0] for d in gtk2_depends() + WANT]
done = set()
files = [GTK2]
while todo:
    name = todo.pop()
    if name in done or name not in db:
        continue
    done.add(name)
    filename, deps = db[name]
    files.append(filename)
    todo += [re.split("[<>=]", d)[0] for d in deps]
for f in files:
    with untar(fetch(f)) as t:
        t.extractall(out, filter="tar") if hasattr(tarfile, "data_filter") else t.extractall(out)
print("\n".join(sorted(files)))
