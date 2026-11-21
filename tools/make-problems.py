#!/usr/bin/env python3
"""Generates mate problems (tsume shogi) with Fairy-Stockfish: Minishogi's, data/problems/problems-minishogi.txt
(standard shogi's are picked from a dataset by tools/pick-problems.py).

  tools/make-problems.py <fairy-stockfish> [--per N] [--seed S] [--jobs J] [--out FILE]

A line is "<plies> <sfen>": black mates in that many plies (1, 3, 5, 7 or 9: the 手 of 1手詰, 3手詰…). A position is made at random (the defender's king in
the top three ranks with a few pieces of its own, the attacker's pieces around it and in hand) and
given every other piece in the defender's hand (残り駒全部, so it can interpose), and kept when the engine
finds a mate where every attacker move is a check and the only one that mates in time (the last may have
twins), the attacker's hand is empty at the end and a mate in 1 is not a gold or silver dropped next to the
king. Nothing is copied from anywhere.
"""
import argparse, multiprocessing, os, random, re, subprocess, sys, time

PLIES = (1, 3, 5, 7, 9)
# What differs between the variants: the engine's name, the board, the defender king's ranks, the
# attacker's pieces (the pieces on the board, with the common ones repeated) and those in hand.
VARIANTS = {
    "shogi": dict(file="problems.txt", size=9, king_ranks=3, attack="GGSSNLBRP" + "PP", defend="gsplngp", hand="GSBRNLPGSBR", reach=3,
                  total=dict(R=2, B=2, G=4, S=4, N=4, L=4, P=18)),
    "minishogi": dict(file="problems-minishogi.txt", size=5, king_ranks=2, attack="GGSBRPP", defend="gsbrpp", hand="GSBRPGSBR", reach=2,
                      total=dict(R=2, B=2, G=2, S=2, P=2)),
}
CFG = VARIANTS["minishogi"]
VARIANT = "minishogi"


class Engine:
    def __init__(self, path):
        self.p = subprocess.Popen([path], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        self.send("usi")
        self.until("usiok")
        for o in ("USI_Variant value " + VARIANT, "Hash value 32", "MultiPV value 2", "USI_Ponder value false"):
            self.send("setoption name " + o)
        self.send("isready")
        self.until("readyok")

    def send(self, s):
        self.p.stdin.write(s + "\n")
        self.p.stdin.flush()

    def until(self, prefix, collect=None):
        while True:
            line = self.p.stdout.readline()
            if not line:
                raise RuntimeError("engine died")
            if collect is not None:
                collect.append(line.strip())
            if line.startswith(prefix):
                return line.strip()

    def after(self, sfen, moves):
        """The SFEN after the moves."""
        self.send("position sfen %s moves %s" % (sfen, " ".join(moves)))
        self.send("d")
        self.send("isready")
        out = []
        self.until("readyok", out)
        return next((l[5:].strip() for l in out if l.startswith("Sfen:")), "")

    def checkers(self, sfen, moves=()):
        """The squares giving check to the side to move."""
        self.send("position sfen %s%s" % (sfen, (" moves " + " ".join(moves)) if moves else ""))
        self.send("d")
        self.send("isready")
        out = []
        self.until("readyok", out)
        for l in out:
            if l.startswith("Checkers:"):
                return l[9:].split()
        return []

    def search(self, sfen, depth):
        """The final depth's two lines: [(mate score or None, pv)]."""
        self.send("position sfen " + sfen)
        self.send("go depth %d" % depth)
        out = []
        self.until("bestmove", out)
        lines = {}
        for l in out:
            m = re.match(r"info depth (\d+) .*multipv (\d+) score (mate|cp) (-?\d+) .* pv (.*)", l)
            if m:
                lines[int(m.group(2))] = (int(m.group(4)) if m.group(3) == "mate" else None, m.group(5).split())
        return [lines.get(1), lines.get(2)]

    def close(self):
        self.send("quit")
        self.p.wait()


def random_position(rnd):
    board = {}
    n = CFG["size"]
    kf, kr = rnd.randrange(n), rnd.randrange(CFG["king_ranks"])
    board[(kf, kr)] = "k"
    board[(rnd.randrange(n), n - 1)] = "K"

    def near(r):
        for _ in range(30):
            f, rk = kf + rnd.randint(-r, r), kr + rnd.randint(-r, r)
            if 0 <= f < n and 0 <= rk < n and (f, rk) not in board:
                return f, rk
        return None

    def legal_at(piece, rk):
        black = piece.isupper()
        last = rk == 0 if black else rk == n - 1
        last2 = rk <= 1 if black else rk >= n - 2
        p = piece.upper()
        return not (p in "PL" and last or p == "N" and last2)

    def pawn_files(color):
        return {f for (f, _), p in board.items() if p == color}

    def put(piece, r):
        for _ in range(8):
            sq = near(r)
            if not sq or not legal_at(piece, sq[1]):
                continue
            if piece in "Pp" and sq[0] in pawn_files(piece):
                continue
            board[sq] = piece
            return

    for _ in range(rnd.randint(1, 4)):
        put(rnd.choice(CFG["attack"]), CFG["reach"])
    for _ in range(rnd.randint(1, 4)):
        put(rnd.choice(CFG["defend"]), 2)
    if rnd.random() < 0.35:  # a promoted piece or two
        for sq, p in list(board.items()):
            if p in "SNLP" and rnd.random() < 0.3 and sq[1] < (3 if n == 9 else 2):
                board[sq] = "+" + p
    hand = {}
    for _ in range(rnd.choice((0, 1, 1, 2, 2, 3))):
        p = rnd.choice(CFG["hand"])
        hand[p] = hand.get(p, 0) + 1
    # Every other piece goes to the defender's hand (残り駒全部).
    left = dict(CFG["total"])
    for p in list(board.values()) + [p for p, k in hand.items() for _ in range(k)]:
        base = p.lstrip("+").upper()
        if base in left:
            left[base] -= 1
    if any(v < 0 for v in left.values()):
        return ""
    for p, v in left.items():
        if v:
            hand[p.lower()] = v
    rows = []
    for rk in range(n):
        row, empty = "", 0
        for f in range(n):
            p = board.get((f, rk))
            if p:
                row += (str(empty) if empty else "") + p
                empty = 0
            else:
                empty += 1
        rows.append(row + (str(empty) if empty else ""))
    order = "RBGSNLPrbgsnlp"
    h = "".join((str(hand[p]) if hand[p] > 1 else "") + p for p in order if p in hand and hand[p] > 0) or "-"
    return "/".join(rows) + " b " + h + " 1"


def boring_one(sfen, pv):
    """A mate in 1 by a gold or silver dropped next to the king (頭金 and the like)."""
    if len(pv) != 1 or pv[0][1] != "*" or pv[0][0] not in "GS":
        return False
    n = CFG["size"]
    rows = sfen.split()[0].split("/")
    for y, row in enumerate(rows):
        x = 0
        for c in row.replace("+", ""):
            if c.isdigit():
                x += int(c)
                continue
            if c == "k":
                ty, tx = ord(pv[0][3]) - ord("a"), n - int(pv[0][2])
                return max(abs(ty - y), abs(tx - x)) == 1
            x += 1
    return False


def work(args):
    path, seed, per, deadline, variant = args
    global CFG, VARIANT
    CFG, VARIANT = VARIANTS[variant], variant  # the workers may not inherit the globals
    rnd = random.Random(seed)
    eng = Engine(path)
    found = []
    try:
        while time.time() < deadline:
            sfen = random_position(rnd)
            if not sfen or "K" not in sfen or "k" not in sfen:
                continue
            # The defender is not in check (asked as if it moved), nor the attacker.
            if eng.checkers(sfen.replace(" b ", " w ", 1)) or eng.checkers(sfen):
                continue
            first, second = eng.search(sfen, max(PLIES) + 2)
            if not first or not first[0] or first[0] < 1 or first[0] > max(PLIES):
                continue
            if second and second[0] is not None and 0 < second[0] <= max(PLIES):
                continue  # more than one way in
            k, pv = first
            if len(pv) < k:
                continue
            ok = not boring_one(sfen, pv) and not any(c.isupper() for c in (eng.after(sfen, pv[:k]).split() + ["", "", ""])[2])  # nothing left in hand
            for i in range(0, k, 2):  # each move of the attacker gives check, and is the only one in time
                if not ok:
                    break
                if not eng.checkers(sfen, pv[: i + 1]):
                    ok = False
                elif 0 < i < k - 1:
                    best, other = eng.search(eng.after(sfen, pv[:i]), k - i + 2)
                    ok = bool(best) and not (other and other[0] is not None and 0 < other[0] <= k - i)
            if ok:
                found.append((k, sfen, pv[:k]))
    finally:
        eng.close()
    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("engine")
    ap.add_argument("--per", type=int, default=30, help="problems to keep for each length")
    ap.add_argument("--seed", type=int, default=1)
    ap.add_argument("--jobs", type=int, default=os.cpu_count() or 2)
    ap.add_argument("--minutes", type=float, default=10)
    ap.add_argument("--variant", choices=sorted(VARIANTS), default="minishogi")
    ap.add_argument("--out", help="default: data/problems/<the variant's file>")
    a = ap.parse_args()
    global CFG, VARIANT
    CFG, VARIANT = VARIANTS[a.variant], a.variant
    a.out = a.out or os.path.join("data/problems", CFG["file"])
    deadline = time.time() + a.minutes * 60
    with multiprocessing.Pool(a.jobs) as pool:
        results = pool.map(work, [(a.engine, a.seed * 1000 + j, a.per, deadline, a.variant) for j in range(a.jobs)])
    by = {k: {} for k in PLIES}
    for r in results:
        for k, sfen, pv in r:
            by[k].setdefault(sfen, pv)
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w") as f:
        for k in PLIES:
            keep = list(by[k].items())[: a.per]
            print("%d plies: %d found, %d kept" % (k, len(by[k]), len(keep)), file=sys.stderr)
            for sfen, pv in keep:
                f.write("%d %s\n" % (k, sfen))


if __name__ == "__main__":
    main()
