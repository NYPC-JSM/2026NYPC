#!/usr/bin/env python3
# Analyze battle-history/0701-3 ladder logs (20 games).
# Filename <rating><W/D/L> = OUR result (ground truth). Our SIDE varies per game:
#   W -> we are the RESULT winner side; L -> the loser side; D -> fingerprint match.
import re, sys, os, glob, statistics

DIR = sys.argv[1] if len(sys.argv) > 1 else "battle-history/0701-3"

def parse(fn):
    lines = open(fn, encoding="utf-8", errors="replace").read().splitlines()
    g = {"file": os.path.basename(fn)}
    m = re.match(r"(\d+)(?:-\d+)?([WDLwdl])", g["file"])
    g["rating"], g["res_letter"] = int(m.group(1)), m.group(2).upper()
    i = 0
    while lines[i] != "MAP": i += 1
    N, K = map(int, lines[i+1].split())
    g["N"], g["K"] = N, K
    hq = {"A": 0, "B": N-1}
    ev = {s: dict(trains=0, hq_up=[], base_up={},  # region -> [turns]
                  siege_dealt_base=[], siege_dealt_hq=[],   # (turn, region, amt)
                  siege_recv_base=[], siege_recv_hq=[],
                  hunger=0, min_tokens=5, max_time=0,
                  cmd_empty_turns=0, cmd_turns=0, moves_cmd=0,
                  t1_moves=0, first_train_id=None, max_train_id=0)
          for s in "AB"}
    turn = 0
    cmd_side = None; cmd_count = 0
    side_of = {"LEFT": "A", "RIGHT": "B"}
    for ln in lines[i:]:
        t = ln.split()
        if not t: continue
        if t[0] == "TURN" and len(t) == 2 and t[1].isdigit():
            turn = int(t[1]); continue
        if t[0] == "COMMAND":
            s = side_of[t[1]]
            if t[2] == "START":
                cmd_side, cmd_count = s, 0
            else:  # END
                ev[s]["cmd_turns"] += 1
                if cmd_count == 0: ev[s]["cmd_empty_turns"] += 1
                cmd_side = None
            continue
        if cmd_side:  # raw command block
            cmd_count += 1
            if t[0] == "MOVE":
                ev[cmd_side]["moves_cmd"] += 1
                if turn == 1: ev[cmd_side]["t1_moves"] += 1
            continue
        # result events
        if t[0] == "TIME":
            # TIME LEFT <ms> <tok> RIGHT <ms> <tok>
            ev["A"]["max_time"] = max(ev["A"]["max_time"], int(t[2]))
            ev["A"]["min_tokens"] = min(ev["A"]["min_tokens"], int(t[3]))
            ev["B"]["max_time"] = max(ev["B"]["max_time"], int(t[5]))
            ev["B"]["min_tokens"] = min(ev["B"]["min_tokens"], int(t[6]))
        elif t[0] == "TRAIN":                      # TRAIN A12 [A13 B7 ...] (one line may carry both sides)
            for w in t[1:]:
                s, num = w[0], int(w[1:])
                ev[s]["trains"] += 1
                ev[s]["max_train_id"] = max(ev[s]["max_train_id"], num)
        elif t[0] == "UPGRADE" and len(t) == 3:    # UPGRADE A 2
            s, r = t[1], int(t[2])
            if r == hq[s]: ev[s]["hq_up"].append(turn)
            else: ev[s]["base_up"].setdefault(r, []).append(turn)
        elif t[0] == "SIEGE":                      # SIEGE A 0 1  (A's bldg at 0 took 1)
            owner, r, amt = t[1], int(t[2]), int(t[3])
            att = "B" if owner == "A" else "A"
            key = "hq" if r == hq[owner] else "base"
            ev[att]["siege_dealt_"+key].append((turn, r, amt))
            ev[owner]["siege_recv_"+key].append((turn, r, amt))
        elif t[0] == "DAMAGE" and t[1] == "HUNGER":
            ev[t[2][0]]["hunger"] += 1
        elif t[0] == "RESULT":
            g["result"] = " ".join(t[1:])
    g["end_turn"] = turn
    g["ev"] = ev
    return g

def profile(e):
    """features used to fingerprint OUR bot in draws"""
    return dict(
        first_hq_up = e["hq_up"][0] if e["hq_up"] else 999,
        hq_up_n     = len(e["hq_up"]),
        first_siege = min([t for t,_,_ in e["siege_dealt_base"]+e["siege_dealt_hq"]] or [999]),
        trains      = e["trains"],
        bases       = len(e["base_up"]),
        t1_moves    = e["t1_moves"],
        min_tok     = e["min_tokens"],
    )

games = [parse(f) for f in sorted(glob.glob(os.path.join(DIR, "*.txt")))]

# --- determine our side ---
for g in games:
    r = g["result"]
    if g["res_letter"] == "W":
        g["us"] = "A" if r.startswith("LEFT_WIN") else "B"
    elif g["res_letter"] == "L":
        g["us"] = "B" if r.startswith("LEFT_WIN") else "A"
    else:
        g["us"] = None  # fingerprint below

known = [g for g in games if g["us"]]
ours  = [profile(g["ev"][g["us"]]) for g in known]
theirs= [profile(g["ev"]["B" if g["us"]=="A" else "A"]) for g in known]

def med(ps, k): return statistics.median(p[k] for p in ps)
keys = ["first_hq_up","t1_moves","first_siege"]
our_med = {k: med(ours,k) for k in keys}

print("=== our-bot fingerprint (median over 16 known games) ===")
print("ours  :", {k: med(ours,k) for k in ["first_hq_up","t1_moves","first_siege","trains","bases"]})
print("theirs:", {k: med(theirs,k) for k in ["first_hq_up","t1_moves","first_siege","trains","bases"]})
print("our t1_moves values:", sorted(p["t1_moves"] for p in ours))
print("opp t1_moves values:", sorted(p["t1_moves"] for p in theirs))
print("our first_hq_up:", sorted(p["first_hq_up"] for p in ours))
print("opp first_hq_up:", sorted(p["first_hq_up"] for p in theirs))

for g in games:
    if g["us"]: continue
    pa, pb = profile(g["ev"]["A"]), profile(g["ev"]["B"])
    def dist(p): return sum(abs(p[k]-our_med[k]) for k in keys)
    g["us"] = "A" if dist(pa) <= dist(pb) else "B"
    g["fp"] = f'A-dist={dist(pa):.0f} B-dist={dist(pb):.0f}'

# --- per-game table ---
print("\n=== per-game metrics (us vs them) ===")
hdr = ("game", "N", "side", "end", "result",
       "HQup(us/th)", "trains(us/th)", "bases(us/th)",
       "raidsON-us", "raids-BY-us", "HQsiege(us->th/th->us)", "1st-raid(us/th)", "hunger(us/th)", "mintok(us/th)", "emptycmd")
print(("{:>7} {:>3} {:>4} {:>4} {:<22} " + "{:>11} {:>13} {:>12} {:>10} {:>11} {:>22} {:>15} {:>13} {:>13} {:>8}").format(*hdr))
tot = dict(W=0, D=0, L=0)
for g in games:
    us = g["us"]; th = "B" if us == "A" else "A"
    e, o = g["ev"][us], g["ev"][th]
    fs = lambda ee: min([t for t,_,_ in ee["siege_dealt_base"]+ee["siege_dealt_hq"]] or [0])
    row = (g["file"].replace(".txt",""), g["N"], "LEFT" if us=="A" else "RGHT", g["end_turn"], g["result"],
           f'{len(e["hq_up"])}/{len(o["hq_up"])}',
           f'{e["trains"]}/{o["trains"]}',
           f'{len(e["base_up"])}/{len(o["base_up"])}',
           f'{len(e["siege_recv_base"])}',
           f'{len(e["siege_dealt_base"])}',
           f'{sum(a for _,_,a in e["siege_dealt_hq"])}/{sum(a for _,_,a in o["siege_dealt_hq"])}',
           f'{fs(e)}/{fs(o)}',
           f'{e["hunger"]}/{o["hunger"]}',
           f'{e["min_tokens"]}/{o["min_tokens"]}',
           f'{e["cmd_empty_turns"]}')
    print(("{:>7} {:>3} {:>4} {:>4} {:<22} " + "{:>11} {:>13} {:>12} {:>10} {:>11} {:>22} {:>15} {:>13} {:>13} {:>8}").format(*row))
    tot[g["res_letter"]] += 1
print(f'\nTOTAL: {tot["W"]}W/{tot["D"]}D/{tot["L"]}L')
for g in games:
    if "fp" in g:
        print(f'  draw side-ID {g["file"]}: chose {"LEFT" if g["us"]=="A" else "RIGHT"} ({g["fp"]})')
