// GeekQuestAI game core: long soak, missions, saving, sleep, texts. Built with ASan/UBSan in CI.
#include "gq_test.h"
#include "../lib/gamecore/src/dc.h"

static int q(int k) { return (int)gc_query(k); }

static void test_tlops_text() {
  struct V { uint32_t v; const char* s; };
  static const V T[] = {{0, "0T"}, {43, "43T"}, {999, "999T"}, {1000, "1.0P"}, {1249, "1.2P"}, {12400, "12.4P"},
                        {99999, "99.9P"}, {100000, "100P"}, {999999, "999P"}, {1000000, "1.0E"}, {3100000, "3.1E"},
                        {4000000000u, "999E"}};
  char b[8];
  for (unsigned i = 0; i < sizeof(T) / sizeof(T[0]); i++) {
    dc_tlops_text(b, T[i].v);
    if (std::strcmp(b, T[i].s)) { std::printf("tlops_text(%u) = %s, want %s\n", (unsigned)T[i].v, b, T[i].s); std::exit(1); }
    CHECK(std::strlen(b) <= 5);
  }
}

// Every band text fits the 20 characters of the bottom strip.
static void check_texts() {
  char t[24];
  dc_status_text(t, 24); CHECK(std::strlen(t) <= 20);
  dc_goal_text(t, 24); CHECK(std::strlen(t) <= 20);
  dc_date_text(t, 24); CHECK(std::strlen(t) <= 20);
}

static void test_soak() {
  sim_start(11, 8 * 60);
  gc_set_date(2026, 11, 16, 1, 0);
  gc_set_status(0, "IP 192.168.100.200");
  int max_empty = 0, saw1 = 0, saw3 = 0;
  int last_tlops = q(GQ_Q_TLOPS), seen_act[GQ_ACT_COUNT] = {0}, seen_area[GQ_AREA_COUNT] = {0};
  CHECK(last_tlops > 0);
  for (int d = 0; d < 45; d++) {
    gc_set_live_weather(d % 9 < 7 ? d % 9 : GC_WX_NONE, 180 + d * 3, d & 1 ? GC_WXF_FAHRENHEIT : 0);
    gc_set_date(2026, 11 + (16 + d) / 30, 1 + (15 + d) % 30, (1 + d) % 7, 0);
    for (int m = 0; m < 1440; m++) {
      sim_minutes(1);
      seen_act[q(GQ_Q_HERO_ACT)]++;
      seen_area[q(GQ_Q_HERO_AREA)]++;
      check_texts();
      { int e = q(GQ_Q_EMPTY_FRAMES); CHECK(e >= 0 && e <= 3); max_empty = e > max_empty ? e : max_empty; if (e == 3) saw3 = 1; if (e == 1) saw1 = 1; }
      int tile = q(GQ_Q_HERO_TILE);
      CHECK(tile >= GC_COLS && tile < 14 * GC_COLS);
      if (gqt::wall_min == 3 * 60) CHECK(q(GQ_Q_HERO_ABED) == 1);       // asleep on the cot at 03:00
      if (gqt::wall_min == 12 * 60) CHECK(q(GQ_Q_HERO_ABED) == 0 || q(GQ_Q_HERO_ACT) == GQ_ACT_REST);
    }
    CHECK(q(GQ_Q_DAY) == d + 2);
    CHECK(q(GQ_Q_ABORTS) == 0);
    CHECK(q(GQ_Q_CRATES) <= 8);
  }
  CHECK(saw1 && saw3 && max_empty <= 3);       // never more than 3 empty frames at a time
  std::printf("soak: racks %d panels %d tlops %d installs %d fights %d missions %d\n", q(GQ_Q_RACKS), q(GQ_Q_PANELS),
              q(GQ_Q_TLOPS), q(GQ_Q_INSTALLS), q(GQ_Q_FIGHTS), q(GQ_Q_MISSIONS));
  CHECK(q(GQ_Q_RACKS) == DC_ALL_RACKS);             // all four floors are full after six weeks
  CHECK(q(GQ_Q_FLOORS) == DC_FLOORS && q(GQ_Q_SCREENS) >= 2 && q(GQ_Q_TOWERS) >= 3 && q(GQ_Q_CLEANERS) >= 1);
  std::printf("soak: floors %d screens %d towers %d cleaners %d power %d%% cooling %d%%\n", q(GQ_Q_FLOORS), q(GQ_Q_SCREENS), q(GQ_Q_TOWERS), q(GQ_Q_CLEANERS), q(GQ_Q_POWER_PCT), q(GQ_Q_COOL_PCT));
  CHECK(q(GQ_Q_TLOPS) > last_tlops * 20);
  CHECK(q(GQ_Q_MISSIONS) >= 35);                    // about one long mission a day
  CHECK(q(GQ_Q_FIGHTS) > 10);
  CHECK(q(GQ_Q_PANELS) >= 30);
  for (int a = GQ_ACT_ROUNDS; a < GQ_ACT_COUNT; a++) if (!seen_act[a] && a != GQ_ACT_FIGHT) { std::printf("activity %d never seen\n", a); std::exit(1); }
  for (int a = 0; a < GQ_AREA_COUNT; a++) if (!seen_area[a]) { std::printf("area %d never seen\n", a); std::exit(1); }
}

// The number starts small (one old server) and only doubles once the second server is racked, wired and patched.
static void test_small_start() {
  sim_start(21, 9 * 60);
  int t0 = q(GQ_Q_TLOPS);
  CHECK(t0 > 0 && t0 < 10 && q(GQ_Q_RACKS) == 1);
  int minutes = 0;
  while (q(GQ_Q_RACKS) < 2 && minutes < 60) { sim_minutes(1); minutes++; CHECK(q(GQ_Q_RACKS) >= 2 || q(GQ_Q_TLOPS) < 10); }
  CHECK(q(GQ_Q_RACKS) == 2 && q(GQ_Q_TLOPS) > t0);
  std::printf("small start: %d T, second server online after %d min\n", t0, minutes);
}

// The hardware store: every purchase walks to the store, pays, and carries the goods to the dock (cables to the network room).
static void test_store() {
  for (int kind = 0; kind < 7; kind++) {
    sim_start((uint32_t)(300 + kind), 10 * 60);
    if (kind == 6) gc_debug(GQ_DBG_ADD_PANELS, 30);
    gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_SHOP | (kind << 8));
    int crates = q(GQ_Q_CRATES), panels = 0, in_store = 0, secs = 0, areas = 0;
    (void)panels;
    sim_seconds_cb(300, [&]() {
      if (q(GQ_Q_HERO_AREA) == GQ_AREA_STORE) in_store++;
      secs++; areas |= 1 << q(GQ_Q_HERO_AREA);
    });
    CHECK(in_store > 20);
    CHECK(secs == 300);
    if (kind == 3) CHECK(areas & (1 << GQ_AREA_NET));
    else CHECK(q(GQ_Q_CRATES) >= crates);          // the goods reached the dock (or were already racked)
    char t[24];
    dc_status_text(t, 24); CHECK(std::strlen(t) <= 20);
    CHECK(q(GQ_Q_ABORTS) == 0);
  }
  // the store is shut at night: he does not go shopping then
  sim_start(77, 22 * 60);
  sim_minutes(60);
  CHECK(q(GQ_Q_HERO_AREA) != GQ_AREA_STORE);
}

// Each mission runs to its end by itself, 20 to 30 rooms, within the time limit.
// Power and cooling are resources: more racks than supply throttles the number; supply restores it.
static void test_resources() {
  sim_start(55, 9 * 60);
  gc_debug(GQ_DBG_ADD_RACKS, 40);
  sim_minutes(1);
  int p0 = q(GQ_Q_POWER_PCT), c0 = q(GQ_Q_COOL_PCT), t0 = q(GQ_Q_TLOPS);
  CHECK(p0 < 100 || c0 < 100);
  gc_debug(GQ_DBG_ADD_PANELS, 60);
  gc_debug(GQ_DBG_ADD_TOWERS, 7);
  sim_minutes(1);
  CHECK(q(GQ_Q_POWER_PCT) > p0 && q(GQ_Q_COOL_PCT) > c0);
  CHECK(q(GQ_Q_TLOPS) >= t0);
  CHECK(q(GQ_Q_FLOORS) >= 2 && q(GQ_Q_SCREENS) >= 2);
  gc_debug(GQ_DBG_ADD_CLEANERS, 99);
  CHECK(q(GQ_Q_CLEANERS) <= DC_CLEANERS_PER_SCREEN * q(GQ_Q_SCREENS));
  sim_minutes(600);
  CHECK(q(GQ_Q_ABORTS) == 0);
  std::printf("resources: power %d%% -> %d%%, cooling %d%% -> %d%%\n", p0, q(GQ_Q_POWER_PCT), c0, q(GQ_Q_COOL_PCT));
}

static void test_missions() {
  for (int type = 1; type < GQ_MIS_COUNT; type++)
    for (int seed = 1; seed <= 6; seed++) {
      sim_start((uint32_t)(type * 100 + seed), 10 * 60);
      gc_set_option(GC_OPT_SLEEP_MODE, 1);
      gc_debug(GQ_DBG_ADD_RACKS, 20);
      gc_debug(GQ_DBG_MISSION, type | (seed * 7919) << 3);
      CHECK(q(GQ_Q_MISSION) == type);
      int rooms = q(GQ_Q_MISSION_ROOMS), minutes = 0;
      CHECK(rooms >= 20 && rooms <= 30);
      while (q(GQ_Q_MISSION) && minutes < 150) { sim_minutes(1); minutes++; check_texts(); }
      CHECK(q(GQ_Q_MISSION) == 0);
      CHECK(q(GQ_Q_MISSIONS) == 1);
      CHECK(q(GQ_Q_HERO_AREA) != GQ_AREA_MISSION);
      CHECK(minutes >= 15);
      if (type == GQ_MIS_COOLING) CHECK(q(GQ_Q_WATER_DOWN) == 0);
      std::printf("mission %d seed %d: %d rooms, %d min\n", type, seed, rooms, minutes);
    }
}

// A mission survives a power cut: the saved game resumes in the same room.
static void test_save() {
  uint8_t a[GC_SAVE_BYTES], b[GC_SAVE_BYTES];
  sim_start(3, 9 * 60);
  gc_set_option(GC_OPT_SLEEP_MODE, 1);
  gc_debug(GQ_DBG_ADD_RACKS, 7);
  gc_debug(GQ_DBG_MISSION, GQ_MIS_FIBER | (4242 << 3));
  sim_minutes(12);
  int room = q(GQ_Q_MISSION_ROOM), racks = q(GQ_Q_RACKS), day = q(GQ_Q_DAY);
  CHECK(room >= 3);
  uint32_t serial = gc_save_serial();
  CHECK(gc_save_export(a, GC_SAVE_BYTES) == GC_SAVE_BYTES);
  CHECK(gc_save_export(a, GC_SAVE_BYTES - 1) == 0);
  sim_start(99, 9 * 60);
  gc_set_option(GC_OPT_SLEEP_MODE, 1);
  CHECK(q(GQ_Q_RACKS) == 1);
  CHECK(gc_save_import(a, GC_SAVE_BYTES) == 1);
  CHECK(q(GQ_Q_RACKS) == racks && q(GQ_Q_DAY) == day);
  CHECK(q(GQ_Q_MISSION) == GQ_MIS_FIBER && q(GQ_Q_MISSION_ROOM) == room);
  (void)serial;
  int minutes = 0;
  while (q(GQ_Q_MISSION) && minutes < 150) { sim_minutes(1); minutes++; }
  CHECK(q(GQ_Q_MISSION) == 0 && q(GQ_Q_MISSIONS) == 1);
  // damaged, foreign and short records are refused and change nothing
  CHECK(gc_save_export(b, GC_SAVE_BYTES) == GC_SAVE_BYTES);
  int before = q(GQ_Q_MISSIONS);
  for (int i = 0; i < GC_SAVE_BYTES; i += 7) {
    uint8_t c[GC_SAVE_BYTES];
    std::memcpy(c, a, GC_SAVE_BYTES);
    c[i] ^= 0x10;
    CHECK(gc_save_import(c, GC_SAVE_BYTES) == 0);
  }
  uint8_t zero[GC_SAVE_BYTES] = {0};
  CHECK(gc_save_import(zero, GC_SAVE_BYTES) == 0);
  CHECK(gc_save_import(a, 100) == 0);
  CHECK(gc_save_import(nullptr, GC_SAVE_BYTES) == 0);
  CHECK(q(GQ_Q_MISSIONS) == before);
  // a record full of out-of-range values with a valid checksum is cleaned up, not trusted
  DcSave s;
  std::memcpy(&s, b, sizeof(s));
  std::memset(s.rack_p, 0xFF, sizeof(s.rack_p));
  std::memset(s.panel_p, 0xFF, sizeof(s.panel_p));
  s.crates_srv = 200; s.crates_pan = 200; s.crates_tow = 200; s.towers = 99; s.cleaners = 99; s.slots = 250; s.pslots = 250; s.m_type = 77; s.m_room = 99; s.minute = 9999; s.day = 0; s.fiber = 255;
  std::memcpy(b, &s, sizeof(s));
  extern uint32_t test_crc(const uint8_t*, int);
  uint32_t c = test_crc(b + 12, GC_SAVE_BYTES - 12);
  b[8] = (uint8_t)c; b[9] = (uint8_t)(c >> 8); b[10] = (uint8_t)(c >> 16); b[11] = (uint8_t)(c >> 24);
  CHECK(gc_save_import(b, GC_SAVE_BYTES) == 1);
  CHECK(q(GQ_Q_MISSION) == 0 && q(GQ_Q_CRATES) <= 8 && q(GQ_Q_DAY) == 1);
  sim_days(2);
  CHECK(q(GQ_Q_ABORTS) == 0);
  // the serial moves when the saved state changes
  uint32_t s0 = gc_save_serial();
  gc_debug(GQ_DBG_ADD_RACKS, 1);
  CHECK(gc_save_serial() != s0);
}

uint32_t test_crc(const uint8_t* p, int n) {
  uint32_t c = 0xFFFFFFFFu;
  for (int i = 0; i < n; i++) { c ^= p[i]; for (int k = 0; k < 8; k++) c = (c >> 1) ^ (0xEDB88320u & (0u - (c & 1u))); }
  return ~c;
}

static void test_sleep_modes() {
  sim_start(5, 14 * 60);
  gc_set_option(GC_OPT_SLEEP_MODE, 2);            // "sleep now" in the afternoon
  int m = 0;
  while (!q(GQ_Q_HERO_ABED) && m < 60) { sim_minutes(1); m++; }
  CHECK(q(GQ_Q_HERO_ABED) == 1 && q(GQ_Q_HERO_ACT) == GQ_ACT_SLEEP);
  sim_minutes(120);
  CHECK(q(GQ_Q_HERO_ABED) == 1);
  gc_set_option(GC_OPT_SLEEP_MODE, 1);            // schedule says: up
  sim_minutes(2);
  CHECK(q(GQ_Q_HERO_ABED) == 0);
  sim_minutes(12 * 60);                           // 04:00 with mode 1: still up
  CHECK(q(GQ_Q_HERO_ABED) == 0 || q(GQ_Q_HERO_ACT) == GQ_ACT_REST);
  gc_set_option(GC_OPT_SLEEP_MODE, 0);            // built-in night again
  m = 0;
  while (!q(GQ_Q_HERO_ABED) && m < 90) { sim_minutes(1); m++; }
  CHECK(q(GQ_Q_HERO_ABED) == 1);
}

// A crew robot goes rogue: he chases it, fights it and it goes back to work.
static void test_glitch() {
  for (int r = 0; r < 4; r++) {
    sim_start((uint32_t)(40 + r), 10 * 60);
    gc_set_option(GC_OPT_SLEEP_MODE, 1);
    gc_debug(GQ_DBG_ADD_RACKS, 20);
    gc_debug(GQ_DBG_ADD_PANELS, 8);
    sim_minutes(2);
    int before = q(GQ_Q_TLOPS), fought = 0, secs = 0;
    gc_debug(GQ_DBG_GLITCH, r);
    CHECK(q(GQ_Q_GLITCHES) == 1);
    CHECK(q(GQ_Q_TLOPS) < before);
    sim_seconds_cb(1200, [&]() { if (q(GQ_Q_HERO_ACT) == GQ_ACT_FIGHT) fought++; if (q(GQ_Q_GLITCHES)) secs++; });
    CHECK(fought >= 5);
    CHECK(q(GQ_Q_GLITCHES) == 0);
    std::printf("robot %d: rogue for %d s\n", r, secs);
  }
}

// Without a wall clock the game runs on its own clock and still passes days.
static void test_no_clock() {
  gc_init(GC_SCENE_VILLAGE, 9, 0);
  gc_set_clock(0, 0, 0);
  for (long i = 0; i < 3L * 1440 * 240; i++) gc_tick(250);
  CHECK(q(GQ_Q_DAY) == 4);
  CHECK(q(GQ_Q_ABORTS) == 0);
  CHECK(q(GQ_Q_INSTALLS) >= 4);
}

// The other scenes and odd inputs.
static void test_api() {
  for (int scene = 0; scene <= 2; scene++) {
    gc_init(scene, 1, scene & 1);
    gc_set_status(0, "IP 10.0.0.1"); gc_set_status(5, "x"); gc_set_status(-1, nullptr); gc_set_status(1, nullptr);
    gc_set_clock(25, 61, 1); gc_set_clock(12, 30, 1);
    gc_set_date(-5, 99, 99, 99, 0);
    gc_set_live_weather(99, 99999, 255); gc_set_live_weather(GQ_W_SNOW, -400, GC_WXF_FAHRENHEIT);
    gc_set_net(77); gc_set_option(99, 99); gc_set_option(GC_OPT_SLEEP_MODE, 9);
    gc_set_depth(-5); gc_set_depth(500); gc_set_fps(300);
    for (int c = 0; c < 20; c++) gc_debug(c, -1);
    for (int c = 0; c < 20; c++) gc_debug(c, 0x7FFFFFFF);
    for (int k = 0; k < 40; k++) gc_query(k);
    for (int i = 0; i < 2000; i++) gc_tick(i % 400);
    CHECK(frame_hash() != 0);
    uint8_t buf[GC_SAVE_BYTES];
    CHECK(gc_save_export(buf, GC_SAVE_BYTES) == (scene == GC_SCENE_VILLAGE ? GC_SAVE_BYTES : 0));
    gc_reset_game();
  }
  // every room renders in every weather, by day and by night
  sim_start(2, 9 * 60);
  gc_set_option(GC_OPT_SLEEP_MODE, 1);
  gc_debug(GQ_DBG_ADD_RACKS, 95);
  gc_debug(GQ_DBG_ADD_PANELS, 30);
  for (int wx = -1; wx < GQ_W_COUNT; wx++)
    for (int area = 0; area < GQ_AREA_MISSION; area++)
      for (int night = 0; night < 2; night++) {
        gc_set_live_weather(wx, 215, night ? GC_WXF_NIGHT : 0);
        gqt::wall_min = night ? 23 * 60 : 13 * 60; gqt::feed_clock();
        gc_debug(GQ_DBG_GOTO, area);
        for (int i = 0; i < 40; i++) { gc_tick(100); if (i % 9 == 0) frame_hash(); }
      }
  gc_debug(GQ_DBG_SHOW_CARD, 0);
  gc_tick(100);
  CHECK(q(GQ_Q_CARD) == 1);
  frame_hash();
}

int main() {
  test_tlops_text();
  test_api();
  test_sleep_modes();
  test_no_clock();
  test_glitch();
  test_small_start();
  test_store();
  test_missions();
  test_save();
  test_resources();
  test_soak();
  std::printf("test_dc: ok (%u frames rendered)\n", (unsigned)gqt::rendered_frames);
  return 0;
}
