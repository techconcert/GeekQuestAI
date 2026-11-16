// GeekQuestAI: rules of the data center scene. The technician plays by himself: he receives deliveries, racks and
// wires hardware, keeps the water plant and the roof solar running, fights rogue robo-engineers, runs one long
// mission a day and sleeps on the cot in the office. No heap, no libc, no floating point.
#include "dc.h"

DcState D;

#define STEP_MS       22        // ms per pixel the technician walks
#define SAVE_MAGIC    0x49415147u   // "GQAI"
#define SAVE_VERSION  1

// ------------------------------------------------------------------ small helpers
static uint32_t hash32(uint32_t x) {
  x ^= x >> 16; x *= 0x7feb352du; x ^= x >> 15; x *= 0x846ca68bu; x ^= x >> 16;
  return x;
}

static void touch(void) { D.serial++; }

static uint32_t crc32_calc(const uint8_t* p, int n) {
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) {
    c ^= p[i];
    for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u)));
  }
  return ~c;
}

int dc_rack_x(int slot) { int c = slot % 10; return 2 + c + (c >= 5 ? 1 : 0); }
int dc_rack_y(int slot) { return 4 + (slot / 10) * 3; }
int dc_panel_x(int slot) { int c = slot % 6; return 1 + c + (c >= 3 ? 1 : 0); }
int dc_panel_y(int slot) { return 4 + (slot / 6) * 2; }

int dc_racks_done(void) {
  int n = 0;
  for (int i = 0; i < DC_RACKS; i++) if ((D.s.rack[i] & RK_TIER) && (D.s.rack[i] & RK_DONE) == RK_DONE) n++;
  return n;
}

static int panels_total(void) {
  int n = 0;
  for (int i = 0; i < DC_PANELS; i++) if (D.s.panel[i]) n++;
  return n;
}

int dc_wx(void) { return (int)D.live_wx1 - 1; }

int dc_is_night(void) {
  int m = D.s.minute;
  return m >= GC_NIGHT_START_MIN || m < GC_NIGHT_END_MIN;
}

static int sleep_due(void) { return D.sleep_mode == 0 ? dc_is_night() : D.sleep_mode == 2; }

int dc_asleep(void) { return (D.job == GQ_ACT_SLEEP || D.job == GQ_ACT_REST) && D.hpose == POSE_BED; }

static int glitched_robot(int crew_only) {
  for (int i = 0; i < (crew_only ? 4 : DC_ROBOTS); i++) if (D.rb[i].on && D.rb[i].glitch) return i;
  return -1;
}

int dc_alarm(void) {
  if (D.s.water_down) return 1;
  if (glitched_robot(1) >= 0) return 2;
  return 0;
}

// Output in T (tera). Racks count once they are wired and patched.
static uint32_t tlops_now(void) {
  static const uint16_t TIER[4] = {0, 20, 250, 3000};
  uint32_t base = 0;
  if (D.blip) return 0;
  for (int i = 0; i < DC_RACKS; i++)
    if ((D.s.rack[i] & RK_DONE) == RK_DONE) base += TIER[D.s.rack[i] & RK_TIER];
  uint32_t pct = 100u + D.s.fiber * 5u + D.s.boost * 3u;
  for (int i = 0; i < DC_PANELS; i++) pct += D.s.panel[i] == PN_OK ? 2u : (D.s.panel[i] == PN_DIRTY ? 1u : 0u);
  uint32_t v = base * pct / 100u;
  if (D.s.water_down) v /= 2;
  for (int i = 0; i < 4; i++) if (D.rb[i].on && D.rb[i].glitch) v -= v / 10;
  return v;
}

// ------------------------------------------------------------------ maps
static uint16_t s_solid[GC_ROWS];

static int solid(int x, int y) {
  if (x < 0 || x >= GC_COLS || y < 1 || y > 13) return 1;
  return (s_solid[y] >> x) & 1;
}

static void set_solid(int x, int y, int on) {
  if (on) s_solid[y] |= (uint16_t)(1u << x);
  else s_solid[y] &= (uint16_t)~(1u << x);
}

static void row(int y, int ground, int is_solid) {
  for (int x = 0; x < GC_COLS; x++) { g_ground[y][x] = (uint8_t)ground; g_obj[y][x] = T_NONE; }
  s_solid[y] = is_solid ? 0x7FFF : 0;
}

static void rows(int y0, int y1, int ground) { for (int y = y0; y <= y1; y++) row(y, ground, 0); }

static void put(int x, int y, int obj) { g_obj[y][x] = (uint8_t)obj; set_solid(x, y, 1); }

static void door(int x, int y, int tile) { g_ground[y][x] = (uint8_t)tile; g_obj[y][x] = T_NONE; set_solid(x, y, 0); }

static int rack_tile(int v) {
  static const uint8_t T[4] = {T_RACK_EMPTY, T_RACK1_A, T_RACK2_A, T_RACK3_A};
  if (!(v & RK_TIER)) return T_RACK_EMPTY;
  if (D.blip || (v & RK_DONE) != RK_DONE) return T_RACK_OFF;
  return T[v & RK_TIER];
}

static void build_hall(void) {
  row(1, T_WALLTOP, 1); row(2, T_WALL, 1);
  rows(3, 12, T_FLOOR);
  row(13, T_WALLTOP, 1);
  for (int y = 5; y <= 11; y += 3)
    for (int x = 2; x <= 12; x++) if (x != 7) g_ground[y][x] = T_VENT;
  door(2, 2, T_DOOR); door(7, 2, T_DOOR); door(12, 2, T_STAIRS);
  door(4, 13, T_DOOR); door(10, 13, T_DOOR);
  for (int i = 0; i < DC_RACKS; i++) {
    int r = i / 10, open = 1;
    if (r > 0) for (int k = (r - 1) * 10; k < r * 10; k++) if (!(D.s.rack[k] & RK_TIER)) open = 0;
    if ((D.s.rack[i] & RK_TIER) || open) put(dc_rack_x(i), dc_rack_y(i), rack_tile(D.s.rack[i]));
  }
  put(0, 12, T_CHARGER); put(14, 12, T_CHARGER);
}

static void build_net(void) {
  row(1, T_WALLTOP, 1); row(2, T_WALL, 1); row(3, T_WALL, 1); row(4, T_WALL, 1);
  rows(5, 12, T_FLOOR);
  row(13, T_WALLTOP, 1);
  for (int x = 3; x <= 11; x++) { g_obj[2][x] = T_SWITCH_A; g_obj[4][x] = T_PATCH; }
  for (int x = 2; x <= 12; x += 2) g_ground[9][x] = T_VENT;
  put(1, 8, T_RACK2_A); put(13, 8, T_RACK2_A); put(1, 11, T_SPOOL); put(13, 11, T_SPOOL);
  door(7, 13, T_DOOR);
}

static const uint8_t CRATE_SPOT[8][2] = {{8, 4}, {9, 4}, {10, 4}, {11, 4}, {8, 6}, {9, 6}, {10, 6}, {11, 6}};

static void build_dock(void) {
  row(1, T_WALLTOP, 1); row(2, T_WALL, 1);
  rows(3, 8, T_CONCRETE);
  rows(9, 13, T_ASPHALT);
  row(12, T_ROADLINE, 0);
  for (int x = 8; x <= 11; x++) g_ground[2][x] = T_SHUTTER;
  door(4, 2, T_DOOR);
  put(1, 7, T_PALLET); put(2, 7, T_PALLET); put(13, 7, T_CONE); put(13, 4, T_BARREL);
  int n = D.s.crates_srv + D.s.crates_pan;
  for (int i = 0; i < n && i < 8; i++)
    put(CRATE_SPOT[i][0], CRATE_SPOT[i][1], i < D.s.crates_srv ? T_CRATE : T_CRATE_PANEL);
  if (D.truck == 2)
    for (int y = 9; y <= 10; y++) for (int x = 8; x <= 10; x++) set_solid(x, y, 1);
}

static void build_water(void) {
  row(1, T_WALLTOP, 1); row(2, T_WALL, 1);
  rows(3, 8, T_CONCRETE);
  for (int y = 9; y <= 12; y++) row(y, T_WATER_A, 1);
  row(13, T_WALLTOP, 1);
  door(10, 2, T_DOOR);
  int towers = 1 + dc_racks_done() / 8;
  if (towers > 4) towers = 4;
  for (int i = 0; i < towers; i++) { put(1 + i * 2, 3, T_TOWER_TOP_A); put(1 + i * 2, 4, T_TOWER_BOT); }
  for (int x = 1; x <= 11; x++) put(x, 6, (x == 4 || x == 8) ? (D.s.water_down ? T_VALVE : T_VALVE_OK) : T_PIPE_H);
  put(12, 6, D.s.water_down ? T_PUMP_BAD : T_PUMP_OK);
  put(12, 7, T_PIPE_V); put(12, 8, T_PIPE_V);
  put(14, 4, T_BARREL);
}

static int panel_tile(int v) {
  static const uint8_t T[4] = {T_MOUNT, T_SOLAR_OK, T_SOLAR_DIRTY, T_SOLAR_CRACK};
  return T[v & 3];
}

static void build_roof(void) {
  row(1, T_SKY, 1); row(2, T_SKY, 1);
  for (int x = 0; x < GC_COLS; x++) g_obj[2][x] = T_PARAPET;
  rows(3, 13, T_GRAVEL);
  door(12, 13, T_STAIRS);
  for (int i = 0; i < DC_PANELS; i++) {
    int r = i / 6, open = 1;
    if (r > 0) for (int k = (r - 1) * 6; k < r * 6; k++) if (!D.s.panel[k]) open = 0;
    if (D.s.panel[i] || open) put(dc_panel_x(i), dc_panel_y(i), panel_tile(D.s.panel[i]));
  }
  put(10, 4, T_INVERTER); put(13, 4, T_AC); put(11, 8, T_AC); put(12, 8, T_AC);
}

static void build_office(void) {
  row(1, T_WALLTOP, 1); row(2, T_WALL, 1);
  rows(3, 12, T_CARPET);
  row(13, T_WALLTOP, 1);
  static const uint8_t WIN[4] = {4, 5, 9, 10};
  for (int i = 0; i < 4; i++) { g_ground[2][WIN[i]] = T_SKY; g_obj[2][WIN[i]] = T_WINDOW; }
  door(2, 13, T_DOOR);
  put(10, 4, T_COT_L); put(11, 4, T_COT_R);
  put(4, 3, T_DESK); put(7, 3, T_COFFEE); put(8, 3, T_BARREL); put(1, 3, T_PLANT); put(13, 3, T_PLANT); put(13, 12, T_PLANT);
  put(5, 9, T_DESK); put(6, 9, T_DESK); put(1, 12, T_CRATE); put(1, 11, T_CRATE); put(12, 9, T_SPOOL);
}

// ---- reachability (used by the walker and by the room generator)
static uint8_t s_dist[GC_ROWS][GC_COLS];
static int8_t  s_nav_x = -1, s_nav_y = -1;

static void bfs(int tx, int ty) {
  static uint8_t q[GC_ROWS * GC_COLS];
  for (int y = 0; y < GC_ROWS; y++) for (int x = 0; x < GC_COLS; x++) s_dist[y][x] = 255;
  int qh = 0, qn = 0;
  if (tx < 0 || tx >= GC_COLS || ty < 1 || ty > 13) return;
  s_dist[ty][tx] = 0;
  q[qn++] = (uint8_t)(ty * GC_COLS + tx);
  while (qh < qn) {
    int c = q[qh++], cx = c % GC_COLS, cy = c / GC_COLS, d = s_dist[cy][cx];
    static const int8_t DX[4] = {0, 0, -1, 1}, DY[4] = {1, -1, 0, 0};
    for (int k = 0; k < 4; k++) {
      int nx = cx + DX[k], ny = cy + DY[k];
      if (solid(nx, ny) || s_dist[ny][nx] != 255) continue;
      s_dist[ny][nx] = (uint8_t)(d + 1);
      q[qn++] = (uint8_t)(ny * GC_COLS + nx);
    }
  }
}

// ---- mission rooms: every room is rebuilt from (seed, room); nothing is stored
struct Theme { uint8_t floor, wall, node, node_ok, obs[4]; };
static const Theme THEMES[4] = {
  {T_CONCRETE, T_WALLTOP, T_VALVE, T_VALVE_OK, {T_PIPE_V, T_BARREL, T_PIPE_H, T_BARREL}},      // pipe tunnels
  {T_GRAVEL, T_SKY, T_SOLAR_CRACK, T_SOLAR_OK, {T_AC, T_SOLAR_OK, T_SOLAR_OK, T_AC}},          // roof sections
  {T_DUCT, T_DUCTWALL, T_JBOX, T_JBOX_OK, {T_SPOOL, T_CRATE, T_RACK_OFF, T_SPOOL}},            // cable ducts
  {T_FLOOR, T_WALLTOP, T_JBOX, T_JBOX_OK, {T_CRATE, T_CHARGER, T_RACK_EMPTY, T_CONE}},         // robot depot
};

static int theme_of(int type) {
  switch (type) {
    case GQ_MIS_COOLING: return 0;
    case GQ_MIS_SOLAR: case GQ_MIS_STORM: return 1;
    case GQ_MIS_FIBER: return 2;
    default: return 3;
  }
}

static uint32_t s_rs;
static uint32_t room_rnd(void) { s_rs ^= s_rs << 13; s_rs ^= s_rs >> 17; s_rs ^= s_rs << 5; return s_rs; }

static void robot_clear(int i) { D.rb[i].on = 0; D.rb[i].glitch = 0; }

static void build_room(void) {
  const Theme* th = &THEMES[D.m_theme & 3];
  int room = D.s.m_room, last = room + 1 >= D.s.m_rooms;
  s_rs = hash32(D.s.m_seed + (uint32_t)room * 0x9E3779B1u) | 1u;
  int wall = th->wall;
  row(1, wall, 1); row(13, wall, 1);
  if (D.m_theme == 1) { row(2, T_SKY, 1); for (int x = 0; x < GC_COLS; x++) g_obj[2][x] = T_PARAPET; }
  else if (D.m_theme == 2) row(2, wall, 1);
  else row(2, T_WALL, 1);
  rows(3, 12, th->floor);
  for (int y = 3; y <= 12; y++) {
    g_ground[y][0] = g_ground[y][14] = (uint8_t)wall;
    set_solid(0, y, 1); set_solid(14, y, 1);
  }
  int ey = 4 + (int)(hash32(D.s.m_seed + (uint32_t)room * 77u) % 8);          // entry, on the left wall
  int xy = 4 + (int)(hash32(D.s.m_seed + (uint32_t)(room + 1) * 77u) % 8);    // exit = the next room's entry row
  int dt = D.m_theme == 1 ? T_STAIRS : T_DOOR;
  door(0, ey, dt); door(14, xy, dt);
  D.exit_x = 14; D.exit_y = (uint8_t)xy;
  // obstacles: each one is kept only if the exit can still be reached from the entry
  int n = 7 + (int)(room_rnd() % 8);
  for (int i = 0; i < n; i++) {
    int x = 2 + (int)(room_rnd() % 11), y = 3 + (int)(room_rnd() % 10);
    int t = th->obs[room_rnd() & 3];
    if (g_obj[y][x] != T_NONE || (x <= 2 && y == ey) || (x >= 12 && y == xy)) continue;
    put(x, y, t);
    bfs(0, ey);
    if (s_dist[xy][14] == 255) { g_obj[y][x] = T_NONE; set_solid(x, y, 0); }
  }
  // work nodes: a node is solid, the technician works on it from the tile below
  for (int i = 0; i < DC_NODES; i++) D.node[i].on = 0;
  int nodes = last ? 1 : (D.s.m_type == GQ_MIS_ROBOTS ? 1 + (int)(room_rnd() % 2) : 2 + (int)(room_rnd() % 2));
  for (int i = 0, tries = 0; i < nodes && tries < 60; tries++) {
    int x = 2 + (int)(room_rnd() % 11), y = 3 + (int)(room_rnd() % 9);
    if (g_obj[y][x] != T_NONE || solid(x, y + 1) || (x <= 2 && y == ey) || (x >= 12 && y == xy)) continue;
    put(x, y, last && D.s.m_type == GQ_MIS_ROBOTS ? (int)T_CONSOLE : (int)th->node);
    bfs(0, ey);
    if (s_dist[xy][14] == 255 || s_dist[y + 1][x] == 255) { g_obj[y][x] = T_NONE; set_solid(x, y, 0); continue; }
    D.node[i].x = (uint8_t)x; D.node[i].y = (uint8_t)y; D.node[i].on = 1; D.node[i].done = 0;
    i++;
  }
  // rogue robots: always in the depot, now and then elsewhere; the last depot room holds the Overseer
  robot_clear(4); robot_clear(5);
  int robots = D.s.m_type == GQ_MIS_ROBOTS ? 1 : (room_rnd() % 7 == 0 ? 1 : 0);
  if (last && D.s.m_type == GQ_MIS_ROBOTS) robots = 1;
  bfs(0, ey);
  for (int i = 0, tries = 0; i < robots && tries < 80; tries++) {
    int x = 4 + (int)(room_rnd() % 9), y = 3 + (int)(room_rnd() % 10);
    if (solid(x, y) || s_dist[y][x] == 255) continue;
    DcRobot* r = &D.rb[4 + i];
    r->x = (int16_t)(x * 16); r->y = (int16_t)(y * 16);
    r->on = 1; r->glitch = 1; r->area = A_MISSION; r->acc = 0; r->wait = 0; r->flash = 0; r->flee = 0;
    r->kind = (uint8_t)(last && D.s.m_type == GQ_MIS_ROBOTS ? (uint32_t)RB_OVERSEER : room_rnd() % 4);
    r->hp = (uint8_t)(r->kind == RB_OVERSEER ? 5 : 2);
    i++;
  }
  s_nav_x = -1;
}

static void build_map(void) {
  for (int y = 0; y < GC_ROWS; y++) { row(y, T_WALLTOP, 1); }
  switch (D.area) {
    case A_HALL: build_hall(); break;
    case A_NET: build_net(); break;
    case A_DOCK: build_dock(); break;
    case A_WATER: build_water(); break;
    case A_ROOF: build_roof(); break;
    case A_OFFICE: build_office(); break;
    default: build_room(); break;
  }
  s_nav_x = -1;
  gc_mark_all_dirty();
}

static void rebuild(int area) { if (D.area == area) build_map(); }

// ------------------------------------------------------------------ the technician
static void hero_mark(void) { mark_rows(D.hy - 22, 40); }

static void hero_place(int tx, int ty) {
  hero_mark();
  D.hx = (int16_t)(tx * 16); D.hy = (int16_t)(ty * 16);
  D.walk_acc = 0;
  hero_mark();
}

static void robots_validate(void) {
  for (int i = 0; i < DC_ROBOTS; i++) {
    DcRobot* r = &D.rb[i];
    if (!r->on || r->area != D.area) continue;
    r->x = (int16_t)(r->x & ~15); r->y = (int16_t)(r->y & ~15);
    if (!solid(r->x >> 4, r->y >> 4)) continue;
    for (int y = 3; y <= 12 && solid(r->x >> 4, r->y >> 4); y++)
      for (int x = 1; x <= 13; x++)
        if (!solid(x, y)) { r->x = (int16_t)(x * 16); r->y = (int16_t)(y * 16); break; }
  }
}

static void enter_area(int area, int tx, int ty) {
  D.area = (uint8_t)area;
  build_map();
  hero_place(tx, ty);
  robots_validate();
  D.curtain = 300;
}

// Door tile in the hall that leads to an area, and the area's own door tile.
static const uint8_t HALL_DOOR[A_COUNT][2] = {{7, 3}, {7, 2}, {4, 13}, {10, 13}, {12, 2}, {2, 2}, {7, 3}};
static const uint8_t AREA_DOOR[A_COUNT][2] = {{7, 3}, {7, 13}, {4, 2}, {10, 2}, {12, 13}, {2, 13}, {7, 3}};

// One step of walking toward a tile of the current area. Returns 1 when he stands on it.
static int walk_to(int tx, int ty, uint32_t dt) {
  if (s_nav_x != tx || s_nav_y != ty) { bfs(tx, ty); s_nav_x = (int8_t)tx; s_nav_y = (int8_t)ty; }
  D.walk_acc = (uint16_t)(D.walk_acc + dt);
  while (D.walk_acc >= STEP_MS) {
    D.walk_acc = (uint16_t)(D.walk_acc - STEP_MS);
    if (!(D.hx & 15) && !(D.hy & 15)) {
      int cx = D.hx >> 4, cy = D.hy >> 4;
      if (cx == tx && cy == ty) { D.walk_acc = 0; return 1; }
      int d = (cy >= 0 && cy < GC_ROWS && cx >= 0 && cx < GC_COLS) ? s_dist[cy][cx] : 255;
      if (d == 255 || d == 0) { hero_place(tx, ty); return 1; }       // cut off (the map changed): never get stuck
      static const int8_t DX[4] = {0, 0, -1, 1}, DY[4] = {1, -1, 0, 0};
      int best = -1;
      for (int k = 0; k < 4; k++) {
        int nx = cx + DX[k], ny = cy + DY[k];
        if (nx < 0 || nx >= GC_COLS || ny < 1 || ny > 13 || s_dist[ny][nx] != d - 1) continue;
        if (best < 0 || k == D.hdir) best = k;
      }
      if (best < 0) { hero_place(tx, ty); return 1; }
      D.hdir = (uint8_t)best;
    }
    hero_mark();
    if (D.hdir == DIR_DOWN) D.hy++; else if (D.hdir == DIR_UP) D.hy--;
    else if (D.hdir == DIR_LEFT) D.hx--; else D.hx++;
    if (++D.hsub >= 6) { D.hsub = 0; D.hstep = (uint8_t)((D.hstep + 1) & 3); }
    hero_mark();
  }
  return 0;
}

// Walks to a tile of any base area, through the hall. Returns 1 when he stands on it.
static int go(int area, int tx, int ty, uint32_t dt) {
  if (D.curtain) return 0;
  D.hpose = POSE_STAND;
  if (D.area == area) return walk_to(tx, ty, dt);
  if (D.area == A_MISSION) { enter_area(A_HALL, 7, 3); return 0; }     // never happens in normal play
  if (D.area == A_HALL) {
    if (walk_to(HALL_DOOR[area][0], HALL_DOOR[area][1], dt)) enter_area(area, AREA_DOOR[area][0], AREA_DOOR[area][1]);
  } else {
    int from = D.area;
    if (walk_to(AREA_DOOR[from][0], AREA_DOOR[from][1], dt)) enter_area(A_HALL, HALL_DOOR[from][0], HALL_DOOR[from][1]);
  }
  return 0;
}

static void job_done(void) {
  D.job = GQ_ACT_NONE; D.ph = 0; D.t = 0; D.job_ms = 0; D.stun = 0;
  D.hitem = -1; D.hemote = -1;
  if (D.hpose != POSE_STAND) { D.hpose = POSE_STAND; hero_mark(); }
  D.wire_slot = -1; D.wire_stage = 0;
}

static void job_start(int job, int arg) {
  job_done();
  D.job = (uint8_t)job; D.arg = (int16_t)arg; D.arg2 = 0;
}

// Stands and works for `ms`. Returns 1 when the time is up.
static int work(uint32_t dt, uint32_t ms, int icon, int pose) {
  if (D.hpose != pose) { D.hpose = (uint8_t)pose; hero_mark(); }
  D.hemote = (int8_t)icon;
  D.t += dt;
  if (D.t < ms) return 0;
  D.t = 0; D.hpose = POSE_STAND; D.hemote = -1;
  hero_mark();
  return 1;
}

// ------------------------------------------------------------------ the day plan (a pure function of the day)
static uint32_t day_hash(int salt) {
  uint32_t d = D.year > 0 ? (uint32_t)D.year * 372u + D.month * 31u + D.mday : 0u;
  return hash32(D.s.day * 2654435761u + d * 40503u + (uint32_t)salt * 97u + 1u);
}

static int deliveries_today(void) { return D.s.day <= 7 ? 2 : 1 + (int)(day_hash(1) & 1); }   // trucks
static int delivery_minute(int k) { return (k == 0 ? 9 * 60 : 13 * 60) + (int)(day_hash(2 + k) % 170); }
static int mission_minute(void) { return 9 * 60 + 30 + (int)(day_hash(5) % 390); }
static int event_minute(void) { return 10 * 60 + (int)(day_hash(6) % 420); }

static void truck_arrive(void) {
  uint32_t h = day_hash(10 + D.s.deliv_done);
  int n = 1 + (D.s.day <= 7 ? (int)(h & 1) : 0);     // crates on this truck
  D.load_srv = D.load_pan = 0;
  for (int i = 0; i < n; i++) {
    int pan = ((h >> (2 + i * 2)) & 3) == 0 && panels_total() + D.s.crates_pan + D.load_pan < DC_PANELS;
    if (pan) D.load_pan++; else D.load_srv++;
  }
  if (D.s.crates_srv + D.s.crates_pan + n > 8) { D.load_srv = D.load_pan = 0; return; }   // the dock is full
  D.truck = 1; D.truck_x = 250; D.truck_acc = 0;
  if (D.s.deliv_done < 255) D.s.deliv_done++;
  rebuild(A_DOCK);
}

static void wear_panels(void) {
  int wx = dc_wx();
  int dirt = wx == GQ_W_STORM ? 25 : ((wx == GQ_W_WINDY || wx == GQ_W_SNOW) ? 20 : 8);
  int crack = wx == GQ_W_STORM ? 10 : 1;
  for (int i = 0; i < DC_PANELS; i++) {
    if (D.s.panel[i] != PN_OK) continue;
    uint32_t r = gc_rnd();
    if ((int)(r % 100) < crack) D.s.panel[i] = PN_CRACKED;
    else if ((int)((r >> 8) % 100) < dirt) D.s.panel[i] = PN_DIRTY;
  }
  rebuild(A_ROOF);
}

static void new_day(void) {
  if (D.s.day < 65535) D.s.day++;
  D.s.deliv_done = 0; D.s.ev_done = 0;
  D.hhp = 3;
  wear_panels();
  touch();
}

static void crew_update(void) {
  static const uint8_t NEED[3] = {3, 8, 14};
  static const uint8_t HOME[4][2] = {{1, 5}, {13, 8}, {1, 11}, {9, 11}};
  int racks = dc_racks_done();
  for (int i = 0; i < 4; i++) {
    DcRobot* r = &D.rb[i];
    int want = i < 3 ? racks >= NEED[i] : panels_total() >= 6;
    if (want && !r->on) {
      r->on = 1; r->glitch = 0; r->kind = (uint8_t)i; r->hp = 3;
      r->area = (uint8_t)(i < 3 ? A_HALL : A_ROOF);
      r->x = (int16_t)(HOME[i][0] * 16); r->y = (int16_t)(HOME[i][1] * 16);
      r->acc = 0; r->wait = 0; r->flash = 0; r->flee = 0;
    }
  }
}

static void on_minute(void) {
  if (D.job == GQ_ACT_MISSION && D.area == A_MISSION && D.s.m_min < 255) D.s.m_min++;
  crew_update();
  if (dc_asleep() || sleep_due()) return;
  int m = D.s.minute;
  if (!D.truck && D.s.deliv_done < deliveries_today() && m >= delivery_minute(D.s.deliv_done) && m < 19 * 60)
    truck_arrive();
  if (!(D.s.ev_done & 1) && m >= event_minute() && m < 19 * 60 && !D.ev_pending) {
    D.s.ev_done |= 1;
    D.ev_pending = (uint8_t)(1 + day_hash(7) % 3);
  }
  if (D.s.m_type || m < 7 * 60) return;
  if (!D.s.water_down && gc_rnd() % 900 == 0) { D.s.water_down = 1; rebuild(A_WATER); touch(); }
  if (glitched_robot(1) < 0 && gc_rnd() % 480 == 0) {
    int k = (int)(gc_rnd() % 4);
    for (int i = 0; i < 4; i++) {
      DcRobot* r = &D.rb[(k + i) % 4];
      if (r->on) { r->glitch = 1; r->hp = (uint8_t)(4 + gc_rnd() % 2); break; }
    }
  }
}

// ------------------------------------------------------------------ fights
// One step of fighting robot `idx` (same area). Returns 0 while it lasts, 1 when he won, 2 when he is knocked flat.
static int fight_step(int idx, uint32_t dt) {
  DcRobot* r = &D.rb[idx];
  if (!r->on || !r->glitch) return 1;
  if (D.stun > 0) {
    D.stun = (int16_t)(D.stun - (int16_t)dt);
    if (D.stun <= 0) { D.stun = 0; D.hpose = POSE_STAND; hero_mark(); if (D.hhp == 0) return 2; }
    return 0;
  }
  int rtx = (r->x + 8) >> 4, rty = (r->y + 8) >> 4;
  int aligned = !(D.hx & 15) && !(D.hy & 15);
  int dx = rtx - (D.hx >> 4), dy = rty - (D.hy >> 4);
  int near = aligned && (dx * dx + dy * dy) <= 1 && r->flee <= 0;
  if (!near) { D.hemote = I_ALERT; walk_to(rtx, rty, dt); D.t = 0; return 0; }
  r->wait = 400;                                   // he holds it in place
  if (dx) D.hdir = (uint8_t)(dx < 0 ? DIR_LEFT : DIR_RIGHT); else if (dy) D.hdir = (uint8_t)(dy < 0 ? DIR_UP : DIR_DOWN);
  if (D.hpose != POSE_WORK) { D.hpose = POSE_WORK; hero_mark(); }
  D.hemote = I_WRENCH;
  D.t += dt;
  if (D.t < 1300) return 0;
  D.t = 0;
  if (gc_rnd() % 100 < 68) {
    r->flash = 3;
    if (r->hp) r->hp--;
    mark_rows(r->y - 8, 32);
    if (!r->hp) {
      r->glitch = 0; r->hp = 3; r->wait = 3000;
      if (idx >= 4) r->on = 0;
      if (D.s.fights < 65535) D.s.fights++;
      D.hpose = POSE_STAND; D.hemote = -1;
      touch();
      return 1;
    }
    r->flee = 1800; r->wait = 0;                   // it bolts; he has to catch it again
    D.hpose = POSE_STAND;
  } else {
    if (D.hhp) D.hhp--;
    D.hpose = POSE_HURT; D.hemote = -1; D.stun = 1200;
    hero_mark();
  }
  return 0;
}

// ------------------------------------------------------------------ missions
static void mission_gate(int type, int* area, int* x, int* y) {
  switch (type) {
    case GQ_MIS_COOLING: *area = A_WATER; *x = 13; *y = 5; break;
    case GQ_MIS_SOLAR: case GQ_MIS_STORM: *area = A_ROOF; *x = 9; *y = 12; break;
    case GQ_MIS_FIBER: *area = A_NET; *x = 12; *y = 6; break;
    default: *area = A_HALL; *x = 13; *y = 12; break;
  }
}

static void room_enter(void) {
  D.m_theme = (uint8_t)theme_of(D.s.m_type);
  D.area = A_MISSION;
  build_map();
  int ey = 4 + (int)(hash32(D.s.m_seed + (uint32_t)D.s.m_room * 77u) % 8);
  hero_place(0, ey);
  D.hdir = DIR_RIGHT;
  D.curtain = 300;
  D.hhp = 3;                                       // he catches his breath between rooms
}

static void mission_leave(void) {
  int area, x, y;
  mission_gate(D.s.m_type ? (int)D.s.m_type : (int)GQ_MIS_ROBOTS, &area, &x, &y);
  robot_clear(4); robot_clear(5);
  enter_area(area, x, y);
}

static int pick_mission(void) {
  int cracked = 0, empty = 0;
  for (int i = 0; i < DC_PANELS; i++) { if (D.s.panel[i] == PN_CRACKED) cracked++; if (!D.s.panel[i]) empty++; }
  if (D.s.water_down) return GQ_MIS_COOLING;
  if (cracked >= 2 || dc_wx() == GQ_W_STORM) return GQ_MIS_STORM;
  switch (day_hash(8) & 3) {
    case 0: return GQ_MIS_COOLING;
    case 1: return empty ? GQ_MIS_SOLAR : GQ_MIS_FIBER;
    case 2: return GQ_MIS_FIBER;
    default: return D.rb[0].on ? GQ_MIS_ROBOTS : GQ_MIS_FIBER;
  }
}

static void mission_begin(int type, uint32_t seed) {
  D.s.m_type = (uint8_t)type;
  D.s.m_seed = seed ? seed : 1u;
  D.s.m_rooms = (uint8_t)(20 + hash32(D.s.m_seed) % 11);
  D.s.m_room = 0; D.s.m_min = 0;
  D.s.m_day = (uint8_t)D.s.day;
  if (type == GQ_MIS_COOLING && !D.s.water_down) { D.s.water_down = 1; rebuild(A_WATER); }
  touch();
}

static void mission_finish(void) {
  switch (D.s.m_type) {
    case GQ_MIS_COOLING: D.s.water_down = 0; break;
    case GQ_MIS_SOLAR:
      for (int i = 0, n = 0; i < DC_PANELS && n < 3; i++) if (!D.s.panel[i]) { D.s.panel[i] = PN_OK; n++; }
      break;
    case GQ_MIS_STORM:
      for (int i = 0; i < DC_PANELS; i++) if (D.s.panel[i]) D.s.panel[i] = PN_OK;
      break;
    case GQ_MIS_FIBER: if (D.s.fiber < 20) D.s.fiber++; break;
    default:
      if (D.s.boost < 250) D.s.boost++;
      for (int i = 0; i < 4; i++) D.rb[i].glitch = 0;
      break;
  }
  if (D.s.missions < 65535) D.s.missions++;
  mission_leave();
  D.s.m_type = 0; D.s.m_room = 0; D.s.m_rooms = 0; D.s.m_min = 0;
  touch();
}

static int mission_due(void) {
  int m = D.s.minute;
  return D.s.m_day != (uint8_t)D.s.day && m >= mission_minute() && m < 17 * 60 + 30;
}

static void job_mission(uint32_t dt) {
  if (D.ph == 0) {
    int type = D.s.m_type ? D.s.m_type : (D.arg ? D.arg & 7 : pick_mission());
    D.arg = (int16_t)type;
    int area, x, y;
    mission_gate(type, &area, &x, &y);
    if (D.area != A_MISSION && !go(area, x, y, dt)) return;
    if (!D.s.m_type) mission_begin(type, hash32(gc_rnd() ^ day_hash(9)));
    room_enter();
    D.ph = 1;
    return;
  }
  if (D.ph == 9) { if (work(dt, 2500, I_BUBBLE, POSE_CHEER)) job_done(); return; }
  if (D.curtain) return;
  int rg = glitched_robot(0);
  if (rg >= 4) {
    int f = fight_step(rg, dt);
    if (f == 2) { mission_leave(); job_start(GQ_ACT_REST, 0); }
    return;
  }
  for (int i = 0; i < DC_NODES; i++) {
    DcNode* n = &D.node[i];
    if (!n->on || n->done) continue;
    if (D.t == 0 && !walk_to(n->x, n->y + 1, dt)) return;
    D.hdir = DIR_UP;
    int icon = D.m_theme == 2 ? I_SNIPS : I_WRENCH;
    if (D.t == 0) D.t = 1;
    if (!work(dt, 30000, icon, POSE_WORK)) return;
    n->done = 1;
    g_obj[n->y][n->x] = (uint8_t)(g_obj[n->y][n->x] == T_CONSOLE ? (int)T_CONSOLE_OK : (int)THEMES[D.m_theme & 3].node_ok);
    mark_rows(n->y * 16, 16);
    return;
  }
  if (!walk_to(D.exit_x, D.exit_y, dt)) return;
  D.s.m_room++;
  touch();
  if (D.s.m_room >= D.s.m_rooms || D.s.m_min >= 70) { mission_finish(); D.ph = 9; D.t = 0; return; }
  if (sleep_due()) { mission_leave(); job_done(); return; }     // carries on tomorrow from this room
  room_enter();
}

// ------------------------------------------------------------------ jobs
static int crate_spot_x(int i) { return CRATE_SPOT[i & 7][0]; }
static int crate_spot_y(int i) { return CRATE_SPOT[i & 7][1] + 1; }

static int next_install_slot(void) {
  for (int i = 0; i < DC_RACKS; i++) if (!(D.s.rack[i] & RK_TIER)) return i;
  for (int t = 1; t <= 2; t++)
    for (int i = 0; i < DC_RACKS; i++) if ((D.s.rack[i] & RK_TIER) == t) return i;
  return -1;
}

static void job_install(uint32_t dt) {
  int slot = D.arg;
  switch (D.ph) {
    case 0:
      for (int i = 0; i < DC_RACKS; i++) {
        int v = D.s.rack[i];
        if ((v & RK_TIER) && !(v & RK_WIRED)) { D.arg = (int16_t)i; D.ph = 4; return; }
        if ((v & RK_TIER) && !(v & RK_PATCHED)) { D.arg = (int16_t)i; D.ph = 7; return; }
      }
      if (!D.s.crates_srv) { job_done(); return; }
      D.arg = (int16_t)next_install_slot();
      D.ph = 1;
      return;
    case 1:
      if (!go(A_DOCK, crate_spot_x(D.s.crates_srv - 1), crate_spot_y(D.s.crates_srv - 1), dt)) return;
      D.hdir = DIR_UP;
      if (!work(dt, 2500, I_BUBBLE, POSE_STAND)) return;
      if (D.s.crates_srv) D.s.crates_srv--;
      D.hitem = I_CARRY_CRATE;
      rebuild(A_DOCK); touch();
      if (slot < 0) D.arg2 = (int16_t)(gc_rnd() % DC_RACKS);
      D.ph = 2;
      return;
    case 2: {
      int at = slot >= 0 ? slot : D.arg2;
      if (!go(A_HALL, dc_rack_x(at), dc_rack_y(at) + 1, dt)) return;
      D.hdir = DIR_UP; D.hitem = I_CARRY_SERVER; D.ph = 3;
      return;
    }
    case 3:
      if (!work(dt, 240000, I_WRENCH, POSE_WORK)) return;
      D.hitem = -1;
      if (D.s.installs < 65535) D.s.installs++;
      if (slot < 0) { if (D.s.boost < 250) D.s.boost++; D.ph = 9; }
      else if (!(D.s.rack[slot] & RK_TIER)) { D.s.rack[slot] = 1; D.ph = 4; }
      else { D.s.rack[slot] = (uint8_t)(D.s.rack[slot] + 1); D.ph = 9; }
      rebuild(A_HALL); touch();
      return;
    case 4:
      if (!go(A_HALL, dc_rack_x(slot), dc_rack_y(slot) - 1, dt)) return;
      D.wire_slot = (int8_t)slot; D.wire_stage = 1; D.hitem = -1; D.ph = 5;
      return;
    case 5:
      D.hemote = I_SNIPS;
      if (!go(A_HALL, 7, dc_rack_y(slot) - 1, dt)) return;
      D.wire_stage = 2; D.ph = 6;
      return;
    case 6:
      if (!go(A_HALL, 7, 3, dt)) return;
      D.s.rack[slot] |= RK_WIRED;
      D.wire_slot = -1; D.wire_stage = 0; D.hemote = -1;
      touch(); gc_mark_all_dirty();
      D.ph = 7;
      return;
    case 7:
      if (!go(A_NET, 3 + slot % 9, 5, dt)) return;
      D.hdir = DIR_UP; D.ph = 8;
      return;
    case 8:
      if (!work(dt, 90000, I_SNIPS, POSE_WORK)) return;
      D.s.rack[slot] |= RK_PATCHED;
      touch(); gc_mark_all_dirty();
      D.ph = 9;
      return;
    default:
      if (work(dt, 2000, I_BUBBLE, POSE_CHEER)) job_done();
      return;
  }
}

static int first_panel(int state_a, int state_b) {
  for (int i = 0; i < DC_PANELS; i++) if (D.s.panel[i] == state_a || D.s.panel[i] == state_b) return i;
  return -1;
}

static void job_panel(uint32_t dt) {
  switch (D.ph) {
    case 0: {
      D.arg = (int16_t)first_panel(PN_NONE, PN_NONE);
      if (D.arg < 0 || !D.s.crates_pan) { D.s.crates_pan = 0; rebuild(A_DOCK); job_done(); return; }
      D.ph = 1;
      return;
    }
    case 1: {
      int i = D.s.crates_srv + D.s.crates_pan - 1;
      if (!go(A_DOCK, crate_spot_x(i), crate_spot_y(i), dt)) return;
      D.hdir = DIR_UP;
      if (!work(dt, 2500, I_BUBBLE, POSE_STAND)) return;
      if (D.s.crates_pan) D.s.crates_pan--;
      D.hitem = I_CARRY_PANEL;
      rebuild(A_DOCK); touch();
      D.ph = 2;
      return;
    }
    case 2:
      if (!go(A_ROOF, dc_panel_x(D.arg), dc_panel_y(D.arg) + 1, dt)) return;
      D.hdir = DIR_UP; D.ph = 3;
      return;
    case 3:
      if (!work(dt, 60000, I_WRENCH, POSE_WORK)) return;
      D.hitem = -1;
      D.s.panel[D.arg] = PN_OK;
      rebuild(A_ROOF); touch();
      D.ph = 4;
      return;
    default:
      if (work(dt, 2000, I_BUBBLE, POSE_CHEER)) job_done();
      return;
  }
}

static void job_clean(uint32_t dt) {
  if (D.ph == 0) {
    D.arg = (int16_t)first_panel(PN_CRACKED, PN_DIRTY);
    if (D.arg < 0 || D.arg2 >= 3) { job_done(); return; }
    D.ph = 1;
  }
  if (D.ph == 1) {
    if (!go(A_ROOF, dc_panel_x(D.arg), dc_panel_y(D.arg) + 1, dt)) return;
    D.hdir = DIR_UP; D.ph = 2;
    return;
  }
  int cracked = D.s.panel[D.arg] == PN_CRACKED;
  if (!work(dt, cracked ? 60000 : 25000, cracked ? I_WRENCH : I_BROOM, POSE_WORK)) return;
  D.s.panel[D.arg] = PN_OK;
  rebuild(A_ROOF); touch();
  D.arg2++; D.ph = 0;
}

static void job_water(uint32_t dt) {
  static const uint8_t STOP[4] = {12, 4, 8, 12};
  static const uint8_t SECS[4] = {100, 50, 50, 30};
  int k = D.ph >> 1;
  if (k >= 4) {
    if (D.ph == 8) { D.s.water_down = 0; rebuild(A_WATER); touch(); D.ph = 9; }
    if (work(dt, 2000, I_BUBBLE, POSE_CHEER)) job_done();
    return;
  }
  if (!(D.ph & 1)) {
    if (!go(A_WATER, STOP[k], 5, dt)) return;
    D.hdir = DIR_DOWN; D.ph++;
    return;
  }
  if (work(dt, SECS[k] * 1000u, I_WRENCH, POSE_WORK)) D.ph++;
}

static void job_receive(uint32_t dt) {
  if (D.truck != 2) { job_done(); return; }
  if (D.ph == 0) {
    if (!go(A_DOCK, 7, 10, dt)) return;
    D.hdir = DIR_RIGHT; D.ph = 1;
    return;
  }
  if (!work(dt, 8000, I_CLIPBOARD, POSE_STAND)) return;
  D.s.crates_srv = (uint8_t)(D.s.crates_srv + D.load_srv);
  D.s.crates_pan = (uint8_t)(D.s.crates_pan + D.load_pan);
  D.load_srv = D.load_pan = 0;
  D.truck = 3; D.truck_acc = 0;
  rebuild(A_DOCK); touch();
  job_done();
}

static void job_rounds(uint32_t dt) {
  if (D.ph == 0) {
    int n = 0, pick = -1;
    for (int i = 0; i < DC_RACKS; i++) if ((D.s.rack[i] & RK_DONE) == RK_DONE && gc_rnd() % (uint32_t)(++n) == 0) pick = i;
    if (pick < 0 || D.arg2 >= 3) { job_done(); return; }
    D.arg = (int16_t)pick; D.ph = 1;
  }
  if (D.ph == 1) {
    if (!go(A_HALL, dc_rack_x(D.arg), dc_rack_y(D.arg) + 1, dt)) return;
    D.hdir = DIR_UP; D.ph = 2;
    return;
  }
  if (!work(dt, 28000, I_WRENCH, POSE_WORK)) return;
  D.arg2++; D.ph = 0;
}

static void job_coffee(uint32_t dt) {
  switch (D.ph) {
    case 0: if (go(A_OFFICE, 7, 4, dt)) { D.hdir = DIR_UP; D.ph = 1; } return;
    case 1: if (work(dt, 14000, I_CUP, POSE_STAND)) { if (D.hhp < 3) D.hhp++; D.ph = 2; } return;
    case 2: if (go(A_OFFICE, 4, 4, dt)) { D.hdir = DIR_UP; D.ph = 3; } return;
    default: if (work(dt, 45000, -1, POSE_STAND)) job_done(); return;
  }
}

static void job_inspect(uint32_t dt) {
  static const uint8_t WP[3][3][2] = {
    {{3, 8}, {11, 8}, {7, 6}},      // network room
    {{2, 5}, {6, 5}, {11, 5}},      // water plant
    {{4, 5}, {9, 9}, {4, 11}},      // roof
  };
  int a = D.arg == A_WATER ? 1 : (D.arg == A_ROOF ? 2 : 0);
  int k = D.ph >> 1;
  if (k >= 3) { job_done(); return; }
  if (!(D.ph & 1)) { if (go(D.arg, WP[a][k][0], WP[a][k][1], dt)) { D.hdir = (uint8_t)(a == 2 ? DIR_UP : DIR_DOWN); D.ph++; } return; }
  if (work(dt, 9000, I_CLIPBOARD, POSE_STAND)) D.ph++;
}

static void job_event(uint32_t dt) {
  switch (D.arg) {
    case 1:                                              // fire drill: everybody out to the yard
      if (D.ph == 0) { if (go(A_DOCK, 3, 10, dt)) { D.hdir = DIR_UP; D.ph = 1; } return; }
      if (work(dt, 45000, I_ALERT, POSE_STAND)) job_done();
      return;
    case 2:                                              // the coffee machine broke down
      if (D.ph == 0) { if (go(A_OFFICE, 7, 4, dt)) { D.hdir = DIR_UP; D.ph = 1; } return; }
      if (work(dt, 60000, I_WRENCH, POSE_WORK)) job_done();
      return;
    default:                                             // power blip: the racks go dark until the breakers are reset
      if (D.ph == 0) { D.blip = 1; rebuild(A_HALL); D.ph = 1; }
      if (D.ph == 1) { if (go(A_NET, 12, 6, dt)) { D.hdir = DIR_UP; D.ph = 2; } return; }
      if (work(dt, 30000, I_BOLT, POSE_WORK)) { D.blip = 0; rebuild(A_HALL); job_done(); }
      return;
  }
}

static void job_fight(uint32_t dt) {
  DcRobot* r = &D.rb[D.arg & 3];
  if (D.ph == 9) { if (work(dt, 2000, I_BUBBLE, POSE_CHEER)) job_done(); return; }
  if (!r->on || !r->glitch) { job_done(); return; }
  if (D.area != r->area) { D.hemote = I_ALERT; go(r->area, AREA_DOOR[r->area][0], AREA_DOOR[r->area][1], dt); return; }
  if (D.curtain) return;
  int f = fight_step(D.arg & 3, dt);
  if (f == 1) { D.ph = 9; D.t = 0; }
  else if (f == 2) job_start(GQ_ACT_REST, 0);
}

// Sleeping and resting both happen on the cot. arg 0 = rest after a knock (2 minutes), 1 = sleep.
static void job_bed(uint32_t dt) {
  if (D.ph == 0) {
    if (!go(A_OFFICE, 10, 5, dt)) return;
    D.hpose = POSE_BED; D.hemote = I_ZZZ; D.ph = 1; D.t = 0;
    hero_mark();
    return;
  }
  D.t += dt;
  int up = D.job == GQ_ACT_SLEEP ? !sleep_due() : D.t >= 120000;
  if (!up) return;
  D.hhp = 3;
  for (int i = 0; i < 4; i++) if (D.job == GQ_ACT_REST) D.rb[i].glitch = 0;   // the robot reboots by itself meanwhile
  job_done();
}

static int emergency(void) {
  return glitched_robot(1) >= 0 || D.s.water_down || D.truck == 2 || sleep_due() || (!D.s.m_type && mission_due());
}

static void pick_job(void) {
  int m = D.s.minute;
  if (sleep_due()) { job_start(GQ_ACT_SLEEP, 1); return; }
  if (D.hhp == 0) { job_start(GQ_ACT_REST, 0); return; }
  int g = glitched_robot(1);
  if (g >= 0) { job_start(GQ_ACT_FIGHT, g); return; }
  if (D.s.m_type || mission_due()) { job_start(GQ_ACT_MISSION, 0); return; }
  if (D.s.water_down) { job_start(GQ_ACT_WATER, 0); return; }
  if (D.truck == 2) { job_start(GQ_ACT_RECEIVE, 0); return; }
  for (int i = 0; i < DC_RACKS; i++)
    if ((D.s.rack[i] & RK_TIER) && (D.s.rack[i] & RK_DONE) != RK_DONE) { job_start(GQ_ACT_INSTALL, 0); return; }
  if (D.s.crates_srv) { job_start(GQ_ACT_INSTALL, 0); return; }
  if (D.s.crates_pan) { job_start(GQ_ACT_PANEL, 0); return; }
  if (D.ev_pending) { int e = D.ev_pending; D.ev_pending = 0; job_start(GQ_ACT_EVENT, e); return; }
  int day = m >= 7 * 60 && m < 19 * 60;
  if (day && first_panel(PN_CRACKED, PN_DIRTY) >= 0 && (D.idle_n & 1)) { D.idle_n++; job_start(GQ_ACT_CLEAN, 0); return; }
  D.idle_n++;
  switch (gc_rnd() % 10) {
    case 0: case 1: case 2: case 3: job_start(GQ_ACT_ROUNDS, 0); return;
    case 4: job_start(GQ_ACT_COFFEE, 0); return;
    case 5: job_start(GQ_ACT_INSPECT, A_NET); return;
    case 6: job_start(GQ_ACT_INSPECT, A_WATER); return;
    case 7: job_start(GQ_ACT_INSPECT, day ? A_ROOF : A_NET); return;
    case 8: job_start(GQ_ACT_INSPECT, A_NET); return;
    default: job_start(GQ_ACT_ROUNDS, 0); return;
  }
}

static void hero_tick(uint32_t dt) {
  if (D.job == GQ_ACT_NONE) pick_job();
  // idle jobs give way at once; a job that hangs is dropped (he walks home through the hall)
  if (D.job <= GQ_ACT_CLEAN && D.hitem < 0 && emergency()) { job_done(); pick_job(); }
  D.job_ms += dt;
  if (D.job_ms > 25u * 60 * 1000 && D.job != GQ_ACT_MISSION && D.job != GQ_ACT_SLEEP && D.job != GQ_ACT_REST) {
    if (D.aborts < 65535) D.aborts++;
    D.blip = 0;
    job_done();
    return;
  }
  switch (D.job) {
    case GQ_ACT_ROUNDS: job_rounds(dt); break;
    case GQ_ACT_COFFEE: job_coffee(dt); break;
    case GQ_ACT_INSPECT: job_inspect(dt); break;
    case GQ_ACT_CLEAN: job_clean(dt); break;
    case GQ_ACT_EVENT: job_event(dt); break;
    case GQ_ACT_PANEL: job_panel(dt); break;
    case GQ_ACT_INSTALL: job_install(dt); break;
    case GQ_ACT_RECEIVE: job_receive(dt); break;
    case GQ_ACT_WATER: job_water(dt); break;
    case GQ_ACT_MISSION: job_mission(dt); break;
    case GQ_ACT_FIGHT: job_fight(dt); break;
    case GQ_ACT_REST: case GQ_ACT_SLEEP: job_bed(dt); break;
    default: job_done(); break;
  }
}

// ------------------------------------------------------------------ robots and the truck
static void robots_tick(uint32_t dt) {
  for (int i = 0; i < DC_ROBOTS; i++) {
    DcRobot* r = &D.rb[i];
    if (!r->on || r->area != D.area) continue;
    if (r->flee > 0) r->flee = (int16_t)(r->flee - (int16_t)dt);
    if (r->wait > 0) { r->wait = (int16_t)(r->wait - (int16_t)dt); continue; }
    r->acc = (uint16_t)(r->acc + dt);
    int step = r->flee > 0 ? 15 : (r->glitch ? 30 : 40);
    while (r->acc >= step) {
      r->acc = (uint16_t)(r->acc - step);
      if (!(r->x & 15) && !(r->y & 15)) {
        if (!r->glitch && gc_rnd() % 3 == 0) { r->wait = (int16_t)(600 + gc_rnd() % 2400); r->acc = 0; break; }
        static const int8_t DX[4] = {0, 0, -1, 1}, DY[4] = {1, -1, 0, 0};
        int k0 = (gc_rnd() % 3 == 0) ? (int)(gc_rnd() & 3) : r->dir, moved = 0;
        for (int k = 0; k < 4; k++) {
          int d = (k0 + k) & 3;
          if (solid((r->x >> 4) + DX[d], (r->y >> 4) + DY[d])) { k0 = (int)(gc_rnd() & 3); continue; }
          r->dir = (uint8_t)d; moved = 1;
          break;
        }
        if (!moved) { r->wait = 1000; break; }
      }
      mark_rows(r->y - 8, 32);
      if (r->dir == DIR_DOWN) r->y++; else if (r->dir == DIR_UP) r->y--;
      else if (r->dir == DIR_LEFT) r->x--; else r->x++;
      r->anim++;
    }
  }
}

static void truck_tick(uint32_t dt) {
  if (D.truck != 1 && D.truck != 3) return;
  D.truck_acc = (uint16_t)(D.truck_acc + dt);
  while (D.truck_acc >= 16) {
    D.truck_acc = (uint16_t)(D.truck_acc - 16);
    D.truck_x--;
    if (D.area == A_DOCK) mark_rows(9 * 16, 32);
    if (D.truck == 1 && D.truck_x <= 8 * 16) { D.truck = 2; rebuild(A_DOCK); break; }
    if (D.truck == 3 && D.truck_x < -52) { D.truck = 0; break; }
  }
}

// ------------------------------------------------------------------ look
static void look_tick(void) {
  int m = D.s.minute, h = m / 60;
  int outdoor = D.area == A_ROOF || D.area == A_DOCK || (D.area == A_MISSION && D.m_theme == 1);
  int r = 256, g = 256, b = 256;
  if (outdoor) {
    if (h >= 20 || h < 5) { r = 110; g = 125; b = 190; }
    else if (h < 7 || h >= 18) { r = 240; g = 200; b = 180; }
    int wx = dc_wx();
    if (wx == GQ_W_RAIN || wx == GQ_W_STORM || wx == GQ_W_FOG) { r = r * 200 >> 8; g = g * 205 >> 8; b = b * 215 >> 8; }
  } else if (dc_is_night() && dc_asleep()) { r = 150; g = 155; b = 200; }
  set_tint(r, g, b);
}

// ------------------------------------------------------------------ scene
static void defaults(DcSave* s) {
  uint8_t* p = (uint8_t*)s;
  for (unsigned i = 0; i < sizeof(DcSave); i++) p[i] = 0;
  s->magic = SAVE_MAGIC; s->version = SAVE_VERSION; s->size = GC_SAVE_BYTES;
  s->rng = 0x1234567u;
  s->day = 1; s->minute = 9 * 60;
  s->rack[0] = s->rack[1] = 1 | RK_DONE;
  s->crates_srv = 2;            // two servers already wait on the dock
  for (int i = 0; i < 4; i++) s->panel[i] = PN_OK;
  s->m_day = 0;
}

static void sanitize(DcSave* s) {
  if (s->day < 1) s->day = 1;
  if (s->minute >= 1440) s->minute = 9 * 60;
  for (int i = 0; i < DC_RACKS; i++) {
    s->rack[i] &= (uint8_t)(RK_TIER | RK_DONE);
    if (!(s->rack[i] & RK_TIER)) s->rack[i] = 0;
  }
  for (int i = 0; i < DC_PANELS; i++) s->panel[i] &= 3;
  if (s->crates_srv > 8) s->crates_srv = 8;
  if (s->crates_srv + s->crates_pan > 8) s->crates_pan = (uint8_t)(8 - s->crates_srv);
  if (s->fiber > 20) s->fiber = 20;
  if (s->boost > 250) s->boost = 250;
  s->water_down = s->water_down ? 1 : 0;
  if (s->m_type >= GQ_MIS_COUNT || s->m_rooms < 20 || s->m_rooms > 30 || s->m_room >= s->m_rooms || s->m_seed == 0) {
    s->m_type = 0; s->m_room = 0; s->m_rooms = 0; s->m_seed = 0; s->m_min = 0;
  }
  if (s->deliv_done > 4) s->deliv_done = 4;
  for (unsigned i = 0; i < sizeof(s->reserved); i++) s->reserved[i] = 0;
}

static void start_scene(void) {
  uint32_t keep_serial = D.serial;
  DcSave s = D.s;
  uint8_t opts[4] = {D.card_period, D.night_quiet, D.sleep_mode, D.net};
  uint8_t wx1 = D.live_wx1, wf = D.live_flags, ap = D.clock_approx, mo = D.month, md = D.mday, wd = D.wday;
  int16_t t10 = D.live_temp10, yr = D.year;
  uint8_t* p = (uint8_t*)&D;
  for (unsigned i = 0; i < sizeof(D); i++) p[i] = 0;
  D.s = s; D.serial = keep_serial;
  D.card_period = opts[0]; D.night_quiet = opts[1]; D.sleep_mode = opts[2]; D.net = opts[3];
  D.live_wx1 = wx1; D.live_flags = wf; D.clock_approx = ap; D.month = mo; D.mday = md; D.wday = wd;
  D.live_temp10 = t10; D.year = yr;
  D.hhp = 3; D.hitem = -1; D.hemote = -1; D.wire_slot = -1;
  D.area = A_OFFICE;
  crew_update();
  build_map();
  D.hx = 6 * 16; D.hy = 7 * 16;
  D.tlops = D.tlops_shown = tlops_now();
  dc_ui_init();
  look_tick();
}

void dc_init(uint32_t seed) {
  (void)seed;
  defaults(&D.s);
  D.card_period = 10; D.night_quiet = 0; D.sleep_mode = 0;
  start_scene();
}

void dc_tick(uint32_t dt) {
  D.up_ms += dt;
  while (D.up_ms >= 1000) { D.up_ms -= 1000; D.up_s++; }
  // the minute of the day: the wall clock when it is known, otherwise a clock of its own from 09:00
  int m;
  if (g_clock_valid) m = g_hour * 60 + g_min;
  else {
    D.min_acc += dt;
    m = D.s.minute;
    while (D.min_acc >= 60000) { D.min_acc -= 60000; m = (m + 1) % 1440; }
  }
  if (m != D.s.minute) {
    int prev = D.s.minute;
    D.s.minute = (uint16_t)m;
    if (m < prev && prev - m > 600) new_day();
    on_minute();
  }
  if (D.curtain) { D.curtain = (uint16_t)(D.curtain > dt ? D.curtain - dt : 0); if (!D.curtain) gc_mark_all_dirty(); }
  truck_tick(dt);
  hero_tick(dt);
  robots_tick(dt);
  D.anim_acc = (uint16_t)(D.anim_acc + dt);
  if (D.anim_acc >= 500) {
    D.anim_acc = 0;
    D.frame++;
    D.tlops = tlops_now();
    uint32_t t = D.tlops, v = D.tlops_shown;
    if (v < t) v += (t - v + 5) / 6; else if (v > t) v -= (v - t + 5) / 6;
    D.tlops_shown = v;
    for (int i = 0; i < DC_ROBOTS; i++) if (D.rb[i].flash) D.rb[i].flash--;
    gc_mark_all_dirty();
  }
  look_tick();
  dc_ui_tick(dt);
}

void dc_status_changed(void) { g_dirty |= 1u << (GC_ROWS - 1); }

// ------------------------------------------------------------------ texts
void dc_tlops_text(char* buf, uint32_t v) {
  // v is in T. Up to 999 T, then P (thousands) and E (millions), with one decimal while it fits in 4 digits.
  static const char UNIT[3] = {'T', 'P', 'E'};
  int u = 0;
  uint32_t whole = v, frac = 0;
  while (whole >= 1000 && u < 2) { frac = (whole % 1000) / 100; whole /= 1000; u++; }
  if (whole > 999) whole = 999;
  int n = fmt_uint(buf, whole);
  if (u > 0 && whole < 100) { buf[n++] = '.'; buf[n++] = (char)('0' + frac); }
  buf[n++] = UNIT[u];
  buf[n] = 0;
}

static const char* mission_name(int type) {
  switch (type) {
    case GQ_MIS_COOLING: return DC_PS("COOLING LOOP");
    case GQ_MIS_SOLAR: return DC_PS("SOLAR BUILD");
    case GQ_MIS_STORM: return DC_PS("STORM REPAIR");
    case GQ_MIS_FIBER: return DC_PS("FIBER HUNT");
    default: return DC_PS("ROBOT UPRISING");
  }
}

void dc_status_text(char* buf, int cap) {
  const char* t;
  buf[0] = 0;
  switch (D.job) {
    case GQ_ACT_ROUNDS: t = D.ph == 2 ? DC_PS("SWAPPING A DRIVE") : DC_PS("MAKING THE ROUNDS"); break;
    case GQ_ACT_COFFEE: t = D.ph < 2 ? DC_PS("COFFEE BREAK") : (D.ph == 2 ? DC_PS("BACK TO THE DESK") : DC_PS("WATCHING DASHBOARDS")); break;
    case GQ_ACT_INSPECT: t = D.arg == A_WATER ? DC_PS("CHECKING THE PUMPS") : (D.arg == A_ROOF ? DC_PS("CHECKING THE ROOF") : DC_PS("CHECKING SWITCHES")); break;
    case GQ_ACT_CLEAN: t = (D.arg >= 0 && D.s.panel[D.arg % DC_PANELS] == PN_CRACKED) ? DC_PS("FIXING SOLAR PANEL") : DC_PS("CLEANING PANELS"); break;
    case GQ_ACT_EVENT: t = D.arg == 1 ? DC_PS("FIRE DRILL") : (D.arg == 2 ? DC_PS("FIXING COFFEE MAKER") : DC_PS("POWER BLIP! BREAKERS")); break;
    case GQ_ACT_PANEL: t = D.ph < 2 ? DC_PS("FETCHING A PANEL") : (D.ph == 2 ? DC_PS("PANEL TO THE ROOF") : DC_PS("MOUNTING SOLAR PANEL")); break;
    case GQ_ACT_INSTALL:
      t = D.ph < 2 ? DC_PS("FETCHING HARDWARE") : (D.ph == 2 ? DC_PS("CARRYING HARDWARE") : (D.ph == 3 ? DC_PS("RACKING HARDWARE") :
          (D.ph <= 6 ? DC_PS("PULLING CABLE") : (D.ph <= 8 ? DC_PS("PATCHING FIBER") : DC_PS("RACK IS ONLINE")))));
      break;
    case GQ_ACT_RECEIVE: t = DC_PS("SIGNING FOR DELIVERY"); break;
    case GQ_ACT_WATER: t = D.ph < 8 ? DC_PS("FIXING WATER PLANT") : DC_PS("COOLING IS BACK"); break;
    case GQ_ACT_FIGHT: t = DC_PS("ROGUE ROBOT!"); break;
    case GQ_ACT_REST: t = DC_PS("KNOCKED OUT. NAPPING"); break;
    case GQ_ACT_SLEEP: t = D.ph ? DC_PS("ASLEEP ON THE COT") : DC_PS("OFF TO THE COT"); break;
    case GQ_ACT_MISSION: {
      int n = rom_cat(buf, 0, mission_name(D.s.m_type ? D.s.m_type : D.arg), cap);
      if (D.area == A_MISSION && D.s.m_rooms) {
        char a[6];
        n = str_cat(buf, n, " ", cap);
        fmt_uint(a, (uint32_t)D.s.m_room + 1u); n = str_cat(buf, n, a, cap);
        n = str_cat(buf, n, "/", cap);
        fmt_uint(a, D.s.m_rooms); str_cat(buf, n, a, cap);
      }
      return;
    }
    default: t = DC_PS("ON SHIFT"); break;
  }
  rom_cat(buf, 0, t, cap);
}

void dc_goal_text(char* buf, int cap) {
  int empty = -1, low = 3;
  for (int i = 0; i < DC_RACKS; i++) {
    int t = D.s.rack[i] & RK_TIER;
    if (!t && empty < 0) empty = i;
    if (t && t < low) low = t;
  }
  int n = rom_cat(buf, 0, DC_PS("GOAL: "), cap);
  if (empty >= 0) {
    n = rom_cat(buf, n, DC_PS("FILL ROW "), cap);
    char a[4]; fmt_uint(a, (uint32_t)(empty / 10 + 1)); str_cat(buf, n, a, cap);
  } else if (low == 1) rom_cat(buf, n, DC_PS("GPU UPGRADES"), cap);
  else if (low == 2) rom_cat(buf, n, DC_PS("LIQUID COOLING"), cap);
  else rom_cat(buf, n, DC_PS("OVERCLOCKING"), cap);
}

void dc_date_text(char* buf, int cap) {
  static const char WD[] GC_ROM_ATTR = "SUNMONTUEWEDTHUFRISAT";
  static const char MO[] GC_ROM_ATTR = "JANFEBMARAPRMAYJUNJULAUGSEPOCTNOVDEC";
  int n = 0;
  char d[8];
  for (int i = 0; i < 3 && n < cap - 1; i++) buf[n++] = (char)GC_RD8(&WD[(D.wday % 7) * 3 + i]);
  if (n < cap - 1) buf[n++] = ' ';
  for (int i = 0; i < 3 && n < cap - 1; i++) buf[n++] = (char)GC_RD8(&MO[((D.month + 11) % 12) * 3 + i]);
  buf[n] = 0;
  n = str_cat(buf, n, " ", cap);
  fmt_uint(d, D.mday > 31 ? 31u : D.mday); n = str_cat(buf, n, d, cap);
  n = str_cat(buf, n, ", ", cap);
  fmt_uint(d, (uint32_t)(D.year < 0 ? 0 : D.year)); str_cat(buf, n, d, cap);
}

// ------------------------------------------------------------------ persistence
static int save_check(const uint8_t* buf, int len) {
  if (!buf || len < GC_SAVE_BYTES) return GC_SAVE_ERR_LEN;
  uint32_t magic = (uint32_t)buf[0] | ((uint32_t)buf[1] << 8) | ((uint32_t)buf[2] << 16) | ((uint32_t)buf[3] << 24);
  if (magic != SAVE_MAGIC) return GC_SAVE_ERR_MAGIC;
  if ((buf[4] | (buf[5] << 8)) != SAVE_VERSION || (buf[6] | (buf[7] << 8)) != GC_SAVE_BYTES) return GC_SAVE_ERR_VERSION;
  uint32_t crc = (uint32_t)buf[8] | ((uint32_t)buf[9] << 8) | ((uint32_t)buf[10] << 16) | ((uint32_t)buf[11] << 24);
  if (crc != crc32_calc(buf + 12, GC_SAVE_BYTES - 12)) return GC_SAVE_ERR_CRC;
  return GC_SAVE_OK;
}

extern "C" int gc_save_export(uint8_t* buf, int cap) {
  if (gc_scene_id() != GC_SCENE_VILLAGE || !buf || cap < GC_SAVE_BYTES) return 0;
  DcSave tmp = D.s;
  sanitize(&tmp);
  tmp.magic = SAVE_MAGIC; tmp.version = SAVE_VERSION; tmp.size = GC_SAVE_BYTES;
  tmp.seq = D.s.seq + 1;
  tmp.rng = gc_rng_get();
  const uint8_t* p = (const uint8_t*)&tmp;
  for (int i = 0; i < GC_SAVE_BYTES; i++) buf[i] = p[i];
  uint32_t crc = crc32_calc(buf + 12, GC_SAVE_BYTES - 12);
  buf[8] = (uint8_t)crc; buf[9] = (uint8_t)(crc >> 8); buf[10] = (uint8_t)(crc >> 16); buf[11] = (uint8_t)(crc >> 24);
  D.s.seq = tmp.seq;
  return GC_SAVE_BYTES;
}

extern "C" int gc_save_import(const uint8_t* buf, int len) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return 0;
  if (save_check(buf, len) != GC_SAVE_OK) return 0;
  DcSave tmp;
  uint8_t* d = (uint8_t*)&tmp;
  for (int i = 0; i < GC_SAVE_BYTES; i++) d[i] = buf[i];
  sanitize(&tmp);
  D.s = tmp;                 // from here on nothing can fail
  start_scene();
  gc_rng_set(tmp.rng);
  gc_mark_all_dirty();
  return 1;
}

extern "C" uint32_t gc_save_serial(void) { return D.serial; }

extern "C" void gc_reset_game(void) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return;
  defaults(&D.s);
  start_scene();
  touch();
  gc_mark_all_dirty();
}

// ------------------------------------------------------------------ environment
extern "C" void gc_set_date(int year, int month, int day, int weekday, uint32_t epoch) {
  (void)epoch;
  if (year < 0 || year > 9999) year = 0;
  if (month < 1 || month > 12) month = 1;
  if (day < 1 || day > 31) day = 1;
  if (weekday < 0 || weekday > 6) weekday = 0;
  D.year = (int16_t)year; D.month = (uint8_t)month; D.mday = (uint8_t)day; D.wday = (uint8_t)weekday;
}

extern "C" void gc_set_clock_quality(int approximate) { D.clock_approx = approximate ? 1 : 0; }

extern "C" void gc_set_net(int state) {
  if (state < GC_NET_ONLINE || state > GC_NET_AP_ONLY) state = GC_NET_OFFLINE;
  D.net = (uint8_t)state;
}

extern "C" void gc_set_option(int key, int value) {
  if (key == GC_OPT_CARD_PERIOD) D.card_period = (uint8_t)(value < 0 ? 0 : (value > 255 ? 255 : value));
  else if (key == GC_OPT_NIGHT_QUIET) D.night_quiet = value ? 1 : 0;
  else if (key == GC_OPT_SLEEP_MODE && value >= 0 && value <= 2) D.sleep_mode = (uint8_t)value;
}

extern "C" void gc_set_live_weather(int wx, int temp_c10, int flags) {
  D.live_wx1 = (wx >= 0 && wx < GQ_W_COUNT) ? (uint8_t)(wx + 1) : 0;
  D.live_temp10 = (int16_t)(temp_c10 < -999 ? -999 : (temp_c10 > 999 ? 999 : temp_c10));
  D.live_flags = (uint8_t)(flags & 7);
}

// ------------------------------------------------------------------ introspection
extern "C" int32_t gc_query(int key) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return 0;
  switch (key) {
    case GQ_Q_DAY: return D.s.day;
    case GQ_Q_MINUTE: return D.s.minute;
    case GQ_Q_HERO_AREA: return D.area;
    case GQ_Q_HERO_ACT: return D.job;
    case GQ_Q_HERO_TILE: return ((D.hy + 8) >> 4) * GC_COLS + ((D.hx + 8) >> 4);
    case GQ_Q_HERO_ABED: return dc_asleep();
    case GQ_Q_TLOPS: return (int32_t)tlops_now();
    case GQ_Q_TLOPS_SHOWN: return (int32_t)D.tlops_shown;
    case GQ_Q_RACKS: return dc_racks_done();
    case GQ_Q_PANELS: return panels_total();
    case GQ_Q_CRATES: return D.s.crates_srv + D.s.crates_pan;
    case GQ_Q_WATER_DOWN: return D.s.water_down;
    case GQ_Q_GLITCHES: { int n = 0; for (int i = 0; i < DC_ROBOTS; i++) n += D.rb[i].on && D.rb[i].glitch; return n; }
    case GQ_Q_MISSION: return D.s.m_type;
    case GQ_Q_MISSION_ROOM: return D.s.m_room;
    case GQ_Q_MISSION_ROOMS: return D.s.m_rooms;
    case GQ_Q_INSTALLS: return D.s.installs;
    case GQ_Q_FIGHTS: return D.s.fights;
    case GQ_Q_MISSIONS: return D.s.missions;
    case GQ_Q_CARD: return dc_card_on();
    case GQ_Q_LIVE_WX: return (int)D.live_wx1 - 1;
    case GQ_Q_LIVE_TEMP: return D.live_temp10;
    case GQ_Q_LIVE_FLAGS: return D.live_flags;
    case GQ_Q_SLEEP_MODE: return D.sleep_mode;
    case GQ_Q_PHASE: return D.ph;
    case GQ_Q_ABORTS: return D.aborts;
    default: return 0;
  }
}

extern "C" void gc_debug(int cmd, int32_t arg) {
  if (gc_scene_id() != GC_SCENE_VILLAGE) return;
  switch (cmd) {
    case GQ_DBG_SET_DAY: D.s.day = (uint16_t)(arg < 1 ? 1 : (arg > 65535 ? 65535 : arg)); touch(); break;
    case GQ_DBG_NEXT_DAY: new_day(); break;
    case GQ_DBG_SET_MINUTE: if (arg >= 0 && arg < 1440) D.s.minute = (uint16_t)arg; break;
    case GQ_DBG_ADD_RACKS:
      for (int n = 0; n < arg && n < 90; n++) {
        int s = next_install_slot();
        if (s < 0) { if (D.s.boost < 250) D.s.boost++; continue; }
        D.s.rack[s] = (uint8_t)(((D.s.rack[s] & RK_TIER) + 1) | RK_DONE);
      }
      crew_update(); rebuild(A_HALL); rebuild(A_WATER); touch();
      break;
    case GQ_DBG_ADD_PANELS:
      for (int n = 0; n < arg; n++) { int s = first_panel(PN_NONE, PN_NONE); if (s >= 0) D.s.panel[s] = PN_OK; }
      crew_update(); rebuild(A_ROOF); touch();
      break;
    case GQ_DBG_DELIVERY: if (!D.truck) { if (D.s.deliv_done) D.s.deliv_done--; truck_arrive(); } break;
    case GQ_DBG_WATER_DOWN: D.s.water_down = 1; rebuild(A_WATER); touch(); break;
    case GQ_DBG_GLITCH: { DcRobot* r = &D.rb[arg & 3]; if (r->on) { r->glitch = 1; r->hp = 3; } break; }
    case GQ_DBG_MISSION:
      if (D.area == A_MISSION) mission_leave();
      if (D.s.m_type == GQ_MIS_COOLING) { D.s.water_down = 0; rebuild(A_WATER); }
      if (D.job == GQ_ACT_MISSION) job_done();
      D.s.m_type = 0;
      if ((arg & 7) >= 1 && (arg & 7) < GQ_MIS_COUNT) {
        job_start(GQ_ACT_MISSION, arg & 7);
        mission_begin(arg & 7, (uint32_t)arg >> 3 ? (uint32_t)arg >> 3 : gc_rnd());
        room_enter();
        D.ph = 1;
      }
      break;
    case GQ_DBG_MISSION_ROOM:
      if (D.area == A_MISSION && D.s.m_type && arg >= 0 && arg < D.s.m_rooms) { D.s.m_room = (uint8_t)arg; D.t = 0; room_enter(); }
      break;
    case GQ_DBG_GOTO:
      if (arg >= 0 && arg < A_MISSION && D.area != A_MISSION) { job_done(); enter_area(arg, AREA_DOOR[arg][0], AREA_DOOR[arg][1]); }
      break;
    case GQ_DBG_SHOW_CARD: dc_show_card(); break;
    case GQ_DBG_FORCE_JOB:
      if ((arg & 255) > GQ_ACT_NONE && (arg & 255) < GQ_ACT_COUNT && D.area != A_MISSION && (arg & 255) != GQ_ACT_FIGHT) {
        int job = arg & 255, a = (arg >> 8) & 15;
        if (job == GQ_ACT_INSPECT && a != A_WATER && a != A_ROOF) a = A_NET;
        if (job == GQ_ACT_EVENT && (a < 1 || a > 3)) a = 1;
        job_start(job, a);
      }
      break;
    default: break;
  }
}
