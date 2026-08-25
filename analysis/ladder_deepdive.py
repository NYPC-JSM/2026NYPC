#!/usr/bin/env python3
# Deep-dive selected ladder games: timeline buckets, base-raze timeline, hunger/stall,
# endgame muster at the enemy HQ (exact alive tracking via judge damage events).
import re, sys, os
from collections import defaultdict, deque

def bfs(adj, src):
    d = {src: 0}; q = deque([src])
    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in d: d[v] = d[u]+1; q.append(v)
    return d

def analyze(fn, us):
    lines = open(fn, encoding="utf-8", errors="replace").read().splitlines()
    i = 0
    while lines[i] != "MAP": i += 1
    N, K = map(int, lines[i+1].split())
    j = i+5  # adjacency lines start after MAP, "N K", coords x2, STRONGHOLDS
    adj = []
    for r in range(N):
        t = list(map(int, lines[j+r].split()))
        adj.append(t[1:])
    hq = {"A": 0, "B": N-1}
    them = "B" if us == "A" else "A"
    hops_to_ehq = bfs(adj, hq[them])   # distance to ENEMY hq (for our muster)
    hops_to_ohq = bfs(adj, hq[us])     # distance to OUR hq (for rush detection)

    lvl = {"A": 1, "B": 1}
    pos, hp = {}, {}
    for s in "AB":
        for k in range(1, 4):
            w = f"{s}{k}"; pos[w] = hq[s]; hp[w] = 4
    turn = 0
    trains = defaultdict(lambda: [0, 0])       # bucket -> [us, them]
    sieges = defaultdict(lambda: [0, 0])       # bucket -> [by-us amt, on-us amt]
    hq_up = {"A": [], "B": []}
    base_up_first = {"A": {}, "B": {}}
    raid_on_us = defaultdict(list)             # region -> [(turn, amt)]
    hunger_turns = []
    empty = {"A": [], "B": []}                 # turns with empty cmd block
    cmd_side = None; cmd_count = 0
    side_of = {"LEFT": "A", "RIGHT": "B"}
    muster = []                                # (turn, at_tile, within2, alive_us, alive_th, our_fwd)
    rushed = []                                # (turn, enemy within2 of OUR hq)
    result = ""
    for ln in lines[i:]:
        t = ln.split()
        if not t: continue
        if t[0] == "TURN" and len(t) == 2 and t[1].isdigit():
            turn = int(t[1]); continue
        if t[0] == "COMMAND":
            s = side_of[t[1]]
            if t[2] == "START": cmd_side, cmd_count = s, 0
            else:
                if cmd_count == 0: empty[s].append(turn)
                cmd_side = None
            continue
        if cmd_side: cmd_count += 1; continue
        b = (turn-1)//20
        if t[0] == "TRAIN":                        # one line may carry several warriors from both sides
            for w in t[1:]:
                s = w[0]
                pos[w] = hq[s]; hp[w] = 3 + lvl[s]
                trains[b][0 if s == us else 1] += 1
        elif t[0] == "MOVE" and len(t) == 3:
            pos[t[1]] = int(t[2])
        elif t[0] == "UPGRADE" and len(t) == 3:
            s, r = t[1], int(t[2])
            if r == hq[s]:
                hq_up[s].append(turn)
                if len(hq_up[s]) <= 4: lvl[s] = min(5, lvl[s]+1)
            else:
                base_up_first[s].setdefault(r, turn)
        elif t[0] == "SIEGE":
            owner, r, amt = t[1], int(t[2]), int(t[3])
            if owner == us:
                sieges[b][1] += amt
                if r != hq[us]: raid_on_us[r].append((turn, amt))
            else:
                sieges[b][0] += amt
        elif t[0] == "DAMAGE":
            w = t[2]; amt = int(t[3])
            hp[w] = hp.get(w, 4) - amt
            if hp[w] <= 0: pos.pop(w, None); hp.pop(w, None)
            if t[1] == "HUNGER" and w[0] == us: hunger_turns.append(turn)
        elif t[0] == "END" and t[1] == "TURN":
            alive_us = [w for w in pos if w[0] == us]
            alive_th = [w for w in pos if w[0] == them]
            at = sum(1 for w in alive_us if pos[w] == hq[them])
            w2 = sum(1 for w in alive_us if hops_to_ehq.get(pos[w], 99) <= 2)
            fwd = sum(1 for w in alive_us if hops_to_ehq.get(pos[w], 99) <= 4)
            muster.append((turn, at, w2, fwd, len(alive_us), len(alive_th)))
            er = sum(1 for w in alive_th if hops_to_ohq.get(pos[w], 99) <= 2)
            rushed.append((turn, er))
        elif t[0] == "RESULT":
            result = " ".join(t[1:])

    name = os.path.basename(fn)
    print(f"\n{'='*100}\n== {name}  N={N} K={K}  us={'LEFT' if us=='A' else 'RIGHT'}  {result} @T{turn}")
    print(f"HQ-up turns  us: {hq_up[us]}  (~L{min(1+len(hq_up[us]),5)})   them: {hq_up[them]}  (~L{min(1+len(hq_up[them]),5)})")
    print(f"bases built  us: {len(base_up_first[us])} {sorted(base_up_first[us].items(), key=lambda x:x[1])}")
    print(f"             th: {len(base_up_first[them])}")
    bks = sorted(set(list(trains.keys())+list(sieges.keys())))
    print("bucket(T)      : " + " ".join(f"{20*b+1:>3}-{20*b+20:<3}" for b in bks))
    print("trains us/them : " + " ".join(f"{trains[b][0]:>3}/{trains[b][1]:<3}" for b in bks))
    print("siege by/on us : " + " ".join(f"{sieges[b][0]:>3}/{sieges[b][1]:<3}" for b in bks))
    if raid_on_us:
        rr = sorted(raid_on_us.items(), key=lambda kv: kv[1][0][0])
        print("raids on our bases: " + "; ".join(f"r{r}@T{v[0][0]}-{v[-1][0]}({sum(a for _,a in v)}dmg)" for r, v in rr))
    if hunger_turns:
        print(f"HUNGER us: {len(hunger_turns)} events T{min(hunger_turns)}-{max(hunger_turns)}")
    ee = empty[us]
    if ee:
        streaks, s0, prev = [], ee[0], ee[0]
        for x in ee[1:]:
            if x == prev+1: prev = x
            else: streaks.append((s0, prev)); s0 = prev = x
        streaks.append((s0, prev))
        ls = max(streaks, key=lambda ab: ab[1]-ab[0])
        print(f"empty-cmd turns us: {len(ee)}, longest streak T{ls[0]}-T{ls[1]} ({ls[1]-ls[0]+1})")
    # endgame muster
    late = [m for m in muster if m[0] >= 120]
    if late:
        mx_at  = max(late, key=lambda m: m[1])
        mx_w2  = max(late, key=lambda m: m[2])
        print(f"endgame muster (T>=120): max at-enemy-HQ-tile {mx_at[1]} @T{mx_at[0]}; max within-2hops {mx_w2[2]} @T{mx_w2[0]}")
        for tt in (140, 170, 195):
            m = next((m for m in muster if m[0] == tt), None)
            if m: print(f"  T{tt}: atHQ={m[1]} within2={m[2]} within4={m[3]} alive us/them={m[4]}/{m[5]}")
    er_max = max(rushed, key=lambda x: x[1])
    first_rush = next(((t, e) for t, e in rushed if e >= 3), None)
    print(f"enemy near OUR HQ (<=2 hops): max {er_max[1]} @T{er_max[0]}; first >=3: {first_rush}")

if __name__ == "__main__":
    for spec in sys.argv[1:]:
        fn, us = spec.split(":")
        analyze(fn, us)
