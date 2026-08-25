#!/usr/bin/env python3
# v2 - user-lens detectors:
#  A) oversized idle garrison: units parked at our HQ far beyond the actual nearby threat
#  B) relay attacks: arrivals at enemy HQ tile spread out instead of one wave
#  C) retreat episodes: a forward unit walks back to home (multi-hop) w/o a real HQ threat
#  D) non-HQ base upgrades with context
import sys, os
from collections import deque, defaultdict

def bfs(adj, src):
    d = {src: 0}; q = deque([src])
    while q:
        u = q.popleft()
        for v in adj[u]:
            if v not in d: d[v] = d[u]+1; q.append(v)
    return d

def analyze(fn, us):
    L = open(fn, encoding="utf-8", errors="replace").read().splitlines()
    i = 0
    while L[i] != "MAP": i += 1
    N, K = map(int, L[i+1].split())
    adj = [list(map(int, L[i+5+r].split()))[1:] for r in range(N)]
    hq = {"A": 0, "B": N-1}
    them = "B" if us == "A" else "A"
    d_my = bfs(adj, hq[us]); d_op = bfs(adj, hq[them])

    pos, hp = {}, {}
    lvl = {"A":1,"B":1}
    for s in "AB":
        for k in range(1,4): pos[f"{s}{k}"]=hq[s]; hp[f"{s}{k}"]=4
    turn=0; blk=False; result=""
    my_bases=set(); upg=[]
    siege_on_us=set()   # turns when one of OUR bases takes siege
    arrivals=defaultdict(int)
    hist=[]             # per turn: (turn, athq, e3, my_alive)
    trace=defaultdict(list)  # warrior -> list of (turn, hops_to_myhq)
    for ln in L[i:]:
        t=ln.split()
        if not t: continue
        if t[0]=="TURN" and len(t)==2 and t[1].isdigit(): turn=int(t[1]); continue
        if t[0]=="COMMAND": blk=(t[2]=="START"); continue
        if blk: continue
        if t[0]=="TRAIN":
            for w in t[1:]:
                s=w[0]; pos[w]=hq[s]; hp[w]=3+lvl[s]
        elif t[0]=="MOVE" and len(t)==3:
            w=t[1]; pos[w]=int(t[2])
            if w[0]==us and int(t[2])==hq[them]: arrivals[turn]+=1
        elif t[0]=="UPGRADE" and len(t)==3:
            s,r=t[1],int(t[2])
            if s==us and r!=hq[us]:
                if r in my_bases:
                    ma=sum(1 for w in pos if w[0]==us); ea=sum(1 for w in pos if w[0]==them)
                    upg.append((turn,r,ma,ea))
                else: my_bases.add(r)
            if r==hq[s]: lvl[s]=min(5,lvl[s]+1)
        elif t[0]=="SIEGE":
            if t[1]==us and int(t[2])!=hq[us]: siege_on_us.add(turn)
        elif t[0]=="DAMAGE":
            w=t[2]; hp[w]=hp.get(w,4)-int(t[3])
            if hp[w]<=0: pos.pop(w,None); hp.pop(w,None)
        elif t[0]=="END" and t[1]=="TURN":
            e3=sum(1 for w in pos if w[0]==them and d_my.get(pos[w],99)<=3)
            athq=sum(1 for w in pos if w[0]==us and pos[w]==hq[us])
            ma=sum(1 for w in pos if w[0]==us)
            hist.append((turn,athq,e3,ma))
            for w in list(pos):
                if w[0]==us: trace[w].append((turn,d_my.get(pos[w],99)))
        elif t[0]=="RESULT": result=" ".join(t[1:])

    name=os.path.basename(fn).replace(".txt","")
    print(f"== {name} N={N} {result}")
    # A) oversized garrison: idle=athq-3 >= 4 AND idle >= 2*e3+2 (double the nearby threat)
    flags=[(t,a-3,e3) for t,a,e3,_ in hist if a-3>=4 and a-3>=2*e3+2]
    if flags:
        # summarize as windows
        wins=[]; cur=None
        for t,idle,e3 in flags:
            if cur and t==cur[1]+1: cur[1]=t; cur[2]=max(cur[2],idle); cur[3]=max(cur[3],e3)
            else:
                if cur: wins.append(cur)
                cur=[t,t,idle,e3]
        wins.append(cur)
        wins.sort(key=lambda w:-(w[1]-w[0]))
        s="; ".join(f"T{a}-{b} idle<= {i} (near-threat<={e})" for a,b,i,e in wins[:4])
        print(f"  [A garrison-idle] {len(flags)} turns: {s}")
    else: print(f"  [A garrison-idle] none")
    # during-siege subset: idle garrison while OUR bases were being razed
    fs=[(t,i) for t,i,e in flags if any(abs(t-x)<=1 for x in siege_on_us)]
    if fs: print(f"      of which WHILE our base under siege: {len(fs)} turns e.g. {' '.join(f'T{t}(idle{i})' for t,i in fs[:8])}")
    # B) relay
    if arrivals:
        ts=sorted(arrivals); tot=sum(arrivals.values()); mx=max(arrivals.values())
        s=" ".join(f"T{t}:+{arrivals[t]}" for t in ts[:16])
        print(f"  [B relay] {tot} arrivals over {len(ts)} turns (max wave {mx}): {s}{'...' if len(ts)>16 else ''}")
    else: print(f"  [B relay] never reached enemy HQ tile")
    # C) retreat episodes: hops>=3 -> hops<=1 within a 4-turn slide while HQ e3<=1 at end
    e3_at={t:e3 for t,_,e3,_ in hist}
    eps=[]
    for w,tr in trace.items():
        k=0
        while k<len(tr):
            t0,h0=tr[k]
            if h0>=3:
                for j in range(k+1,min(k+5,len(tr))):
                    t1,h1=tr[j]
                    if h1<=1 and e3_at.get(t1,0)<=1:
                        eps.append((t0,t1,w,h0)); k=j; break
            k+=1
    if eps:
        eps.sort()
        s=" ".join(f"T{a}->{b}:{w}({h}hops)" for a,b,w,h in eps[:12])
        print(f"  [C retreat-home] {len(eps)} episodes: {s}{'...' if len(eps)>12 else ''}")
    else: print(f"  [C retreat-home] none")
    # D) base upgrades
    if upg:
        s=" ".join(f"T{t}:r{r}(us{ma}vs{ea})" for t,r,ma,ea in upg[:10])
        print(f"  [D base-upgrades] {len(upg)}: {s}")
    else: print(f"  [D base-upgrades] none")

if __name__=="__main__":
    for spec in sys.argv[1:]:
        fn,us=spec.rsplit(":",1)
        analyze(fn,us)
