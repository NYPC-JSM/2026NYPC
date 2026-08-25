// NYPC battle bot — C++
// Base infrastructure (state tracking, path calc, I/O) is taken from the
// official sample. Strategy lives in `decide()` at the bottom (v1: economy +
// expansion + light defense).
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <sstream>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

constexpr int MAX_TURN = 200;         // maximum turn (days)
constexpr int START_GOLD = 500;       // initial gold
constexpr int START_WARRIORS = 3;     // initial warriors
constexpr int MOVE_COST = 10;         // move cost
constexpr int TRAIN_COST = 120;       // train cost
constexpr int WORK_INCOME = 15;       // income per warrior
constexpr int UPKEEP_PER_WARRIOR = 2; // upkeep per warrior
constexpr int HQ_MAX_LEVEL = 5;       // HQ max level
constexpr int BASE_MAX_LEVEL = 3;     // base max level
constexpr int HQ_HEAL_COST = 1000;    // HQ fix cost
constexpr int BASE_HEAL_COST = 500;   // base fix cost

struct HqLevelEntry {
  int upgrade_cost;
  int warrior_hp;
  int hp;
  int turret;
  int train_cap;
  int work_cap;
};

struct BaseLevelEntry {
  int cost;
  int hp;
  int turret;
  int work_cap;
};

constexpr HqLevelEntry HQ_LEVELS[HQ_MAX_LEVEL + 1] = {
    {0, 0, 0, 0, 0, 0},     {0, 4, 10, 1, 1, 1},    {600, 5, 15, 2, 1, 2},
    {1200, 6, 20, 2, 2, 3}, {2400, 7, 25, 3, 2, 4}, {3600, 8, 30, 3, 3, 5},
};
constexpr BaseLevelEntry BASE_LEVELS[BASE_MAX_LEVEL + 1] = {
    {0, 0, 0, 0},
    {300, 6, 1, 1},
    {600, 12, 1, 2},
    {1000, 18, 2, 3},
};

enum class Side : int { LEFT = 0, RIGHT = 1 };
enum class BType : int { HQ, BASE };
enum class WState : int { STATIONARY, MOVING };

inline Side opposite(Side s) {
  return s == Side::LEFT ? Side::RIGHT : Side::LEFT;
}
inline char side_char(Side s) { return s == Side::LEFT ? 'A' : 'B'; }
inline Side parse_side_char(char c) {
  return c == 'A' ? Side::LEFT : Side::RIGHT;
}

struct WarriorId {
  Side side = Side::LEFT;
  int num = 0;
  bool operator==(const WarriorId &o) const {
    return side == o.side && num == o.num;
  }
};

struct Warrior {
  WarriorId id;
  int region = 0;
  int hp = 0;
  WState state = WState::STATIONARY;
  int target = 0;
};

struct Building {
  int region = 0;
  Side side = Side::LEFT;
  BType type = BType::HQ;
  int level = 1;
  int hp = 10;

  int current_hp() const {
    return type == BType::HQ ? HQ_LEVELS[level].hp : BASE_LEVELS[level].hp;
  }
  int work_cap() const {
    return type == BType::HQ ? HQ_LEVELS[level].work_cap
                             : BASE_LEVELS[level].work_cap;
  }
};

struct GameMap {
  int N = 0, K = 0;
  std::vector<long long> x, y;
  std::vector<int> strongholds;
  std::vector<std::vector<int>> adj;

  Side my_side = Side::LEFT;
  int my_hq = 0;
  int opp_hq = 0;
};

struct GameState {
  int gold = START_GOLD; // current gold
  int my_countdown = 5;  // my remaining countdowns
  int opp_countdown = 5; // opponent's remaining countdowns
  std::vector<Warrior> warriors;
  std::vector<Building> buildings;
};

struct Actions {
  int train_n = 0;
  std::vector<std::pair<WarriorId, int>> moves;
  std::vector<int> upgrades;
};

static std::string readln() {
  std::string s;
  if (!std::getline(std::cin, s))
    std::exit(0);
  return s;
}

static std::vector<std::string> tokens(const std::string &s) {
  std::vector<std::string> out;
  std::istringstream is(s);
  for (std::string t; is >> t;)
    out.push_back(t);
  return out;
}

static WarriorId parse_warrior(const std::string &tok) {
  assert(!tok.empty() && (tok[0] == 'A' || tok[0] == 'B'));
  WarriorId id;
  id.side = parse_side_char(tok[0]);
  id.num = std::stoi(tok.substr(1));
  return id;
}

static std::string format_warrior(WarriorId id) {
  std::string s;
  s.push_back(side_char(id.side));
  s += std::to_string(id.num);
  return s;
}

static int hq_of(const GameMap &M, Side s) {
  return (s == Side::LEFT) ? 0 : M.N - 1;
}

static Building make_base(int region, Side s) {
  return Building{region, s, BType::BASE, 1, BASE_LEVELS[1].hp};
}

static void apply_upgrade(Building &b) {
  b.level += 1;
  b.hp = b.current_hp();
}

static int upgrade_cost(const Building &b) {
  if (b.type == BType::HQ)
    return HQ_LEVELS[b.level + 1].upgrade_cost;
  else
    return BASE_LEVELS[b.level + 1].cost;
}

static int max_level(const Building &b) {
  return b.type == BType::HQ ? HQ_MAX_LEVEL : BASE_MAX_LEVEL;
}

static void parse_init(GameMap &M, GameState &S) {
  {
    auto t = tokens(readln());
    assert(t.size() >= 2 && t[0] == "READY");
    M.my_side = (t[1] == "LEFT") ? Side::LEFT : Side::RIGHT;
  }
  {
    auto t = tokens(readln());
    M.N = std::stoi(t.at(0));
    M.K = std::stoi(t.at(1));
  }
  M.x.assign(M.N, 0);
  M.y.assign(M.N, 0);
  {
    auto t = tokens(readln()); // x_0 x_1 ... x_{N-1}
    for (int i = 0; i < M.N; ++i)
      M.x[i] = std::stoll(t.at(i));
  }
  {
    auto t = tokens(readln()); // y_0 y_1 ... y_{N-1}
    for (int i = 0; i < M.N; ++i)
      M.y[i] = std::stoll(t.at(i));
  }
  {
    auto t = tokens(readln()); // K strongholds
    M.strongholds.clear();
    M.strongholds.reserve(t.size());
    for (const auto &s : t)
      M.strongholds.push_back(std::stoi(s));
    std::sort(M.strongholds.begin(), M.strongholds.end());
  }
  M.adj.assign(M.N, {});
  for (int r = 0; r < M.N; ++r) {
    auto t = tokens(readln()); // deg n_1 n_2 ...
    int deg = std::stoi(t.at(0));
    auto &nb = M.adj[r];
    nb.reserve(deg);
    for (int j = 0; j < deg; ++j)
      nb.push_back(std::stoi(t.at(1 + j)));
    std::sort(nb.begin(), nb.end());
  }

  M.my_hq = hq_of(M, M.my_side);
  M.opp_hq = hq_of(M, opposite(M.my_side));

  S = GameState{};
  S.gold = START_GOLD;
  Side opp = opposite(M.my_side);
  for (int sfx = 1; sfx <= START_WARRIORS; ++sfx) {
    S.warriors.push_back(Warrior{.id = WarriorId{M.my_side, sfx},
                                 .region = M.my_hq,
                                 .hp = HQ_LEVELS[1].warrior_hp});
    S.warriors.push_back(Warrior{.id = WarriorId{opp, sfx},
                                 .region = M.opp_hq,
                                 .hp = HQ_LEVELS[1].warrior_hp});
  }
  S.buildings.push_back(Building{hq_of(M, Side::LEFT), Side::LEFT, BType::HQ, 1,
                                 HQ_LEVELS[1].hp});
  S.buildings.push_back(Building{hq_of(M, Side::RIGHT), Side::RIGHT, BType::HQ,
                                 1, HQ_LEVELS[1].hp});

  std::cout << "OK" << std::endl;
}

static bool read_turn_start(int &turn_index) {
  std::string line = readln();
  if (line == "FINISH")
    return false;
  auto t = tokens(line);
  assert(!t.empty() && t[0] == "START");
  turn_index = std::stoi(t.at(2));
  return true;
}

static Building *find_building(GameState &S, int region) {
  for (auto &b : S.buildings)
    if (b.region == region)
      return &b;
  return nullptr;
}

static Warrior *find_warrior(GameState &S, WarriorId id) {
  for (auto &w : S.warriors)
    if (w.id == id)
      return &w;
  return nullptr;
}

static void read_turn_result(GameState &S, const GameMap &M,
                             const Actions &submitted) {
  for (int region : submitted.upgrades) {
    Building *b = find_building(S, region);
    if (b == nullptr) {
      S.gold -= BASE_LEVELS[1].cost;
      S.buildings.push_back(make_base(region, M.my_side));
    } else {
      if (b->level >= max_level(*b)) {
        int cost = (b->type == BType::HQ) ? HQ_HEAL_COST : BASE_HEAL_COST;
        S.gold -= cost;
        b->hp = b->current_hp();
      } else {
        S.gold -= upgrade_cost(*b);
        apply_upgrade(*b);
      }
    }
  }

  for (const auto &[id, target] : submitted.moves) {
    Building *b = find_building(S, target);
    int cost = (b != nullptr && b->side == M.my_side) ? 0 : MOVE_COST;
    S.gold -= cost;
    if (Warrior *w = find_warrior(S, id)) {
      w->state = WState::MOVING;
      w->target = target;
    }
  }

  S.gold -= TRAIN_COST * submitted.train_n;

  {
    std::string line = readln();
    if (line == "FINISH")
      std::exit(0);
    auto t = tokens(line);
    assert(!t.empty() && t[0] == "TURN");
  }
  {
    auto t = tokens(readln());
    S.my_countdown = std::stoi(t.at(2));
    S.opp_countdown = std::stoi(t.at(4));
  }
  // UPGRADE
  {
    auto t = tokens(readln()); // "UPGRADE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln()); // "<A|B> <region>"
      Side s = parse_side_char(r.at(0)[0]);
      int region = std::stoi(r.at(1));
      Building *b = find_building(S, region);
      if (b == nullptr) {
        S.buildings.push_back(make_base(region, s));
      } else if (b->side != M.my_side) {
        if (b->level >= max_level(*b)) {
          b->hp = b->current_hp();
        } else {
          apply_upgrade(*b);
        }
      }
    }
  }
  // TRAIN
  {
    auto t = tokens(readln()); // "TRAIN N"
    int n = std::stoi(t.at(1));
    if (n > 0) {
      auto ids = tokens(readln());
      for (int i = 0; i < n; ++i) {
        WarriorId id = parse_warrior(ids.at(i));
        int hq_region = hq_of(M, id.side);
        Building *hq_b = find_building(S, hq_region);
        int hq_level = (hq_b != nullptr) ? hq_b->level : 1;
        S.warriors.push_back(Warrior{.id = id,
                                     .region = hq_region,
                                     .hp = HQ_LEVELS[hq_level].warrior_hp});
      }
    }
  }
  // MOVE
  {
    auto t = tokens(readln()); // "MOVE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln());
      WarriorId id = parse_warrior(r.at(0));
      int region = std::stoi(r.at(1));
      if (Warrior *w = find_warrior(S, id)) {
        w->region = region;
        if (id.side == M.my_side && w->state == WState::MOVING &&
            w->region == w->target) {
          w->state = WState::STATIONARY;
        }
      }
    }
  }
  // DAMAGE
  {
    auto t = tokens(readln()); // "DAMAGE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln());
      WarriorId id = parse_warrior(r.at(1));
      int damage = std::stoi(r.at(2));
      if (Warrior *w = find_warrior(S, id))
        w->hp -= damage;
    }
    S.warriors.erase(std::remove_if(S.warriors.begin(), S.warriors.end(),
                                    [](const Warrior &w) { return w.hp <= 0; }),
                     S.warriors.end());
  }
  // SIEGE
  {
    auto t = tokens(readln()); // "SIEGE N"
    int n = std::stoi(t.at(1));
    for (int i = 0; i < n; ++i) {
      auto r = tokens(readln());
      int region = std::stoi(r.at(1));
      int damage = std::stoi(r.at(2));
      if (Building *b = find_building(S, region))
        b->hp -= damage;
    }
    S.buildings.erase(
        std::remove_if(S.buildings.begin(), S.buildings.end(),
                       [](const Building &b) { return b.hp <= 0; }),
        S.buildings.end());
  }
  (void)readln(); // "END"

  int income = 0;
  for (const auto &b : S.buildings) {
    if (b.side != M.my_side)
      continue;
    int count = 0;
    for (const auto &w : S.warriors) {
      if (w.id.side == M.my_side && w.region == b.region)
        ++count;
    }
    income += WORK_INCOME * std::min(count, b.work_cap());
  }
  S.gold += income;

  int alive = 0;
  for (const auto &w : S.warriors)
    if (w.id.side == M.my_side)
      ++alive;
  S.gold = std::max(0, S.gold - UPKEEP_PER_WARRIOR * alive);
}

struct Paths {
  std::vector<std::vector<double>> dist;
  std::vector<std::vector<int>> nxt;
};

static double euclid_ceil(const GameMap &M, int u, int v) {
  double dx = (double)(M.x[u] - M.x[v]);
  double dy = (double)(M.y[u] - M.y[v]);
  return std::ceil(std::sqrt(dx * dx + dy * dy));
}

static Paths calculate_paths(const GameMap &M) {
  const double INF = std::numeric_limits<double>::infinity();
  Paths P;
  P.dist.assign(M.N, std::vector<double>(M.N, INF));
  P.nxt.assign(M.N, std::vector<int>(M.N, -1));

  for (int i = 0; i < M.N; ++i) {
    P.dist[i][i] = 0.0;
    P.nxt[i][i] = i;
  }
  for (int u = 0; u < M.N; ++u) {
    for (int v : M.adj[u]) {
      double w = euclid_ceil(M, u, v);
      if (w < P.dist[u][v])
        P.dist[u][v] = w;
    }
  }

  for (int k = 0; k < M.N; ++k) {
    for (int u = 0; u < M.N; ++u) {
      if (P.dist[u][k] == INF)
        continue;
      for (int v = 0; v < M.N; ++v) {
        double cand = P.dist[u][k] + P.dist[k][v];
        if (cand < P.dist[u][v])
          P.dist[u][v] = cand;
      }
    }
  }

  for (int u = 0; u < M.N; ++u) {
    for (int v = 0; v < M.N; ++v) {
      if (u == v || P.dist[u][v] == INF)
        continue;
      double best_score = INF;
      for (int nb : M.adj[u]) {
        if (P.dist[nb][v] == INF)
          continue;
        double score = euclid_ceil(M, u, nb) + P.dist[nb][v];
        if (score < best_score) {
          best_score = score;
          P.nxt[u][v] = nb;
        }
      }
    }
  }
  return P;
}

// Returns the next step on the path from u to v.
// If the path is not reachable, returns -1.
[[maybe_unused]] static int next_step(const Paths &P, int u, int v) {
  return P.nxt[u][v];
}

// Returns the path from u to v.
// If the path is not reachable, returns an empty vector.
[[maybe_unused]] static std::vector<int> path(const Paths &P, int u, int v) {
  std::vector<int> out;
  if (P.nxt[u][v] == -1)
    return out;
  out.push_back(u);
  while (u != v) {
    u = P.nxt[u][v];
    out.push_back(u);
  }
  return out;
}

static void emit_command() { std::cout << "COMMAND\n"; }

static void emit_actions(const Actions &a) {
  for (const auto &[id, target] : a.moves) {
    std::cout << "MOVE " << format_warrior(id) << ' ' << target << '\n';
  }
  for (int r : a.upgrades) {
    std::cout << "UPGRADE " << r << '\n';
  }
  if (a.train_n > 0) {
    std::cout << "TRAIN " << a.train_n << '\n';
  }
}

static void emit_end() { std::cout << "END" << std::endl; }

// ============================================================================
// [SIM] EXACT WORLD MODEL — replicates testing-tool.py's daily resolution.
// Every rule below carries the authoritative function/line reference. This is
// the foundation of the search bot: plans are chosen by forward-simulating
// them, so this model must match the judge EXACTLY (verified by the replay
// calibration harness in JS/debug/calib.cpp — 0 mismatches over real logs).
//
// Daily order (run_game L1352-1454):
//   phase1 per side (parse -> apply_upgrades L856 -> apply_moves L900 ->
//   apply_train_charge L924) -> apply_day_movement L962 -> spawn_trained L947
//   -> apply_day_combat L1031 -> apply_day_siege L1102 -> apply_evening_work
//   L1123 -> apply_evening_upkeep L1136 -> HQ-destroyed check.
// Key semantics (verified in source, NOT guessed):
//   - Combat caps are SNAPSHOTS of start-of-combat counts (L1033-36,1055-56):
//     effectively simultaneous. Ticks deal 1 hp each to the CURRENT lowest-hp
//     enemy in the region (tie -> smaller suffix, L1018-24). First `turret`
//     ticks of a building side are cause TURRET (L1060-66).
//   - Siege = the building-attacker side's IDLE ticks (no target left), applied
//     in the siege phase (L1092-95, L1102-20). Destroyed -> region empty.
//   - A moving unit whose CURRENT region holds an enemy (snapshot at movement-
//     phase start, L963-971) is PINNED: does not step, stays moving (L983-85).
//   - Movement step = argmin over adj of edge(u,v)+dist(v,target), tie -> first
//     in adjacency order (L991-99); with sorted adjacency this equals P.nxt.
//   - Income counts ALL hp>0 units of the owner on the building region — moving
//     units INCLUDED (L1123-33, no moving filter). work_cap per building.
//   - Upkeep: per side, units in suffix order pay 2 or take 1 HUNGER damage
//     (L1136-49); gold never goes negative from upkeep.
//   - Trained units spawn AFTER movement, BEFORE combat (L1416-18): they defend
//     the same turn but cannot move. Numbering = per-side next_suffix.
//   - Upgrades take effect in phase 1 (same-day turret/work_cap/full hp).
// ============================================================================

struct SUnit {
  int num;    // suffix (A4 -> 4)
  int side;   // 0 = LEFT, 1 = RIGHT
  int region;
  int hp;
  int target; // -1 = stationary, else move destination
};

struct SBld {
  int region;
  int side;
  int type; // 0 = HQ, 1 = BASE
  int level;
  int hp;
  int turret() const {
    return type == 0 ? HQ_LEVELS[level].turret : BASE_LEVELS[level].turret;
  }
  int wcap() const {
    return type == 0 ? HQ_LEVELS[level].work_cap : BASE_LEVELS[level].work_cap;
  }
  int max_hp() const {
    return type == 0 ? HQ_LEVELS[level].hp : BASE_LEVELS[level].hp;
  }
  int max_lvl() const { return type == 0 ? HQ_MAX_LEVEL : BASE_MAX_LEVEL; }
};

struct SOrders {
  std::vector<int> upgrades;               // regions (valid by construction)
  std::vector<std::pair<int, int>> moves;  // (unit num, target region)
  int train_n = 0;
};

// Predicted result events — filled only when a non-null pointer is passed
// (calibration); rollouts pass nullptr and skip the bookkeeping.
struct SEvents {
  std::vector<std::pair<int, int>> upgrades;          // (region, side)
  std::vector<std::pair<int, int>> spawns;            // (side, num)
  std::map<std::pair<int, int>, int> move_step;       // (side,num) -> new region
  std::map<std::tuple<int, int, int>, int> damage;    // (cause,side,num) -> dmg
                                                      // cause 0=TURRET 1=COMBAT 2=HUNGER
  std::map<int, std::pair<int, int>> siege;           // region -> (bld side, dmg)
};

struct SState {
  std::vector<SUnit> units;
  std::vector<SBld> blds;
  long long gold[2] = {START_GOLD, START_GOLD};
  int next_num[2] = {START_WARRIORS + 1, START_WARRIORS + 1};
  int day = 0;

  SBld *bld_at(int r) {
    for (auto &b : blds)
      if (b.region == r)
        return &b;
    return nullptr;
  }
  const SBld *bld_at(int r) const {
    for (const auto &b : blds)
      if (b.region == r)
        return &b;
    return nullptr;
  }
  SBld *hq_of(int side) {
    for (auto &b : blds)
      if (b.type == 0 && b.side == side)
        return &b;
    return nullptr;
  }
  SUnit *unit(int side, int num) {
    for (auto &u : units)
      if (u.side == side && u.num == num)
        return &u;
    return nullptr;
  }
};

// Initial game state (init_state L748-62): 3 warriors per side at their HQs
// (suffix 1..3, hp = HQ L1 warrior_hp), L1 HQs, 500 gold each.
[[maybe_unused]] static SState sim_initial(const GameMap &M) {
  SState st;
  int hqL = 0, hqR = M.N - 1;
  for (int sfx = 1; sfx <= START_WARRIORS; ++sfx) {
    st.units.push_back(SUnit{sfx, 0, hqL, HQ_LEVELS[1].warrior_hp, -1});
    st.units.push_back(SUnit{sfx, 1, hqR, HQ_LEVELS[1].warrior_hp, -1});
  }
  st.blds.push_back(SBld{hqL, 0, 0, 1, HQ_LEVELS[1].hp});
  st.blds.push_back(SBld{hqR, 1, 0, 1, HQ_LEVELS[1].hp});
  return st;
}

// Build SState from the bot's live tracked state (for search rollouts).
// Enemy gold is NOT observable — caller supplies the ledger estimate.
[[maybe_unused]] static SState sim_from_game(const GameState &S, const GameMap &M,
                                             long long my_gold, long long opp_gold_est) {
  SState st;
  int me = (int)M.my_side;
  st.gold[me] = my_gold;
  st.gold[1 - me] = opp_gold_est;
  int mx[2] = {0, 0};
  for (const auto &w : S.warriors) {
    int s = (int)w.id.side;
    st.units.push_back(SUnit{w.id.num, s, w.region, w.hp,
                             (w.state == WState::MOVING) ? w.target : -1});
    mx[s] = std::max(mx[s], w.id.num);
  }
  st.next_num[0] = mx[0] + 1;
  st.next_num[1] = mx[1] + 1;
  for (const auto &b : S.buildings)
    st.blds.push_back(SBld{b.region, (int)b.side,
                           b.type == BType::HQ ? 0 : 1, b.level, b.hp});
  return st;
}

// One combat tick (== _damage_tick L1011-28): -1 hp to the lowest-hp (tie:
// lowest suffix) hp>0 unit of `tside` in `region`. Returns its index or -1.
static int sim_damage_tick(SState &st, int region, int tside) {
  int best = -1;
  for (int i = 0; i < (int)st.units.size(); ++i) {
    const SUnit &u = st.units[i];
    if (u.region != region || u.side != tside || u.hp <= 0)
      continue;
    if (best < 0 || u.hp < st.units[best].hp ||
        (u.hp == st.units[best].hp && u.num < st.units[best].num))
      best = i;
  }
  if (best >= 0)
    st.units[best].hp -= 1;
  return best;
}

// Advance one full day. Orders are assumed VALID (calibration feeds judge-
// validated logs; search policies construct valid orders). Returns false if a
// HQ is gone after the day resolves (game over).
[[maybe_unused]] static bool sim_step(SState &st, const GameMap &M, const Paths &P,
                                      const SOrders ord[2], SEvents *ev) {
  // -- phase 1 per side: upgrades -> moves -> train charge (run_phase1) ------
  for (int s = 0; s < 2; ++s) {
    for (int r : ord[s].upgrades) {
      SBld *b = st.bld_at(r);
      if (b == nullptr) { // new base (apply_upgrades L872-78)
        st.gold[s] -= BASE_LEVELS[1].cost;
        st.blds.push_back(SBld{r, s, 1, 1, BASE_LEVELS[1].hp});
      } else if (b->level >= b->max_lvl()) { // heal (L883-88)
        st.gold[s] -= (b->type == 0 ? HQ_HEAL_COST : BASE_HEAL_COST);
        b->hp = b->max_hp();
      } else { // level up (L889-95)
        st.gold[s] -= (b->type == 0 ? HQ_LEVELS[b->level + 1].upgrade_cost
                                    : BASE_LEVELS[b->level + 1].cost);
        b->level += 1;
        b->hp = b->max_hp();
      }
      if (ev)
        ev->upgrades.push_back({r, s});
    }
    for (const auto &[num, tgt] : ord[s].moves) { // apply_moves L900-22
      SUnit *u = st.unit(s, num);
      if (u == nullptr)
        continue;
      const SBld *b = st.bld_at(tgt);
      st.gold[s] -= (b != nullptr && b->side == s) ? 0 : MOVE_COST;
      u->target = tgt;
    }
    st.gold[s] -= (long long)TRAIN_COST * ord[s].train_n; // L924-44
  }

  // -- movement (apply_day_movement L962-1009) -------------------------------
  // Enemy-presence PIN set is a snapshot taken before anyone steps.
  {
    std::vector<char> occ[2];
    occ[0].assign(M.N, 0);
    occ[1].assign(M.N, 0);
    for (const auto &u : st.units)
      if (u.hp > 0)
        occ[u.side][u.region] = 1;
    for (auto &u : st.units) {
      if (u.hp <= 0 || u.target < 0)
        continue;
      if (u.region == u.target) { // already there: just clear (L979-81)
        u.target = -1;
        continue;
      }
      if (occ[1 - u.side][u.region]) // pinned by enemy in CURRENT region
        continue;
      int nx = P.nxt[u.region][u.target];
      if (nx < 0)
        continue;
      u.region = nx;
      if (ev)
        ev->move_step[{u.side, u.num}] = nx;
      if (u.region == u.target)
        u.target = -1;
    }
  }

  // -- spawn trained (spawn_trained L947-59): after movement, BEFORE combat --
  for (int s = 0; s < 2; ++s) {
    if (ord[s].train_n <= 0)
      continue;
    SBld *hq = st.hq_of(s);
    if (hq == nullptr)
      continue;
    int whp = HQ_LEVELS[hq->level].warrior_hp;
    for (int i = 0; i < ord[s].train_n; ++i) {
      int num = st.next_num[s]++;
      st.units.push_back(SUnit{num, s, hq->region, whp, -1});
      if (ev)
        ev->spawns.push_back({s, num});
    }
  }

  // -- combat (apply_day_combat L1031-1100) ----------------------------------
  std::vector<int> siege_dmg(M.N, 0);
  {
    std::vector<int> cnt[2];
    cnt[0].assign(M.N, 0);
    cnt[1].assign(M.N, 0);
    for (const auto &u : st.units)
      if (u.hp > 0)
        cnt[u.side][u.region]++;
    for (int r = 0; r < M.N; ++r) { // ascending region order (L1043)
      const SBld *b = st.bld_at(r);
      int lc = cnt[0][r], rc = cnt[1][r];
      bool bl = b != nullptr && b->side == 0, br = b != nullptr && b->side == 1;
      if (!((lc > 0 || bl) && (rc > 0 || br)))
        continue;
      int tv = b != nullptr ? b->turret() : 0;
      int lcap = lc + (bl ? tv : 0), rcap = rc + (br ? tv : 0);
      int lidle = 0, ridle = 0;
      for (int i = 0; i < lcap; ++i) { // LEFT's ticks hit RIGHT units
        int cause = (bl && i < tv) ? 0 : 1;
        int idx = sim_damage_tick(st, r, 1);
        if (idx < 0)
          ++lidle;
        else if (ev) {
          auto key = std::make_tuple(cause, 1, st.units[idx].num);
          ev->damage[key] += 1;
        }
      }
      for (int i = 0; i < rcap; ++i) { // RIGHT's ticks hit LEFT units
        int cause = (br && i < tv) ? 0 : 1;
        int idx = sim_damage_tick(st, r, 0);
        if (idx < 0)
          ++ridle;
        else if (ev) {
          auto key = std::make_tuple(cause, 0, st.units[idx].num);
          ev->damage[key] += 1;
        }
      }
      if (b != nullptr) { // attacker idle -> siege (L1092-95)
        int idle = (b->side == 0) ? ridle : lidle;
        if (idle > 0)
          siege_dmg[r] = idle;
      }
    }
  }

  // -- siege (apply_day_siege L1102-20) --------------------------------------
  for (int r = 0; r < M.N; ++r) {
    if (siege_dmg[r] <= 0)
      continue;
    SBld *b = st.bld_at(r);
    if (b == nullptr)
      continue;
    int dealt = std::min(siege_dmg[r], b->hp);
    b->hp -= dealt;
    if (ev)
      ev->siege[r] = {b->side, dealt};
  }
  st.blds.erase(std::remove_if(st.blds.begin(), st.blds.end(),
                               [](const SBld &b) { return b.hp <= 0; }),
                st.blds.end());
  // combat deaths: judge keeps hp<=0 in the dict until end-of-upkeep, but every
  // later phase filters hp>0, so removing now is behavior-identical & cheaper.
  st.units.erase(std::remove_if(st.units.begin(), st.units.end(),
                                [](const SUnit &u) { return u.hp <= 0; }),
                 st.units.end());

  // -- income (apply_evening_work L1123-33): ALL owner units on the region ---
  for (const auto &b : st.blds) {
    int c = 0;
    for (const auto &u : st.units)
      if (u.side == b.side && u.region == b.region)
        ++c;
    st.gold[b.side] += (long long)WORK_INCOME * std::min(c, b.wcap());
  }

  // -- upkeep + hunger (apply_evening_upkeep L1136-53): suffix order ---------
  for (int s = 0; s < 2; ++s) {
    std::vector<int> order;
    for (int i = 0; i < (int)st.units.size(); ++i)
      if (st.units[i].side == s)
        order.push_back(i);
    std::sort(order.begin(), order.end(), [&](int a, int b2) {
      return st.units[a].num < st.units[b2].num;
    });
    for (int i : order) {
      if (st.gold[s] >= UPKEEP_PER_WARRIOR) {
        st.gold[s] -= UPKEEP_PER_WARRIOR;
      } else {
        st.units[i].hp -= 1;
        if (ev)
          ev->damage[std::make_tuple(2, s, st.units[i].num)] += 1;
      }
    }
  }
  st.units.erase(std::remove_if(st.units.begin(), st.units.end(),
                                [](const SUnit &u) { return u.hp <= 0; }),
                 st.units.end());

  st.day += 1;
  return st.hq_of(0) != nullptr && st.hq_of(1) != nullptr;
}

//////////////////////////////////
//// WRITE YOUR STRATEGY HERE ////
//////////////////////////////////

// ---- Strategy tunables (v4: expand-first economy, parallel HQ, mass-then-commit)
namespace cfg {
constexpr int RESERVE = 50;          // base gold cushion
constexpr int BASE_BUILD_COST = 300; // base build price
constexpr int EXPAND_CAP = 10;       // grab all reachable strongholds (don't cede economy)
constexpr int EXPAND_PER_TURN = 3;   // expansion movers launched per turn
constexpr int DEFEND_HOPS = 3;       // enemy within this many hops of HQ = threat
constexpr int GARRISON_EXTRA = 2;    // keep this many defenders over incoming
constexpr int MIN_GARRISON = 1;      // home guard when safe (recruits defend too)
constexpr int ARMY_PER_WORKCAP = 6;  // sustainable army ≈ workcap * this
constexpr int MIN_ARMY = 6;          // floor on the army-size target
constexpr int STRIKE_FORCE = 8;      // mobile raiders kept during growth (once HQ≥L3)
                                     // so the guerilla-raid logic actually fires
constexpr int RAID_START_TURN = 50;  // establish economy first; don't raid too early
constexpr int MIN_DEF = 4;           // always be able to train up to this many
constexpr int ATTACK_MARGIN = 4;     // mass needed OVER the enemy's TOTAL army (commit decisively)
constexpr int MIN_PUSH = 6;          // ...and a minimum wave size
constexpr int RALLY_FRAC = 70;       // % of the wave that must gather before it marches
constexpr int LATE_TURN = 175;       // endgame all-in if losing the hp tiebreak
constexpr int HQ_SOFT_TARGET = 3;    // keep the mobile STRIKE_FORCE from HQ L3 so we
                                     // ATTACK early (~T80), not at L4 (A2). HQ still
                                     // reaches L4/L5 because base-upgrade gold no
                                     // longer competes (A1 made base level-ups respect
                                     // hq_save) — we tech AND attack instead of either
                                     // stalling at L3 (v48) or attacking too late (v49)
constexpr int HQ_RICH_RESERVE = 150; // gold buffer required for HQ L4/L5 (lowered to eagerness)
constexpr int ARMY_BUFFER = 4;       // target buffer over enemy army size
// [S1] tile-fight simulator (race-based HQ commit gates — the 1250D fix, CONTEXT §8.8)
constexpr int SIM_HORIZON = 40;      // how many turns the assault sim looks ahead
constexpr int SIM_DEF_HOPS = 4;      // only enemies within this many hops of THEIR HQ count
                                     // as its defenders (a spread army isn't defending it).
                                     // Swept 3-5 clean vs the g5/1910D real states; 6 re-
                                     // inflates g5 (a cluster sits at 6 hops).
constexpr int BUILD_GUARD_HOPS = 2;  // [defect4] don't build a stronghold if enemy mobile
                                     // units within this radius outnumber ours there
constexpr int SIM_BURST = 4;         // turns the enemy retrains at FULL cap (hidden banked gold)
constexpr int SIM_NEED_CAP = 60;     // stop sizing the hypothetical wave beyond this
constexpr int SIM_MARGIN = 1;        // units added over the sim's minimal winning wave.
                                     // Keep SMALL: the sim is already worst-case (full
                                     // retrain burst + every defender converges); at 2 it
                                     // double-counted and a 5-mobile force refused to
                                     // finish a 2-hp empty HQ (judge 50456 g1/g2 slowdown)
constexpr int SIM_OUTBOUND_ETA = 6;  // extra ETA for a defender marching AWAY (move-locked)
constexpr int OPEN_SETTLE_TURN = 14; // ① big-map opening: hold 1 home + bank gold for bases through here
} // namespace cfg

// --- small read-only helpers over the tracked state ---
static bool is_stronghold(const GameMap &M, int region) {
  return std::binary_search(M.strongholds.begin(), M.strongholds.end(), region);
}

static const Building *building_at(const GameState &S, int region) {
  for (const auto &b : S.buildings)
    if (b.region == region)
      return &b;
  return nullptr;
}

static int my_count_at(const GameState &S, const GameMap &M, int region) {
  int c = 0;
  for (const auto &w : S.warriors)
    if (w.id.side == M.my_side && w.region == region)
      ++c;
  return c;
}

static int enemy_count_at(const GameState &S, const GameMap &M, int region) {
  int c = 0;
  for (const auto &w : S.warriors)
    if (w.id.side != M.my_side && w.region == region)
      ++c;
  return c;
}

static bool reachable(const Paths &P, int from, int to) {
  return from == to || P.nxt[from][to] != -1;
}

// Hop distance (in regions, = turns to walk) from src; -1 if unreachable.
static std::vector<int> bfs_hops(const GameMap &M, int src) {
  std::vector<int> d(M.N, -1);
  std::queue<int> q;
  d[src] = 0;
  q.push(src);
  while (!q.empty()) {
    int u = q.front();
    q.pop();
    for (int v : M.adj[u])
      if (d[v] < 0) {
        d[v] = d[u] + 1;
        q.push(v);
      }
  }
  return d;
}

// [defect4] Count `side`'s MOBILE field units within k hops of a stronghold — i.e. units
//   NOT standing on one of their own buildings (pinned workers/garrison don't leave to
//   fight for a different tile, and we won't sacrifice income to defend). Used to decide
//   whether a stronghold can be HELD before spending 300g to build it.
[[maybe_unused]] static int mobile_near(const GameState &S,
                                        const std::vector<int> &hops_from,
                                        int k, Side side) {
  int c = 0;
  for (const auto &w : S.warriors) {
    if (w.id.side != side)
      continue;
    int h = hops_from[w.region];
    if (h < 0 || h > k)
      continue;
    const Building *b = building_at(S, w.region);
    if (b != nullptr && b->side == side)
      continue; // on its own building = worker/garrison, not a mobile defender
    ++c;
  }
  return c;
}

// ---------------------------------------------------------------------------
// [S1] TILE-FIGHT SIMULATOR — the race half of the 1250D fix (CONTEXT §8.8).
// Sizes the HQ assault by simulating the actual fight instead of comparing
// against the enemy's TOTAL army (their spread army made every commit gate read
// false while their HQ stood at 4-7 defenders). Deterministic, rule-exact where
// the rules are known: arrivals join by hop-ETA, trained defenders spawn BEFORE
// combat, each side deals count(+turret) hits per turn lowest-hp-first, and the
// building takes LEFTOVER hits only on a turn the tile is fully cleared. Enemy
// gold is hidden: retraining = a short full-cap burst (banked gold), then the
// SUSTAINABLE rate from their visible economy.
struct SimUnit {
  int eta; // turns until it joins the fight at the target tile
  int hp;
};

static bool sim_hq_assault(const std::vector<SimUnit> &atk,
                           const std::vector<SimUnit> &def, int turret,
                           int building_hp, int regen_hp, int burst_cap,
                           int sustain_cap) {
  std::vector<int> atk_pool, def_pool; // hp of units at the tile
  int pending_atk = (int)atk.size();
  for (int t = 1; t <= cfg::SIM_HORIZON && building_hp > 0; ++t) {
    int spawn = (t <= cfg::SIM_BURST) ? burst_cap : sustain_cap;
    for (int i = 0; i < spawn; ++i)
      def_pool.push_back(regen_hp); // spawns defend the same turn (before combat)
    for (const auto &u : atk)
      if (std::max(1, u.eta) == t) {
        atk_pool.push_back(u.hp);
        --pending_atk;
      }
    for (const auto &u : def)
      if (std::max(1, u.eta) == t)
        def_pool.push_back(u.hp);
    if (atk_pool.empty()) {
      if (pending_atk == 0)
        return false; // the wave died before the kill
      continue;
    }
    std::sort(atk_pool.begin(), atk_pool.end());
    std::sort(def_pool.begin(), def_pool.end());
    // simultaneous exchange with start-of-turn counts
    int atk_hits = (int)atk_pool.size();
    int def_hits = (int)def_pool.size() + turret;
    for (auto &h : def_pool) { // chew defenders lowest-hp-first
      int d = std::min(h, atk_hits);
      h -= d;
      atk_hits -= d;
    }
    def_pool.erase(
        std::remove_if(def_pool.begin(), def_pool.end(), [](int h) { return h <= 0; }),
        def_pool.end());
    if (def_pool.empty())
      building_hp -= atk_hits; // leftover hits siege (tile cleared this turn)
    for (auto &h : atk_pool) { // their hits land on us regardless
      int d = std::min(h, def_hits);
      h -= d;
      def_hits -= d;
    }
    atk_pool.erase(
        std::remove_if(atk_pool.begin(), atk_pool.end(), [](int h) { return h <= 0; }),
        atk_pool.end());
  }
  return building_hp <= 0;
}

// Minimal wave (launched TOGETHER from eta0 hops out — waves fire from one
// staging tile, so a uniform ETA matches how they actually land) that cracks
// the enemy HQ within the horizon; SIM_NEED_CAP+1 if none up to the cap can.
// Ramp starts at 1, NOT MIN_PUSH: vs a stripped/idle enemy (0 defenders, no
// retrain) 2-3 units genuinely finish an L1 HQ — a MIN_PUSH floor here made
// need never drop below 8 and PULLED a siege party off a 2-hp HQ (judge 50456
// g1: kill T91 -> T200 clock). A healthy foe sims large anyway (retrain burst).
static int sim_hq_need(const std::vector<SimUnit> &def, int turret,
                       int building_hp, int regen_hp, int burst_cap,
                       int sustain_cap, int eta0, int my_whp) {
  for (int n = 1; n <= cfg::SIM_NEED_CAP; ++n) {
    std::vector<SimUnit> atk(n, SimUnit{eta0, my_whp});
    if (sim_hq_assault(atk, def, turret, building_hp, regen_hp, burst_cap,
                       sustain_cap))
      return n;
  }
  return cfg::SIM_NEED_CAP + 1;
}

// ============================================================================
// [SEARCH] PLAN-SEARCH STRATEGY (the "Searcher" redesign, M1).
//
// Every turn: build the exact SState from tracked state (+ the enemy-gold
// LEDGER — their gold is the only hidden info, estimated from their observed
// trains/builds/income/upkeep), generate a handful of SIMPLE candidate plans,
// roll each one forward H days with the calibrated world model (sim_step, 0
// mismatches vs real logs), score the end state, and play the best plan's
// first-day orders. Rush detection, raid-vs-assault choice and recall
// decisions all fall out of the rollout — no hand-tuned mode gates.
//
// Safety rails: a wall-clock guard (55 ms budget, ~1 rollout costs <1 ms;
// TLE = loss) and a final emit-time re-validation of every order against the
// REAL tracked state (budget prefix ≥ 0 in upgrade→move→train order, only
// STATIONARY own units moved, build/upgrade legality) so a policy bug can
// degrade play but can never WA.
// ============================================================================
#include <chrono>

namespace sb {

enum PK {
  PK_ECON = 0,
  PK_TECH,
  PK_RAID,
  PK_ASSAULT,
  PK_DEFEND,
  PK_FINISH,
  PK_PROT
};
static const char *PK_NAME[7] = {"ECON", "TECH", "RAID", "ASSLT",
                                 "DEF",  "FIN",  "PROT"};
struct Plan {
  int kind = PK_ECON;
  int target = -1; // region: RAID base / ASSAULT,FINISH enemy HQ
  bool operator==(const Plan &o) const {
    return kind == o.kind && target == o.target;
  }
};

// Per-turn precomputed context.
struct Ctx {
  int me = 0, op = 1, myhq = 0, ophq = 0;
  std::vector<int> h_my, h_op; // hop distance maps from the two HQs
  const GameMap *M = nullptr;
  const Paths *P = nullptr;
};

// ---------------------------------------------------------------------------
// Enemy observation: gold ledger + per-unit destination inference. The ledger
// ignores their MOVE costs (unobservable issue events) => slight OVERestimate
// of their gold => conservative (we size fights bigger). Heals also ignored.
struct Obs {
  long long opp_gold = START_GOLD;
  int prev_max_num = START_WARRIORS;
  std::map<int, int> prev_pos;  // enemy num -> region last turn
  std::map<int, int> prev_lvl;  // enemy building region -> level last turn
  std::map<int, int> guess;     // enemy num -> inferred move target
  // OBSERVED aggression: the most enemies ever seen simultaneously on or next
  // to our buildings. Rollouts model enemy raids at exactly this DEMONSTRATED
  // scale — 0 until they actually raid (M2e lesson: an UNconditional raid
  // model taxed every rollout with phantom packs -> paranoid over-defense ->
  // 0/4/4 collapsed to 0/0/8; model what THIS foe does, not what it could do).
  int max_raiders_seen = 0;
};
static Obs OB;
static Plan INCUMBENT;
// m2aq ASSAULT-STALL WATCHDOG state (updated once per real turn in decide()).
static int g_as_stall = 0;   // consecutive assault turns with no HQ damage
static int g_as_cool = 0;    // turns the ASSAULT candidate stays benched
static int g_prev_ophp = -1; // enemy HQ hp last turn
// m2ba RAID-STALL BENCH (the RAID twin of the watchdog): a committed target
// that yields no building damage for 8 turns past the muster hold gets
// benched from the candidate list, so score-level re-picking can't chain a
// zombie camp (h2h s100R: RAID@7 held/re-picked 70 turns, 0 siege, death).
static int g_rd_cool_tgt = -1; // benched RAID target region
static int g_rd_cool = 0;      // turns the bench lasts
// m2ba STRANGLE LEDGER (observed, cumulative): event-turns where a side's
// buildings lost hp or died. The wave hold below engages only when WE are
// being clearly out-raided (taken >= dealt + 5) — in balanced games the bot
// stays byte-identical to 78624 (the unconditional hold lost the 78624
// mirror 2W/1D/5L with 4 deaths: aggression vs a sound economy = suicide;
// vs a strangler there is nothing left to protect by staying flexible).
static std::map<int, std::pair<int, int>> g_prev_blds; // region -> (side,hp)
static int g_sieged_us = 0, g_sieged_op = 0;

static void observe(const GameState &S, const Ctx &C) {
  long long spend = 0;
  int mx = OB.prev_max_num;
  std::map<int, int> pos;
  for (const auto &w : S.warriors) {
    if ((int)w.id.side != C.op)
      continue;
    if (w.id.num > OB.prev_max_num)
      spend += TRAIN_COST; // newly trained since last turn
    mx = std::max(mx, w.id.num);
    pos[w.id.num] = w.region;
  }
  std::map<int, int> lvl;
  int opp_army = (int)pos.size(), opp_income = 0;
  for (const auto &b : S.buildings) {
    if ((int)b.side != C.op)
      continue;
    lvl[b.region] = b.level;
    auto it = OB.prev_lvl.find(b.region);
    if (it == OB.prev_lvl.end()) {
      if (b.type == BType::BASE)
        spend += BASE_LEVELS[1].cost; // new base appeared
    } else {
      for (int L = it->second + 1; L <= b.level; ++L)
        spend += (b.type == BType::HQ) ? HQ_LEVELS[L].upgrade_cost
                                       : BASE_LEVELS[L].cost;
    }
    int c = 0;
    for (const auto &w : S.warriors)
      if ((int)w.id.side == C.op && w.region == b.region)
        ++c;
    opp_income += WORK_INCOME * std::min(c, b.work_cap());
  }
  OB.opp_gold = std::max(0LL, OB.opp_gold - spend + opp_income -
                                  (long long)UPKEEP_PER_WARRIOR * opp_army);
  // destination inference from the observed step (their intent is hidden)
  for (const auto &[num, r] : pos) {
    auto pit = OB.prev_pos.find(num);
    if (pit == OB.prev_pos.end() || pit->second == r)
      continue; // new or holding: no fresh signal
    int ph = C.h_my[pit->second], nh = C.h_my[r];
    bool our_half = nh >= 0 && C.h_op[r] >= 0 && nh <= C.h_op[r];
    if (nh >= 0 && ph >= 0 && nh < ph && our_half)
      OB.guess[num] = C.myhq; // closing in INSIDE our half: assume a push.
    // (an approacher still in THEIR half is usually just settling toward the
    //  midline — reading it as a rush poisoned every economic rollout with a
    //  phantom attack and the search camped on ASSLT/DEF forever)
    else if (C.h_op[r] >= 0 && C.h_op[pit->second] > C.h_op[r])
      OB.guess[num] = C.ophq; // heading home
    else
      OB.guess.erase(num);
  }
  for (auto it = OB.guess.begin(); it != OB.guess.end();) {
    if (!pos.count(it->first))
      it = OB.guess.erase(it); // dead
    else
      ++it;
  }
  // demonstrated raid scale: enemies simultaneously on/next to our buildings
  {
    const GameMap &M = *C.M;
    int raiders = 0;
    for (const auto &w : S.warriors) {
      if ((int)w.id.side == C.me)
        continue;
      bool on_ours = false; // STRICT: standing ON our building = actually
                            // razing/contesting it. Counting mere adjacency
                            // latched on passing midline settlers and taxed
                            // every rollout early (0/2/6 vs M2c's 0/4/4).
      for (const auto &b : S.buildings) {
        if ((int)b.side != C.me)
          continue;
        if (w.region == b.region) {
          on_ours = true;
          break;
        }
      }
      if (on_ours)
        ++raiders;
    }
    (void)M;
    OB.max_raiders_seen = std::max(OB.max_raiders_seen, raiders);
  }
  OB.prev_pos.swap(pos);
  OB.prev_lvl.swap(lvl);
  OB.prev_max_num = mx;
}

// ---------------------------------------------------------------------------
// MY policy: turn one plan into orders on a SimState. Used identically inside
// rollouts and for the real emission (what we simulate is what we play).
// Spend order upgrades -> moves -> train with a local budget (prefix >= 0).
static void my_policy(const Plan &pl, const Ctx &C, const SState &st,
                      SOrders &o) {
  const GameMap &M = *C.M;
  const Paths &P = *C.P;
  const int me = C.me;
  long long bud = st.gold[me];

  int my_army = 0;
  for (const auto &u : st.units)
    if (u.side == me)
      ++my_army;
  long long food = (long long)UPKEEP_PER_WARRIOR * my_army; // never starve

  // -- classify my stationary units; pin workers up to each building's cap --
  std::vector<int> mob;                       // indices of my movable units
  std::map<int, int> pinned;                  // region -> pinned count
  for (int i = 0; i < (int)st.units.size(); ++i) {
    const SUnit &u = st.units[i];
    if (u.side != me || u.target >= 0)
      continue; // enemy or already moving (cannot be re-ordered)
    const SBld *b = st.bld_at(u.region);
    if (b != nullptr && b->side == me && pinned[u.region] < b->wcap()) {
      pinned[u.region]++; // stays as worker
      continue;
    }
    mob.push_back(i);
  }

  // -- hot set (m2ad: moved above upgrades so the build block can see it) ----
  std::vector<int> hot;
  for (const auto &b : st.blds) {
    if (b.side != me)
      continue;
    bool th = false;
    for (const auto &e : st.units) {
      if (e.side == me || e.hp <= 0)
        continue;
      if (e.region == b.region) {
        th = true;
        break;
      }
      for (int nb : M.adj[b.region])
        if (e.region == nb) {
          th = true;
          break;
        }
      if (th)
        break;
    }
    if (th)
      hot.push_back(b.region);
  }

  // -- upgrades ---------------------------------------------------------------
  std::set<int> upreg;
  long long next_hq_cost = 0;
  {
    const SBld *hq = nullptr;
    for (const auto &b : st.blds)
      if (b.side == me && b.type == 0)
        hq = &b;
    if (hq != nullptr && hq->level < HQ_MAX_LEVEL)
      next_hq_cost = HQ_LEVELS[hq->level + 1].upgrade_cost;
    // HQ level-up: any plan, whenever affordable with a food+train cushion.
    if (hq != nullptr && next_hq_cost > 0) {
      bool friendly = false, enemy_there = false; // judge: ANY unit present
      for (const auto &u : st.units)
        if (u.region == hq->region && u.hp > 0)
          (u.side == me ? friendly : enemy_there) = true;
      if (friendly && !enemy_there && bud >= next_hq_cost) {
        o.upgrades.push_back(hq->region);
        upreg.insert(hq->region);
        bud -= next_hq_cost;
        next_hq_cost = 0; // banked & spent
      }
    }
  }
  // Found a settler standing on an empty own-side stronghold -> build.
  // TECH banks the next HQ level first; FINISH doesn't build at all.
  // m2ad: a QUIET DEFEND builds too — its safe settler camps an own-side
  // stronghold and must convert it, else the g6 DEF-lock keeps 2 bases
  // forever; a landed attack (hot non-empty) still suspends building.
  if (pl.kind != PK_FINISH && (pl.kind != PK_DEFEND || hot.empty())) {
    for (int i : mob) {
      const SUnit &u = st.units[i];
      int r = u.region;
      if (!is_stronghold(M, r) || st.bld_at(r) != nullptr || upreg.count(r))
        continue;
      bool enemy_there = false;
      for (const auto &e : st.units)
        if (e.side != me && e.region == r && e.hp > 0)
          enemy_there = true;
      if (enemy_there)
        continue;
      long long gate = (pl.kind == PK_TECH) ? next_hq_cost : 0;
      if (bud < BASE_LEVELS[1].cost + gate)
        continue;
      o.upgrades.push_back(r);
      upreg.insert(r);
      bud -= BASE_LEVELS[1].cost;
    }
  }

  // -- moves ------------------------------------------------------------------
  auto try_move = [&](const SUnit &u, int dst) {
    if (dst < 0 || dst == u.region || P.nxt[u.region][dst] < 0)
      return false;
    const SBld *b = st.bld_at(dst);
    long long c = (b != nullptr && b->side == me) ? 0 : MOVE_COST;
    // No food reserve here: hunger is MODELED by the rollout, so a plan that
    // starves itself scores badly on its own. Blocking cheap moves in poverty
    // locked the bot out of the settling that ends the poverty (M1 trap).
    if (bud < c)
      return false;
    o.moves.push_back({u.num, dst});
    bud -= c;
    return true;
  };

  // -- HOT buildings: own buildings with an enemy standing on or adjacent ----
  int wave_need = -1; // m2ao: posture E's muster size, read by the train gate

  if (pl.kind == PK_DEFEND) {
    // m2ad: DEFEND was a TOTAL growth freeze (no settle, no build). In judge
    // g6 the stockpile fear sim-kills every growth plan from T21, so the bot
    // camped DEF for ~180 turns at 2 bases / HQ L1 / upkeep-saturated income
    // and lost the tiebreak — against a grower that never actually attacked.
    // While nothing is actually being hit (hot empty), keep ONE settler
    // working a SAFE own-side stronghold (enemy-free, nobody adjacent);
    // everyone else still garrisons home, and a real landed attack (hot)
    // still pulls every mobile home — the rush answer is untouched. The
    // settler compounds wcap, income outgrows upkeep, the HQ can level (its
    // block runs under every plan), and a grown army eventually breaks the
    // stockpile trigger (en_idle >= my_army) — the bootstrap out of the trap.
    int settler = -1, dst = -1;
    if (hot.empty()) {
      double bd = 1e18;
      for (int s : M.strongholds) {
        if (st.bld_at(s) != nullptr)
          continue;
        if (C.h_my[s] < 0 || (C.h_op[s] >= 0 && C.h_my[s] > C.h_op[s]))
          continue; // own side only
        bool danger = false;
        for (const auto &e : st.units) {
          if (e.side == me || e.hp <= 0)
            continue;
          if (e.region == s) {
            danger = true;
            break;
          }
          for (int nb : M.adj[s])
            if (e.region == nb) {
              danger = true;
              break;
            }
          if (danger)
            break;
        }
        if (danger)
          continue;
        for (int i : mob) {
          const SUnit &u = st.units[i];
          if (P.nxt[u.region][s] < 0 && u.region != s)
            continue;
          double d = P.dist[u.region][s];
          if (d < bd) {
            bd = d;
            settler = i;
            dst = s;
          }
        }
      }
    }
    for (int i : mob) {
      const SUnit &u = st.units[i];
      if (i == settler) {
        if (u.region != dst)
          try_move(u, dst);
        continue; // camper stays; the build block converts it at 300g
      }
      try_move(u, C.myhq); // everyone home (free onto own HQ)
    }
  } else if (pl.kind == PK_FINISH) {
    for (int i : mob)
      try_move(st.units[i], pl.target); // endgame all-in — no muster gate
  } else {
    // ================= BASE LAYER (under every working posture) =============
    // M2b lesson: orthogonal plans couldn't reproduce 55043's integrated
    // allocation — offensive postures abandoned staffing/protection and the
    // economy died behind the army (diag5: bases 4->0 while RAID toured, and
    // ECON<->TECH thrash hoarded gold). CAMP -> PROTECT -> STAFF -> SETTLE
    // now ALWAYS run; the posture only decides what the SURPLUS mobiles do.
    // A. CAMP (P2c): hold an empty own-side stronghold until 300g builds it.
    std::vector<int> rest;
    for (int i : mob) {
      const SUnit &u = st.units[i];
      bool own_side =
          C.h_my[u.region] >= 0 &&
          (C.h_op[u.region] < 0 || C.h_my[u.region] <= C.h_op[u.region]);
      bool enemy_here = false;
      for (const auto &e : st.units)
        if (e.side != me && e.region == u.region && e.hp > 0)
          enemy_here = true;
      if (is_stronghold(M, u.region) && st.bld_at(u.region) == nullptr &&
          own_side && !enemy_here)
        continue; // camp in place (free), build when the bank reaches 300
      rest.push_back(i);
    }
    // B. PROTECT: relieve each hot building with its nearest mobiles, sized
    //    to the local threat (+1 to overwhelm, not tie).
    std::vector<int> rest_p;
    if (!hot.empty()) {
      std::map<int, int> quota;
      for (int r : hot) {
        int threat = 0;
        for (const auto &e : st.units) {
          if (e.side == me || e.hp <= 0)
            continue;
          bool close = e.region == r;
          for (int nb : M.adj[r])
            if (e.region == nb)
              close = true;
          if (close)
            ++threat;
        }
        quota[r] = threat + 1;
      }
      for (int i : rest) {
        const SUnit &u = st.units[i];
        int dst = -1;
        double bd = 1e18;
        for (const auto &[r, q] : quota) {
          if (q <= 0)
            continue;
          if (u.region != r && P.nxt[u.region][r] < 0)
            continue;
          double d = P.dist[u.region][r];
          if (d < bd) {
            bd = d;
            dst = r;
          }
        }
        if (dst >= 0 && (u.region == dst || try_move(u, dst))) {
          quota[dst]--; // a defender already standing there fills quota too
          continue;
        }
        rest_p.push_back(i);
      }
    } else {
      rest_p = rest;
    }
    // C. STAFF (P1d): fill worker deficits (free moves onto own buildings).
    std::map<int, int> inbound; // region -> my units already heading there
    for (const auto &u : st.units)
      if (u.side == me && u.target >= 0)
        inbound[u.target]++;
    std::vector<int> rest2;
    for (int i : rest_p) {
      const SUnit &u = st.units[i];
      int dst = -1;
      double bd = 1e18;
      for (const auto &b : st.blds) {
        if (b.side != me)
          continue;
        int have = 0;
        auto pit = pinned.find(b.region);
        if (pit != pinned.end())
          have = pit->second;
        auto iit = inbound.find(b.region);
        if (iit != inbound.end())
          have += iit->second;
        if (have >= b.wcap())
          continue;
        if (P.nxt[u.region][b.region] < 0)
          continue;
        double d = P.dist[u.region][b.region];
        if (d < bd) {
          bd = d;
          dst = b.region;
        }
      }
      if (dst >= 0 && try_move(u, dst)) {
        inbound[dst]++;
        continue;
      }
      rest2.push_back(i);
    }
    // D. SETTLE toward unclaimed empty own-side strongholds. Offensive
    //    postures keep ONE settler going (expansion never fully stops — the
    //    A4/P4 lesson); growth postures settle with everything left.
    int settle_cap =
        (pl.kind == PK_RAID || pl.kind == PK_ASSAULT) ? 1 : (1 << 30);
    std::vector<int> open;
    for (int s : M.strongholds) {
      if (st.bld_at(s) != nullptr)
        continue;
      if (C.h_my[s] < 0 || (C.h_op[s] >= 0 && C.h_my[s] > C.h_op[s]))
        continue; // enemy side (by hops) — don't overextend by default
      bool contested = false; // enemy standing ON it: never dribble a lone
      for (const auto &e : st.units) // settler in (judge g6: 33 serial deaths
        if (e.side != me && e.region == s && e.hp > 0)
          contested = true; // -> that SH becomes a RAID target: group-clear,
      if (contested)        //    then camp+build take over)
        continue;
      bool claimed = false;
      for (const auto &u : st.units)
        if (u.side == me && (u.target == s || u.region == s))
          claimed = true;
      if (!claimed)
        open.push_back(s);
    }
    std::vector<int> surplus; // what the POSTURE may spend
    for (int i : rest2) {
      const SUnit &u = st.units[i];
      if (settle_cap > 0 && !open.empty()) {
        int best = -1, bi = -1;
        double bd = 1e18;
        for (int k = 0; k < (int)open.size(); ++k) {
          if (P.nxt[u.region][open[k]] < 0 && u.region != open[k])
            continue;
          double d = P.dist[u.region][open[k]];
          if (d < bd) {
            bd = d;
            best = open[k];
            bi = k;
          }
        }
        if (best >= 0 && try_move(u, best)) {
          open.erase(open.begin() + bi);
          --settle_cap;
          continue;
        }
      }
      surplus.push_back(i);
    }
    // E. POSTURE: offensive postures muster the surplus and strike massed.
    if ((pl.kind == PK_RAID || pl.kind == PK_ASSAULT) && !surplus.empty()) {
      int tgt = pl.target;
      int staging = C.myhq; // forward staging: my building nearest the target
      double bd = P.dist[C.myhq][tgt];
      for (const auto &b : st.blds)
        if (b.side == me && P.nxt[b.region][tgt] >= 0 &&
            P.dist[b.region][tgt] < bd) {
          bd = P.dist[b.region][tgt];
          staging = b.region;
        }
      int need = 0; // force massed before the wave strikes (mass, don't trickle)
      if (pl.kind == PK_RAID) {
        const SBld *tb = st.bld_at(tgt);
        int def = tb != nullptr ? tb->turret() : 0;
        for (const auto &e : st.units)
          if (e.side != me && e.region == tgt && e.hp > 0)
            ++def;
        need = def + 3;
      } else { // ASSAULT: S1 sizing, ledger-fed retrain burst
        std::vector<SimUnit> defs;
        const SBld *oh = st.bld_at(C.ophq);
        for (const auto &e : st.units)
          if (e.side != me && e.hp > 0 && C.h_op[e.region] >= 0 &&
              C.h_op[e.region] <= 6)
            defs.push_back(SimUnit{C.h_op[e.region], e.hp});
        if (oh != nullptr) {
          int tcap = HQ_LEVELS[oh->level].train_cap;
          int opp_income = 0, oa = 0;
          for (const auto &b : st.blds)
            if (b.side != me) {
              int c = 0;
              for (const auto &e : st.units)
                if (e.side != me && e.region == b.region)
                  ++c;
              opp_income += WORK_INCOME * std::min(c, b.wcap());
            }
          for (const auto &e : st.units)
            if (e.side != me)
              ++oa;
          int sustain = std::max(
              0, std::min(tcap, (opp_income - UPKEEP_PER_WARRIOR * oa) /
                                    TRAIN_COST));
          int burst =
              std::min(tcap, (int)(st.gold[1 - me] / TRAIN_COST));
          int my_whp = 4;
          for (const auto &b : st.blds)
            if (b.side == me && b.type == 0)
              my_whp = HQ_LEVELS[b.level].warrior_hp;
          need = sim_hq_need(defs, HQ_LEVELS[oh->level].turret, oh->hp,
                             HQ_LEVELS[oh->level].warrior_hp, burst, sustain,
                             std::max(1, C.h_op[staging]), my_whp) +
                 1;
        } else {
          need = 1;
        }
      }
      wave_need = need; // m2ao: expose the muster size to the train gate
      int at_staging = 0;
      for (int i : surplus)
        if (st.units[i].region == staging)
          ++at_staging;
      double stg_d = P.dist[staging][tgt];
      for (int i : surplus) {
        const SUnit &u = st.units[i];
        bool engaged = P.dist[u.region][tgt] < stg_d; // past the staging line
        if (engaged || at_staging >= need)
          try_move(u, tgt); // strike / press / hold the target
        else
          try_move(u, staging); // gather (free if staging is our building)
      }
    }
    // GROW/TECH: surplus holds position (free) — the search escalates to an
    // offensive posture when a rollout says the fight actually pays.
  }

  // -- train ------------------------------------------------------------------
  {
    const SBld *hq = nullptr;
    int total_wcap = 0;
    for (const auto &b : st.blds)
      if (b.side == me) {
        total_wcap += b.wcap();
        if (b.type == 0)
          hq = &b;
      }
    if (hq != nullptr) {
      int tcap = HQ_LEVELS[hq->level].train_cap;
      // STAFFED workers, not raw army: units out settling/raiding/locked
      // mid-march staff nothing — the army-based test read a scattered army
      // as "fully staffed" and training stopped while buildings sat idle
      // (diag2: gold hoarded to 1000+ with tr=0 for 100 turns).
      int staffed = 0;
      for (const auto &[r, c] : pinned)
        staffed += c;
      int want = 0;
      if (pl.kind == PK_DEFEND || pl.kind == PK_FINISH)
        want = tcap; // max defense / last push
      else if (pl.kind == PK_RAID || pl.kind == PK_ASSAULT) {
        // m2ao FEED THE WAVE ONLY WHILE IT'S HUNGRY (the 0704-2 allocation
        // fix): unconditional tcap during offensive postures trained 73-86
        // units across the mid-game while the HQ stalled exactly ONE level
        // behind — every tie-loss in the ladder run (1990L L2v3, 2060L L3v4
        // 73v39 trains, 2300L-2 L4v5 86v34, 2430L L2v3) was this middle
        // path: too much army to out-tech, not enough conversion to kill.
        // Train only while the mobile force hasn't covered the muster size
        // (+4 attrition margin); once the wave is fed the surplus gold
        // flows to the HQ level-up block (it fires under every plan) — the
        // search now swings between REAL poles: a fed strike or hard tech.
        // Hot/insurance overrides below still raise `want` when it matters.
        int fighters = my_army - staffed; // wave members incl. mid-march
        if (wave_need >= 0 && fighters >= wave_need + 4)
          want = (staffed < total_wcap) ? 1 : 0; // bodies only for income
        else
          want = tcap; // feed the wave
      } else if (pl.kind == PK_ECON)
        want = (staffed < total_wcap + 2) ? tcap : 0; // staff + settler buffer
      else // TECH: bodies only for unstaffed income
        want = (staffed < total_wcap) ? 1 : 0;
      if (!hot.empty())
        want = tcap; // buildings under attack: base layer needs defenders NOW
      // INSURANCE CAP vs a PASSIVE home ball (judge-59028 g6/g8 fix): the
      // stockpile all-in model makes rollouts demand troops vs a big idle
      // enemy ball — correct vs a masser (it WILL march, 58941 g3 died), but
      // vs a turtle that never comes, unlimited insurance froze our tech at
      // L1 (g6: 0 HQ ups, 26 trains) and lost the tiebreak. The two are NOT
      // distinguishable before the march, so cap the premium: while the ball
      // is passive (nothing raided, nobody in our half), hold FIELD PARITY+2
      // and let the rest of the gold keep teching.
      {
        int en_idle = 0;
        bool en_in_our_half = false;
        std::map<int, int> ow2;
        for (const auto &e : st.units) {
          if (e.side == me || e.hp <= 0)
            continue;
          if (C.h_my[e.region] >= 0 && C.h_op[e.region] >= 0 &&
              C.h_my[e.region] < C.h_op[e.region])
            en_in_our_half = true;
          if (e.target >= 0)
            continue; // marching (rollout ball underway) — not passive
          const SBld *b = st.bld_at(e.region);
          if (b != nullptr && b->side != me && ow2[e.region] < b->wcap()) {
            ow2[e.region]++;
            continue;
          }
          ++en_idle;
        }
        if (!en_in_our_half && hot.empty() && en_idle >= 6 &&
            en_idle >= my_army) {
          // Premium CEILING scales with OUR economy, not their ball: chasing
          // en_idle+2 alone is a moving target vs a bigger-economy grower
          // (judge 59117 g6: their ball outgrew our training forever, cap
          // never bound, tech stayed frozen, 2-vs-15 bases). Match a small
          // foe's ball outright (masser: staffed+8 > ball -> parity, the g3
          // fix survives); against an outscaling one, hold what we can
          // afford and put the rest into tech/economy + JIT defense.
          int ceiling = std::min(en_idle + 2, staffed + 8);
          want = ((my_army - staffed) < ceiling) ? tcap : 0;
        }
      }
      // Keep the HQ POPULATED: a spawn lands at the HQ, restoring the worker
      // AND the body an HQ upgrade legally requires (diag2 hoarded 1000g at
      // up=0 — nobody home to execute the level-up).
      if (pinned.find(C.myhq) == pinned.end())
        want = std::max(want, 1);
      // ECON/TECH bank the pending settle: while an empty own-side stronghold
      // exists, keep 300 out of training's reach so the settler can BUILD on
      // arrival instead of camping broke (the M1 poverty trap: training ate
      // the base fund every turn and income never compounded).
      bool want_base = false;
      if (pl.kind == PK_ECON || pl.kind == PK_TECH)
        for (int s : M.strongholds)
          if (st.bld_at(s) == nullptr && C.h_my[s] >= 0 &&
              (C.h_op[s] < 0 || C.h_my[s] <= C.h_op[s])) {
            want_base = true;
            break;
          }
      // m2ab POST-RAZE CONVERT FUND (s11 X-ray): a successful RAID leaves an
      // empty stronghold under the wave's feet, and the build block already
      // converts it (it has no own-side filter; the wave stands STATIONARY
      // on it the turn after the raze). It never fired (s11: 10 sieges, 0
      // converts) because the raid's own training spends the gold below the
      // 300 build cost by the time the building falls — while 55043
      // converted OUR razed regions (9, 13) and compounded the game away.
      // Keep the convert fund banked while raiding: one deferred train
      // every ~3 turns buys the conversion engine.
      if (pl.kind == PK_RAID)
        want_base = true;
      // Reserve the base fund only while someone can actually PLACE a base —
      // reserving it with zero mobiles deadlocked the opening (diag5 T20-60:
      // all 3 units pinned as workers, gold hoarded to 1228, nothing trained
      // because the reserve blocked the very settler the base needed).
      long long gate =
          (want_base && !mob.empty()) ? BASE_LEVELS[1].cost : 0;
      if (pl.kind == PK_TECH)
        gate += next_hq_cost; // TECH also banks the next HQ level
      // [m2as] SURVIVAL FLOOR (ported from 75998; ladder 0705-1 2290L T114 freeze-death):
      //   when the army is shredded to <=2, release ALL reserves (base fund + TECH HQ bank)
      //   so the last defenders are still trained -- else army->0, workers->0, income 0,
      //   gold frozen a few short of a train = a 20-turn wait for the executioner. NOTE:
      //   the early-L2 BANKING from 75998 is deliberately NOT ported (it caused the T17
      //   rush bug and cost ~18 net vs peers); only this bug fix is kept.
      if (my_army <= 2)
        gate = 0;
      int n = 0;
      while (n < want &&
             bud - TRAIN_COST >= food + gate + UPKEEP_PER_WARRIOR) {
        bud -= TRAIN_COST;
        food += UPKEEP_PER_WARRIOR;
        ++n;
      }
      o.train_n = n;
    }
  }
}

// ---------------------------------------------------------------------------
// Enemy rollout policy: inferred journeys continue (their unit targets were
// seeded from observation); trains hard when their HQ is threatened, else
// grows slowly; upgrades HQ from surplus. Deliberately simple — modeling
// error shows up as ladder losses and gets iterated there.
static void opp_policy(const Ctx &C, const SState &st, SOrders &o) {
  const int op = C.op;
  const SBld *hq = nullptr;
  for (const auto &b : st.blds)
    if (b.side == op && b.type == 0)
      hq = &b;
  if (hq == nullptr)
    return;
  long long g = st.gold[op];
  // ALERT: any of our units inside THEIR half. Without this the model only
  // trained when rich, so rollouts showed raids succeeding for free and the
  // search sent the T6 starters cross-map instead of settling (diag_loss:
  // mv=0 for 100+ turns, economy never compounded). A real opponent SEES an
  // incursion and trains against it — price that in.
  bool threatened = false, alert = false;
  for (const auto &u : st.units) {
    if (u.side == op)
      continue;
    if (C.h_op[u.region] >= 0 && C.h_op[u.region] <= 2)
      threatened = true;
    if (C.h_op[u.region] >= 0 && C.h_my[u.region] >= 0 &&
        C.h_op[u.region] <= C.h_my[u.region])
      alert = true;
  }
  if (!threatened && hq->level < HQ_MAX_LEVEL) {
    long long c = HQ_LEVELS[hq->level + 1].upgrade_cost;
    if (g >= c + 400) {
      o.upgrades.push_back(hq->region);
      g -= c;
    }
  }
  // OBSERVATION-CONDITIONED raids: send at most `max_raiders_seen` of their
  // idle field mobiles at OUR nearest buildings — i.e. model exactly the raid
  // scale this opponent has DEMONSTRATED. Zero observed -> zero modeled (the
  // rollouts stay M2c-identical); heavy raider -> our rollouts finally show
  // what an unguarded economy costs, so defense/training gets real value.
  {
    const Paths &P = *C.P;
    std::map<int, int> owork;
    std::vector<const SUnit *> idle;
    int my_total = 0;
    for (const auto &u : st.units) {
      if (u.side != op) {
        if (u.hp > 0)
          ++my_total;
        continue;
      }
      if (u.target >= 0 || u.hp <= 0)
        continue;
      const SBld *b = st.bld_at(u.region);
      if (b != nullptr && b->side == op && owork[u.region] < b->wcap()) {
        owork[u.region]++; // worker stays home
        continue;
      }
      idle.push_back(&u);
    }
    // STOCKPILE ALL-IN (the judge-58941 g3 killer): a masser never raids, so
    // the observation latch stays 0 — yet its home BALL is fully observable.
    // When their idle field force clearly out-masses our whole army, model
    // the one-shot march on our HQ; the rollout then prices the defense we
    // are missing (g3: we trained 0 for 30 turns and died T77 to a 15-ball).
    // Peer games (both sides ~even) don't trigger — no M2e paranoia tax.
    // [allinball] LOGIC FIX (TB anatomy g86; iter 3): the trigger counted
    //   their GLOBAL idle field force, so in even peer endgames (35-42 idle
    //   spread over raids/garrisons vs our swinging 33-50) it fired constantly
    //   -> every sim showed a phantom all-in -> all plans except DEF hit the
    //   -1e15 death terminal (T165-195 scoreboards) while the real foe never
    //   marched (HQ damage 0 in all 15 TB losses) -> DEF-hoard lock, L3 stall.
    //   The masser signature this models is the observable HOME BALL (per the
    //   comment above), not global count: count only idle massed within 2 hops
    //   of THEIR HQ. Masser g3 (15-ball at home vs our ~5) still fires; a
    //   peer's dispersed endgame force does not.
    int ball = 0;
    for (const SUnit *u : idle)
      if (C.h_op[u->region] >= 0 && C.h_op[u->region] <= 2)
        ++ball;
    if (ball >= 6 && ball >= my_total) {
      for (const SUnit *u : idle) {
        if (u->region != C.myhq && g >= MOVE_COST &&
            P.nxt[u->region][C.myhq] >= 0) {
          o.moves.push_back({u->num, C.myhq});
          g -= MOVE_COST;
        }
      }
    } else if (OB.max_raiders_seen > 0) {
      int send = std::min((int)idle.size() - 2, OB.max_raiders_seen);
      for (int k = 2; k < 2 + send; ++k) { // first 2 stay as home guard
        const SUnit *u = idle[k];
        int dst = -1;
        double bd = 1e18;
        for (const auto &b : st.blds) {
          if (b.side == op)
            continue; // target OUR buildings
          if (u->region != b.region && P.nxt[u->region][b.region] < 0)
            continue;
          double d = P.dist[u->region][b.region];
          if (d < bd) {
            bd = d;
            dst = b.region;
          }
        }
        if (dst >= 0 && dst != u->region && g >= MOVE_COST) {
          o.moves.push_back({u->num, dst});
          g -= MOVE_COST;
        }
      }
    }
  }
  int tcap = HQ_LEVELS[hq->level].train_cap;
  int n = 0,
      cap = (threatened || alert) ? tcap : (g >= 800 ? 1 : 0);
  while (n < cap && g >= TRAIN_COST) {
    g -= TRAIN_COST;
    ++n;
  }
  o.train_n = n;
}

// ---------------------------------------------------------------------------
static double eval_state(const Ctx &C, const SState &st) {
  const SBld *mh = nullptr, *oh = nullptr;
  for (const auto &b : st.blds) {
    if (b.type != 0)
      continue;
    if (b.side == C.me)
      mh = &b;
    else
      oh = &b;
  }
  if (oh == nullptr && mh != nullptr)
    return 1e15 - (double)st.day * 1e9; // kill, sooner the better
  if (mh == nullptr)
    return -1e15 + (double)st.day * 1e9; // death, later the better
  // GOLD-EQUIVALENT asset pricing (horizon-consistent): assets are worth what
  // they cost, work_cap is priced by its future income, units by their cost
  // net of upkeep. HQ hp (the T200 tiebreak) is worth little early and ramps
  // to DOMINANT near day 200 — the first eval used a flat 1e6/hp and the
  // search degenerated into pure mirror-teching (DEF forever, no economy).
  double val[2] = {0, 0};
  for (const auto &b : st.blds) {
    double av = 0;
    if (b.type == 0)
      for (int L = 2; L <= b.level; ++L)
        av += HQ_LEVELS[L].upgrade_cost;
    else {
      av += BASE_LEVELS[1].cost;
      for (int L = 2; L <= b.level; ++L)
        av += BASE_LEVELS[L].cost;
    }
    // m2an REMAINING-GAME wcap pricing (also the raze-EV amplifier): the
    // flat 400 undervalued both OUR compounding and the damage a raze does
    // to THEIRS (the eval is differential, so pricing wcap right prices
    // economic warfare right — the 2430L opponent's "wreck their economy
    // early, tech on the ruins" strategy becomes EV-visible). 15g x
    // min(remaining, 40) turns: ~600 early, decaying late (m2x insight).
    double wv = (double)WORK_INCOME *
                std::min(200 - st.day > 0 ? 200 - st.day : 1, 40);
    val[b.side] += av + wv * b.wcap(); // asset cost + future income stream
  }
  for (const auto &u : st.units)
    val[u.side] += 100.0; // [j69+army100] // train cost net of future upkeep
  val[0] += (double)st.gold[0];
  val[1] += (double)st.gold[1];
  double hp_w = 200.0; // repair-cost-ish while the tiebreak is far away
  if (st.day > 160)
    hp_w += (st.day - 160) * (30000.0 / 40.0); // ramps to ~3e4/hp at day 200
  return (val[C.me] - val[C.op]) + hp_w * (double)(mh->hp - oh->hp);
}

} // namespace sb

// ---------------------------------------------------------------------------
static Actions decide(const GameState &S, const GameMap &M, const Paths &P,
                      int turn) {
  using namespace sb;
  const auto t0 = std::chrono::steady_clock::now();
  auto elapsed_ms = [&t0]() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - t0)
        .count();
  };

  Ctx C;
  C.me = (int)M.my_side;
  C.op = 1 - C.me;
  C.myhq = M.my_hq;
  C.ophq = M.opp_hq;
  C.h_my = bfs_hops(M, M.my_hq);
  C.h_op = bfs_hops(M, M.opp_hq);
  C.M = &M;
  C.P = &P;

  observe(S, C);

  SState base = sim_from_game(S, M, S.gold, OB.opp_gold);
  for (auto &u : base.units) { // seed inferred enemy journeys
    if (u.side != C.op)
      continue;
    auto it = OB.guess.find(u.num);
    if (it != OB.guess.end())
      u.target = it->second;
  }
  base.day = turn;

  // m2aq ASSAULT-STALL WATCHDOG (g8 X-ray, 69046): the rollout promised a
  // kill at ~1.02e15 for FORTY straight turns (T140-180) while the real
  // assault dealt ZERO HQ siege — the opponent model defends softer than a
  // real peer, so the phantom kill outranked TECH's honest tiebreak-win
  // line (154k and climbing) every single turn and we DREW a game we
  // economically dominated (army 19, wcap 8, trains 62v28). Trust, then
  // verify: an ASSAULT incumbent that fails to scratch their HQ for 12
  // consecutive turns loses its candidacy for 20 turns — the search falls
  // through to the next honest plan (g8: TECH -> level up -> tiebreak win;
  // the user's "can't kill the HQ? own every base and out-tech" doctrine).
  // A working assault (hp dropping) never trips it: real kills untouched.
  {
    int ophp = -1;
    for (const auto &b : base.blds)
      if (b.side == C.op && b.type == 0)
        ophp = b.hp;
    if (g_as_cool > 0)
      --g_as_cool;
    if (g_rd_cool > 0 && --g_rd_cool == 0)
      g_rd_cool_tgt = -1; // m2ba: raid bench expires
    { // m2ba strangle ledger: who lost building hp this turn (observed only)
      bool hit[2] = {false, false};
      for (const auto &[r, sh] : g_prev_blds) {
        const SBld *b = base.bld_at(r);
        if (b == nullptr || b->side != sh.first)
          hit[sh.first] = true; // razed (or replaced by the other side)
        else if (b->hp < sh.second)
          hit[sh.first] = true; // sieged
      }
      if (hit[C.me])
        ++g_sieged_us;
      if (hit[C.op])
        ++g_sieged_op;
      g_prev_blds.clear();
      for (const auto &b : base.blds)
        g_prev_blds[b.region] = {b.side, b.hp};
    }
    if (INCUMBENT.kind == PK_ASSAULT) {
      if (ophp >= 0 && g_prev_ophp >= 0 && ophp < g_prev_ophp)
        g_as_stall = 0; // progress: the siege is real
      else if (++g_as_stall >= 12) {
        g_as_cool = 20; // phantom kill: bench the assault, let TECH speak
        g_as_stall = 0;
      }
    } else
      g_as_stall = 0;
    g_prev_ophp = ophp;
  }

  // --- candidate plans -------------------------------------------------------
  std::vector<Plan> cand;
  cand.push_back(Plan{PK_ECON, -1});
  cand.push_back(Plan{PK_TECH, -1});
  cand.push_back(Plan{PK_DEFEND, -1});
  // WAVE FEASIBILITY (zombie-camp fix): an offensive candidate whose needed
  // force can NEVER muster just parks the army at staging forever — a1 died
  // after 30+ turns of ASSLT mv=0, g6's opening froze in a T20-50 RAID camp.
  // Estimate our force ceiling (field mobiles + ~10 turns of training) and
  // only OFFER attacks that fit under it; the search then compares honest
  // alternatives (resettle/grow) instead of a zombie. FINISH stays exempt.
  int force_ceiling = 0;
  {
    int mob_now = 0, tcap_now = 1;
    std::map<int, int> pinq;
    for (const auto &u : base.units) {
      if (u.side != C.me)
        continue;
      const SBld *b = base.bld_at(u.region);
      if (b != nullptr && b->side == C.me && pinq[u.region] < b->wcap()) {
        pinq[u.region]++; // worker
        continue;
      }
      ++mob_now;
    }
    for (const auto &b : base.blds)
      if (b.side == C.me && b.type == 0)
        tcap_now = HQ_LEVELS[b.level].train_cap;
    force_ceiling = mob_now + tcap_now * 10;
  }
  { // top-2 raidable enemy bases by (defense + distance from us)
    std::vector<std::pair<double, int>> rb;
    for (const auto &b : base.blds) {
      if (b.side == C.me || b.type != 1)
        continue;
      int def = b.turret();
      for (const auto &e : base.units)
        if (e.side != C.me && e.region == b.region && e.hp > 0)
          ++def;
      if (C.h_my[b.region] < 0)
        continue;
      if (def + 3 > force_ceiling)
        continue; // wave can't exist — don't offer a zombie camp
      if (b.region == g_rd_cool_tgt && g_rd_cool > 0)
        continue; // m2ba: benched after a fruitless committed raid
      rb.push_back({def + C.h_my[b.region], b.region});
    }
    std::sort(rb.begin(), rb.end());
    for (int k = 0; k < (int)rb.size() && k < 2; ++k)
      cand.push_back(Plan{PK_RAID, rb[k].second});
  }
  { // ASSAULT only when the sim-sized wave fits under our force ceiling
    int need_est = 1;
    const SBld *oh = base.bld_at(C.ophq);
    if (oh != nullptr) {
      std::vector<SimUnit> defs;
      for (const auto &e : base.units)
        if (e.side != C.me && e.hp > 0 && C.h_op[e.region] >= 0 &&
            C.h_op[e.region] <= 6)
          defs.push_back(SimUnit{C.h_op[e.region], e.hp});
      int otc = HQ_LEVELS[oh->level].train_cap;
      int opp_income = 0, oa = 0;
      for (const auto &b : base.blds)
        if (b.side != C.me) {
          int c = 0;
          for (const auto &e : base.units)
            if (e.side != C.me && e.region == b.region)
              ++c;
          opp_income += WORK_INCOME * std::min(c, b.wcap());
        }
      for (const auto &e : base.units)
        if (e.side != C.me)
          ++oa;
      int sustain = std::max(
          0, std::min(otc, (opp_income - UPKEEP_PER_WARRIOR * oa) /
                               TRAIN_COST));
      int burst = std::min(otc, (int)(base.gold[C.op] / TRAIN_COST));
      int my_whp = 4;
      for (const auto &b : base.blds)
        if (b.side == C.me && b.type == 0)
          my_whp = HQ_LEVELS[b.level].warrior_hp;
      need_est = sim_hq_need(defs, HQ_LEVELS[oh->level].turret, oh->hp,
                             HQ_LEVELS[oh->level].warrior_hp, burst, sustain,
                             /*eta0=*/2, my_whp) +
                 1;
    }
    if (need_est <= force_ceiling && g_as_cool == 0)
      cand.push_back(Plan{PK_ASSAULT, C.ophq}); // m2aq: benched while stalled
  }
  { // CONTESTED SETTLEMENT (judge g6 fix): an empty own-side stronghold with
    // enemies standing on it is offered as a RAID target — the muster/strike
    // machinery IS the settler escort (clear as a group, then the base layer
    // camps & builds it). The settle step skips these, so no more feeding
    // lone settlers into a held tile one per turn (g6: 33 serial deaths,
    // 2-vs-15 bases, economy strangled).
    int best = -1;
    double bs = 1e18;
    for (int s : M.strongholds) {
      if (base.bld_at(s) != nullptr)
        continue;
      if (C.h_my[s] < 0 || (C.h_op[s] >= 0 && C.h_my[s] > C.h_op[s]))
        continue; // own side only
      int ec = 0;
      for (const auto &e : base.units)
        if (e.side != C.me && e.region == s && e.hp > 0)
          ++ec;
      if (ec == 0)
        continue;
      if (ec + 3 > force_ceiling)
        continue; // escort wave can't exist — no zombie camp
      if (s == g_rd_cool_tgt && g_rd_cool > 0)
        continue; // m2ba: benched after a fruitless committed raid
      double sc = ec * 2.0 + C.h_my[s]; // prefer lightly-held & near
      if (sc < bs) {
        bs = sc;
        best = s;
      }
    }
    if (best >= 0)
      cand.push_back(Plan{PK_RAID, best});
  }
  // (no PROTECT candidate anymore: base relief lives in the BASE LAYER that
  //  runs under every working posture — M2c restructure)
  if (turn >= 170)
    cand.push_back(Plan{PK_FINISH, C.ophq});
  // evaluate the incumbent FIRST so a timeout still has a sane best
  for (size_t i = 0; i < cand.size(); ++i)
    if (cand[i] == INCUMBENT && i > 0) {
      std::swap(cand[0], cand[i]);
      break;
    }

  // --- rollouts ---------------------------------------------------------------
  const int H = 24;
  double best = -1e30;
  Plan bestp = cand[0];
  std::string scoreboard; // every candidate's score, for the debug line
  for (const auto &pl : cand) {
    if (elapsed_ms() > 55)
      break;
    SState st = base;
    for (int h = 0; h < H && st.day < MAX_TURN; ++h) {
      SOrders od[2];
      my_policy(pl, C, st, od[C.me]);
      opp_policy(C, st, od[C.op]);
      if (!sim_step(st, M, P, od, nullptr))
        break;
      if (elapsed_ms() > 65)
        break;
    }
    double sc = eval_state(C, st);
    if (pl == INCUMBENT)
      sc += std::abs(sc) * 0.02 + 500; // plan stickiness (anti-thrash; small
                                       // so real differences still override)
    scoreboard += std::string(" ") + PK_NAME[pl.kind] +
                  (pl.target >= 0 ? "@" + std::to_string(pl.target) : "") +
                  ":" + std::to_string((long long)sc);
    if (sc > best) {
      best = sc;
      bestp = pl;
    }
  }
  // m2ah OFFENSIVE COMMIT INERTIA (the g6 thrash fix, h-dump verified): with
  // an L1 HQ every rollout is a knife-edge home-defense race, so a ±1-unit
  // input blip flips a committed strike between +2577 and -1e15 on
  // consecutive turns (63305 g6: T54 six units out, T56 eight units back,
  // repeated ~150 turns — the move bleed kept gold below the 720 the HQ
  // needed). The 2%+500 incumbent bonus is powerless against a 1e15 swing,
  // and every score-side fix (eval margin, reserve liquidation, reactive
  // override) reshuffled peers because it acted INSIDE rollouts. This one
  // acts only at the final choice, which rollouts never execute: a
  // committed RAID/ASSAULT ignores ONE dissenting turn and switches only on
  // the second consecutive one. Real signals keep priority — a landed hit
  // (hot), the incumbent dropping out of the candidate list (target razed),
  // or a FINISH call all switch immediately.
  {
    static Plan sw_pending{};
    static int sw_cnt = 0;
    // m2ba RAID WAVE COMPLETION (ladder 0705-3 strangle X-ray, 2480L): every
    // strangle loss shares "raided 14-31x, we sieged 0x" although RAID plans
    // WERE chosen (38 turns in 2480L). The wave never lands because (a) the
    // real_hot bypass below checked ANY of our buildings, and a strangler has
    // raiders on some base almost every turn, so the anti-thrash hold was
    // disabled exactly in the games that need the counter-raid; (b) the plan
    // scores oscillate +-2k under constant pressure, so ECON outbids RAID for
    // a few turns mid-muster (2480L T72: 4 units staged at 34, need~5, one
    // spawn short) and the switch re-pins the whole muster as workers - 17
    // turns of investment destroyed, repeat forever, siege 0. Fix, still at
    // the final-choice layer that rollouts never execute, and ONLY while the
    // strangle ledger says we are being out-raided: the bypass narrows to
    // the HQ (base raids are the base layer's + hot-training's job), and a
    // committed RAID holds its target a minimum RAID_HOLD turns so the wave
    // is fed to strike size and lands. Releases stay immediate: HQ-hot,
    // target gone (razed -> incumbent drops out of cand), FINISH. Camping is
    // hard-bounded: past RAID_HOLD turns the normal 2-turn rule resumes, so
    // no watchdog is needed (20 covers the 2480L muster, commit T56 ->
    // strike-size ~T74; the 12 first tried expired mid-muster, counterfactual
    // T71 flip).
    constexpr int RAID_HOLD = 20, RAID_NOPROG = 8, RAID_BENCH = 30;
    static int cm_tgt = -1, cm_turns = 0, cm_hp_last = -1, cm_noprog = 0;
    bool inc_alive = false;
    for (const auto &pl : cand)
      if (pl == INCUMBENT) {
        inc_alive = true;
        break;
      }
    // m2ba engages ONLY under an observed strangle (we clearly out-raided):
    // in balanced games every knob below reverts to stock 78624 behaviour —
    // the unconditional hold lost the 78624 mirror (2W/1D/5L, 4 deaths).
    bool strangled = g_sieged_us >= g_sieged_op + 5;
    bool real_hot = false; // enemy on/adjacent to OUR buildings NOW
    for (const auto &b : base.blds) {
      if (b.side != C.me)
        continue;
      if (strangled && b.type != 0)
        continue; // strangle: only the HQ aborts the wave (base raids are
                  // the base layer's + hot-training's job, and some base is
                  // hot almost every turn = the hold would never engage)
      for (const auto &e : base.units) {
        if (e.side == C.me || e.hp <= 0)
          continue;
        if (e.region == b.region) {
          real_hot = true;
          break;
        }
        for (int nb : M.adj[b.region])
          if (e.region == nb) {
            real_hot = true;
            break;
          }
        if (real_hot)
          break;
      }
      if (real_hot)
        break;
    }
    if (INCUMBENT.kind == PK_RAID) { // commitment bookkeeping (observed state)
      if (INCUMBENT.target != cm_tgt) {
        cm_tgt = INCUMBENT.target;
        cm_turns = 0;
        cm_hp_last = -1;
        cm_noprog = 0;
      }
      ++cm_turns;
      const SBld *tb = base.bld_at(cm_tgt);
      int hp = tb != nullptr ? tb->hp : -1;
      if (hp >= 0 && cm_hp_last >= 0 && hp < cm_hp_last)
        cm_noprog = 0; // the wave is landing (building hp dropping)
      else
        ++cm_noprog;
      cm_hp_last = hp;
      if (cm_turns > RAID_HOLD && cm_noprog >= RAID_NOPROG) {
        g_rd_cool_tgt = cm_tgt; // fruitless past the hold: bench the target
        g_rd_cool = RAID_BENCH; // (drops from cand -> inc_alive=false next
        cm_tgt = -1;            //  turn -> immediate honest re-plan)
        cm_turns = 0;
      }
    } else {
      cm_tgt = -1;
      cm_turns = 0;
    }
    if ((INCUMBENT.kind == PK_RAID || INCUMBENT.kind == PK_ASSAULT) &&
        inc_alive && !(bestp == INCUMBENT) && !real_hot &&
        bestp.kind != PK_FINISH) {
      if (sw_pending == bestp)
        ++sw_cnt;
      else {
        sw_pending = bestp;
        sw_cnt = 1;
      }
      bool min_hold =
          strangled && INCUMBENT.kind == PK_RAID && cm_turns < RAID_HOLD;
      if (sw_cnt < 2 || min_hold)
        bestp = INCUMBENT; // blip or mid-muster: hold the strike
    } else
      sw_cnt = 0;
  }
  INCUMBENT = bestp;

  // --- emit the chosen plan's first-day orders, re-validated for WA-safety ---
  SOrders mine;
  my_policy(bestp, C, base, mine);

  Actions a;
  long long bud = S.gold;
  std::set<int> upreg;
  for (int r : mine.upgrades) { // spend order: upgrades first
    if (upreg.count(r))
      continue;
    if (my_count_at(S, M, r) <= 0 || enemy_count_at(S, M, r) > 0)
      continue;
    const Building *b = building_at(S, r);
    long long cost;
    if (b == nullptr)
      cost = BASE_LEVELS[1].cost;
    else if (b->side != M.my_side)
      continue; // never touch enemy buildings
    else if (b->level >= max_level(*b))
      cost = (b->type == BType::HQ) ? HQ_HEAL_COST : BASE_HEAL_COST;
    else
      cost = upgrade_cost(*b);
    if (b == nullptr && (r == M.my_hq || r == M.opp_hq))
      continue; // cannot BUILD on a HQ region
    if (bud < cost)
      continue;
    bud -= cost;
    a.upgrades.push_back(r);
    upreg.insert(r);
  }
  for (const auto &[num, tgt] : mine.moves) { // then moves
    const Warrior *w = nullptr;
    for (const auto &x : S.warriors)
      if (x.id.side == M.my_side && x.id.num == num)
        w = &x;
    if (w == nullptr || w->state != WState::STATIONARY || tgt == w->region)
      continue;
    if (!reachable(P, w->region, tgt))
      continue;
    const Building *b = building_at(S, tgt);
    long long cost = (b != nullptr && b->side == M.my_side) ? 0 : MOVE_COST;
    if (bud < cost)
      continue;
    bud -= cost;
    a.moves.emplace_back(w->id, tgt);
  }
  { // train last
    const Building *hq = building_at(S, M.my_hq);
    int tcap = (hq != nullptr && hq->side == M.my_side)
                   ? HQ_LEVELS[hq->level].train_cap
                   : 0;
    int n = std::min(mine.train_n, tcap);
    while (n > 0 && bud < (long long)TRAIN_COST * n)
      --n;
    if (n > 0) {
      a.train_n = n;
      bud -= (long long)TRAIN_COST * n;
    }
  }

  { // growth telemetry: army/wcap/hq mirror 55043's debug for direct diffing
    int army = 0, wcap = 0, hqlvl = 0, basec = 0;
    for (const auto &w : S.warriors)
      if (w.id.side == M.my_side)
        ++army;
    for (const auto &b : S.buildings)
      if (b.side == M.my_side) {
        wcap += b.work_cap();
        if (b.type == BType::HQ)
          hqlvl = b.level;
        else
          ++basec;
      }
    std::cerr << "T" << turn << " plan=" << PK_NAME[bestp.kind] << "@"
              << bestp.target << " ms=" << elapsed_ms() << " gold=" << S.gold
              << " oppg=" << OB.opp_gold << " army=" << army
              << " wcap=" << wcap << " hq=" << hqlvl << " bases=" << basec
              << " up=" << a.upgrades.size() << " mv=" << a.moves.size()
              << " tr=" << a.train_n << " |" << scoreboard << "\n";
  }
  return a;
}

#ifndef SIM_CALIB // JS/debug/calib.cpp includes this file and brings its own main()
int main() {
  GameMap M;
  GameState S;
  parse_init(M, S);              // initialize the game
  Paths P = calculate_paths(M); // calculate the shortest paths

  int turn;
  while (read_turn_start(turn)) {
    Actions a = decide(S, M, P, turn);
    emit_command();
    emit_actions(a);
    emit_end();
    read_turn_result(S, M, a);
  }
  return 0;
}
#endif // SIM_CALIB
