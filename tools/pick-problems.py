#!/usr/bin/env python3
"""The standard shogi mate problems (data/problems/problems.txt), picked from Generated Tsume-Shogi by Shinta
Nakayama (https://huggingface.co/datasets/tokoroten-jp/generated-tsume-shogi, v4, CC BY 4.0): sound problems
(every attacker move a check, one solution, the defender holding every other piece, nothing left over).

  tools/pick-problems.py <fairy-stockfish> <dataset .parquet> [--per N] [--out FILE]

Needs pyarrow. Changes made to the dataset's rows, as its licence asks to say: rows derived from the YaneuraOu
collection are left out; only problems whose length stays the same when futile interpositions count are kept
(the game plays them by the rules, where any interposition is a move); a mate in 1 is not a gold or silver
dropped next to the king, and the longer ones have a sacrifice, focal move or hanging piece and are not
lined-up mates; the best of each length are kept by their tactics and fewest pieces; the attacker gets a king
in a corner it cannot be checked from (the game needs one), after which Fairy-Stockfish must still find the
same mate, as long, with the same first move.
"""
import argparse, os, re, subprocess

import pyarrow.parquet as pq

PLIES = (1, 3, 5, 7, 9)
COLUMNS = ["sfen", "mate_ply", "main_line", "mate_ply_with_futile", "is_representative", "origins", "has_tesuji",
           "is_narabe", "n_discovered", "n_non_promo", "n_long_drop", "n_free_sacrifice", "n_focal", "n_chuai",
           "first_move_sacrifice", "king_drag"]
CORNERS = ["9i", "1i", "8i", "2i", "9h", "1h", "7i", "3i", "5i", "9g", "1g"]


class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        for c in ("usi", "setoption name USI_Variant value shogi", "setoption name Hash value 64", "isready"):
            self.send(c)
        self.until("readyok")

    def send(self, s):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def until(self, prefix, out=None):
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("engine died")
            if out is not None:
                out.append(line.strip())
            if line.startswith(prefix):
                return line.strip()

    def in_check(self, sfen):
        self.send("position sfen " + sfen)
        self.send("d")
        self.send("isready")
        out = []
        self.until("readyok", out)
        return any(l.startswith("Checkers:") and l[9:].strip() for l in out)

    def mate(self, sfen, plies):
        """The engine's mate in plies and first move, searched deep enough for this length."""
        self.send("position sfen " + sfen)
        self.send("go depth %d" % (plies + 4))
        out = []
        best = self.until("bestmove", out).split()[1]
        score = None
        for l in out:
            m = re.search(r" score mate (-?\d+)", l)
            if m:
                score = int(m.group(1))
        return score, best


def board_rows(sfen):
    rows = []
    for row in sfen.split()[0].split("/"):
        cells, i = [], 0
        while i < len(row):
            c = row[i]
            if c.isdigit():
                cells += [""] * int(c)
            elif c == "+":
                cells.append("+" + row[i + 1])
                i += 1
            else:
                cells.append(c)
            i += 1
        rows.append(cells)
    return rows


def write_rows(rows):
    out = []
    for cells in rows:
        s, empty = "", 0
        for c in cells:
            if c:
                s += (str(empty) if empty else "") + c
                empty = 0
            else:
                empty += 1
        out.append(s + (str(empty) if empty else ""))
    return "/".join(out)


def square(usi):  # "7c" -> (row, column) in the SFEN's rows
    return ord(usi[1]) - ord("a"), 9 - int(usi[0])


def with_king(eng, r):
    """The problem with the attacker's king in a free corner, if it keeps its solution."""
    parts = r["sfen"].split()
    first = r["main_line"].split()[0]
    for corner in CORNERS:
        rows = board_rows(r["sfen"])
        y, x = square(corner)
        if rows[y][x]:
            continue
        rows[y][x] = "K"
        sfen = " ".join([write_rows(rows), "b", parts[2], "1"])
        if eng.in_check(sfen):
            continue
        score, best = eng.mate(sfen, r["mate_ply"])
        if score == r["mate_ply"] and best == first:
            return sfen
    return None


def boring_one(r):
    """A mate in 1 by a gold or silver dropped next to the king (頭金 and the like)."""
    move = r["main_line"].split()[0]
    if move[1] != "*" or move[0] not in "GS":
        return False
    rows = board_rows(r["sfen"])
    ky, kx = next((y, x) for y, cells in enumerate(rows) for x, c in enumerate(cells) if c == "k")
    y, x = square(move[2:4])
    return max(abs(y - ky), abs(x - kx)) == 1


def pieces(r):
    return sum(1 for cells in board_rows(r["sfen"]) for c in cells if c)


def score(r):
    one = r["mate_ply"] == 1
    move = r["main_line"].split()[0]
    s = 3 * r["n_discovered"] + 2 * r["n_non_promo"] + 2 * r["n_long_drop"] + 2 * r["n_free_sacrifice"] + r["n_focal"] + 2 * r["n_chuai"]
    s += 2 * bool(r["first_move_sacrifice"]) + bool(r["king_drag"])
    if one:
        s += 2 * (move[1] != "*")  # a move on the board rather than a drop
    return s - 0.2 * pieces(r)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("dataset")
    ap.add_argument("--per", type=int, default=50)
    ap.add_argument("--out", default="data/problems/problems.txt")
    a = ap.parse_args()
    rows = pq.read_table(a.dataset, columns=COLUMNS).to_pylist()
    eng = Engine(a.engine)
    lines = []
    for plies in PLIES:
        pool = [r for r in rows if r["mate_ply"] == plies and r["mate_ply_with_futile"] == plies and r["is_representative"]
                and "yaneuraou-2020" not in (r["origins"] or []) and not r["is_narabe"]
                and (r["has_tesuji"] if plies > 1 else not boring_one(r))]
        pool.sort(key=score, reverse=True)
        kept = []
        for r in pool:
            sfen = with_king(eng, r)
            if sfen:
                kept.append(sfen)
            if len(kept) == a.per:
                break
        print(plies, "plies:", len(pool), "candidates,", len(kept), "kept")
        lines += ["%d %s" % (plies, s) for s in kept]
    eng.send("quit")
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w") as f:
        f.write("\n".join(lines) + "\n")


if __name__ == "__main__":
    main()
