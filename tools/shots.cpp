// Host tool: runs the game core and writes screenshots (PPM) for the README and for eyeballing a build.
//   g++ -std=c++17 -O1 -o shots tools/shots.cpp lib/gamecore/src/*.cpp && ./shots outdir
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include "../lib/gamecore/src/gamecore.h"
#include "../lib/gamecore/src/gq_types.h"

static uint16_t fb[240 * 240];
static int g_min = 9 * 60 + 5;
static uint32_t g_acc = 0;

static void run(uint32_t ms) {
  while (ms) {
    uint32_t dt = ms > 50 ? 50 : ms;
    ms -= dt;
    g_acc += dt;
    while (g_acc >= 60000) { g_acc -= 60000; g_min = (g_min + 1) % 1440; }
    gc_set_clock(g_min / 60, g_min % 60, 1);
    gc_tick(dt);
  }
}

static void shot(const std::string& dir, const char* name) {
  gc_mark_all_dirty();
  uint16_t strip[GC_STRIP_PIXELS];
  for (int s = 0; s < GC_ROWS; s++) { gc_render_strip(s, strip); memcpy(fb + s * GC_STRIP_PIXELS, strip, sizeof(strip)); }
  std::string p = dir + "/" + name + ".ppm";
  FILE* f = fopen(p.c_str(), "wb");
  fprintf(f, "P6\n240 240\n255\n");
  for (int i = 0; i < 240 * 240; i++) {
    uint16_t c = fb[i];
    uint8_t px[3] = {(uint8_t)(((c >> 11) & 31) * 255 / 31), (uint8_t)(((c >> 5) & 63) * 255 / 63), (uint8_t)((c & 31) * 255 / 31)};
    fwrite(px, 1, 3, f);
  }
  fclose(f);
}

int main(int argc, char** argv) {
  std::string dir = argc > 1 ? argv[1] : ".";
  gc_init(GC_SCENE_VILLAGE, 7, 0);
  gc_set_option(GC_OPT_CARD_PERIOD, 0);
  gc_set_option(GC_OPT_SLEEP_MODE, 1);
  gc_set_date(2026, 11, 16, 1, 0);
  gc_set_status(0, "IP 192.168.1.42");
  gc_set_status(3, "FW r4");
  gc_set_live_weather(GQ_W_CLEAR, 235, 0);
  gc_set_clock(9, 5, 1);
  run(1000); shot(dir, "start");     // a new game: one old server, a small number
  gc_debug(GQ_DBG_ADD_RACKS, 15);
  gc_debug(GQ_DBG_ADD_PANELS, 9);
  run(1000);
  auto wait = [](int act, int phase, int area) {
    for (int i = 0; i < 9000; i++) {
      if ((act < 0 || gc_query(GQ_Q_HERO_ACT) == act) && (phase < 0 || gc_query(GQ_Q_PHASE) == phase) &&
          (area < 0 || gc_query(GQ_Q_HERO_AREA) == area)) return;
      run(200);
    }
    printf("wait(%d,%d,%d) timed out\n", act, phase, area);
  };
  wait(GQ_ACT_INSTALL, 3, GQ_AREA_HALL); run(5000); shot(dir, "hall");
  wait(GQ_ACT_INSTALL, 5, GQ_AREA_HALL); run(1800); shot(dir, "cable");
  wait(GQ_ACT_INSTALL, 8, GQ_AREA_NET); run(4000); shot(dir, "network");
  wait(GQ_ACT_INSTALL, 3, GQ_AREA_HALL);
  gc_debug(GQ_DBG_DELIVERY, 0);
  wait(GQ_ACT_RECEIVE, 1, GQ_AREA_DOCK); run(3000); shot(dir, "dock");
  gc_debug(GQ_DBG_WATER_DOWN, 0);
  wait(GQ_ACT_WATER, 1, GQ_AREA_WATER); run(4000); shot(dir, "water");
  wait(GQ_ACT_WATER, 9, -1); run(3000); shot(dir, "water_ok");
  gc_debug(GQ_DBG_ADD_CLEANERS, 2);
  gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_INSPECT | (GQ_AREA_ROOF << 8));
  wait(GQ_ACT_INSPECT, 3, GQ_AREA_ROOF); run(3000); shot(dir, "roof");
  gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_COFFEE);
  wait(GQ_ACT_COFFEE, 1, GQ_AREA_OFFICE); run(3000); shot(dir, "office");
  gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_SHOP | (0 << 8));
  wait(GQ_ACT_SHOP, 1, GQ_AREA_STORE); run(5000); shot(dir, "store");
  wait(GQ_ACT_SHOP, 3, GQ_AREA_STORE); run(2500); shot(dir, "store_checkout");
  gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_COFFEE);
  gc_debug(GQ_DBG_GLITCH, 0);
  wait(GQ_ACT_FIGHT, -1, GQ_AREA_HALL); run(4000); shot(dir, "robot");
  for (int i = 0; i < 3000 && gc_query(GQ_Q_GLITCHES); i++) run(200);
  // more floors, more farm screens, more towers: the balance of power and cooling
  gc_debug(GQ_DBG_ADD_RACKS, 40);
  gc_debug(GQ_DBG_ADD_TOWERS, 4);
  gc_debug(GQ_DBG_ADD_PANELS, 25);
  gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_INSTALL);
  for (int i = 0; i < 9000 && gc_query(GQ_Q_FLOOR) < 1; i++) run(200);
  run(2500); shot(dir, "floor2");
  run(4000); shot(dir, "resources_a");
  run(4000); shot(dir, "resources_b");
  gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_COFFEE);
  gc_debug(GQ_DBG_FORCE_JOB, GQ_ACT_SHOP | (6 << 8));
  wait(GQ_ACT_SHOP, -1, GQ_AREA_ROOF); run(1500); shot(dir, "farm2");
  // missions
  static const char* MN[6] = {"", "mission_cooling", "mission_solar", "mission_storm", "mission_fiber", "mission_robots"};
  for (int m = 1; m <= 5; m++) {
    gc_debug(GQ_DBG_MISSION, m | (1234 << 3));
    gc_debug(GQ_DBG_MISSION_ROOM, 3 + m);
    run(9000); shot(dir, MN[m]);
  }
  gc_debug(GQ_DBG_MISSION, 0);
  // night on the cot, and the card
  gc_debug(GQ_DBG_SHOW_CARD, 0); run(1000); shot(dir, "card");
  run(8000);
  g_min = 23 * 60 + 10; gc_set_live_weather(GQ_W_CLEAR, 181, GC_WXF_NIGHT);
  gc_set_option(GC_OPT_SLEEP_MODE, 0);
  for (int i = 0; i < 9000 && !gc_query(GQ_Q_HERO_ABED); i++) run(200);
  run(3000); shot(dir, "cot");
  printf("tlops %d racks %d aborts %d\n", (int)gc_query(GQ_Q_TLOPS), (int)gc_query(GQ_Q_RACKS), (int)gc_query(GQ_Q_ABORTS));
  return 0;
}
