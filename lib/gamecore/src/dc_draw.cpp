// GeekQuestAI: pictures of the data center scene. Strip 0 is the top bar (TLOPS, weather, temperature, clock),
// strips 1..13 the room, strip 14 the message band (what he is doing, the goal, IP address, date).
#include "dc.h"

static uint32_t s_ms;            // animation clock
static uint32_t s_page_ms;
static uint8_t  s_page;          // band page
static uint16_t s_part_acc;
static char     s_band[24];
static uint8_t  s_band_red;

enum { PG_STATUS, PG_GOAL, PG_IP, PG_DATE, PG_AP, PG_PASS, PG_FW, PG_COUNT };
#define PAGE_MS 4000
#define CARD_MS 7000

static const uint8_t WIRE_COL[3] = {P_BLUE, P_YELLOW, P_RED};

// ------------------------------------------------------------------ band and card state
static int page_ok(int pg) {
  switch (pg) {
    case PG_IP: return g_status[0][0] != 0;
    case PG_DATE: return g_clock_valid && D.year > 0;
    case PG_AP: return g_status[1][0] != 0;
    case PG_PASS: return g_status[2][0] != 0;
    case PG_FW: return g_status[3][0] != 0;
    default: return 1;
  }
}

static void band_update(void) {
  char t[24];
  t[0] = 0;
  int red = 0;
  switch (s_page) {
    case PG_GOAL: dc_goal_text(t, 24); break;
    case PG_IP: str_copy(t, g_status[0], 24); break;
    case PG_DATE: dc_date_text(t, 24); break;
    case PG_AP: str_copy(t, g_status[1], 24); break;
    case PG_PASS: str_copy(t, g_status[2], 24); break;
    case PG_FW: str_copy(t, g_status[3], 24); break;
    default: dc_status_text(t, 24); red = dc_alarm() != 0; break;
  }
  int same = red == s_band_red;
  for (int i = 0; same && i < 24; i++) { if (t[i] != s_band[i]) same = 0; if (!t[i]) break; }
  if (same) return;
  str_copy(s_band, t, 24);
  s_band_red = (uint8_t)red;
  g_dirty |= 1u << (GC_ROWS - 1);
}

void dc_ui_init(void) {
  s_ms = 0; s_page_ms = 0; s_page = PG_STATUS; s_part_acc = 0;
  s_band[0] = 0; s_band_red = 0;
  D.card_ms = 0; D.card_acc = 0;
  band_update();
}

void dc_show_card(void) { D.card_ms = CARD_MS; D.card_acc = 0; gc_mark_all_dirty(); }
int dc_card_on(void) { return D.card_ms > 0; }

static int particles(void) {
  int outdoor = D.area == A_ROOF || D.area == A_DOCK || (D.area == A_MISSION && D.m_theme == 1);
  int wx = dc_wx();
  if (!outdoor) return 0;
  return wx == GQ_W_RAIN || wx == GQ_W_STORM ? 1 : (wx == GQ_W_SNOW ? 2 : 0);
}

void dc_ui_tick(uint32_t dt) {
  s_ms += dt;
  s_page_ms += dt;
  if (s_page_ms >= PAGE_MS) {
    s_page_ms = 0;
    for (int i = 0; i < PG_COUNT; i++) { s_page = (uint8_t)((s_page + 1) % PG_COUNT); if (page_ok(s_page)) break; }
  }
  band_update();
  if (D.card_ms) {
    D.card_ms = (uint16_t)(D.card_ms > dt ? D.card_ms - dt : 0);
    if (!D.card_ms) gc_mark_all_dirty();
  } else if (D.card_period) {
    D.card_acc += dt;
    if (D.card_acc >= (uint32_t)D.card_period * 60000u) {
      D.card_acc = 0;
      if (!dc_asleep() && !(D.night_quiet && dc_is_night())) dc_show_card();
    }
  }
  if (particles()) {
    s_part_acc = (uint16_t)(s_part_acc + dt);
    if (s_part_acc >= 60) { s_part_acc = 0; g_dirty |= 0x3FFEu; }
  }
}

// ------------------------------------------------------------------ world
static uint16_t rgb(int r, int g, int b) { return (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)); }

static uint16_t sky_colour(void) {
  int h = D.s.minute / 60, wx = dc_wx();
  if (h >= 20 || h < 5) return rgb(14, 22, 58);
  if (h < 7 || h >= 18) return rgb(236, 150, 96);
  if (wx == GQ_W_RAIN || wx == GQ_W_STORM || wx == GQ_W_FOG || wx == GQ_W_SNOW) return g_ui[P_GREY];
  return g_ui[P_FOAM];
}

static void tiles_row(uint16_t* out, int s) {
  int f = D.frame;
  for (int tx = 0; tx < GC_COLS; tx++) {
    int g = g_ground[s][tx];
    if (g == T_SKY) fill_rect(out, s * 16, tx * 16, s * 16, 16, 16, sky_colour());
    else {
      if (g == T_WATER_A && (f & 1) && !D.s.water_down) g = T_WATER_B;
      blit_tile(out, tx * 16, GC_TILES(g), 0);
    }
    int o = g_obj[s][tx];
    if (o == T_NONE) continue;
    int blink = (f + tx * 3 + s) & 1;
    if ((o == T_RACK1_A || o == T_RACK2_A || o == T_RACK3_A || o == T_SWITCH_A) && blink) o++;
    else if (o == T_TOWER_TOP_A && (f & 1) && !D.s.water_down) o++;
    blit_tile(out, tx * 16, GC_TILES(o), 1);
  }
}

static void hall_cables(uint16_t* out, int y0) {
  for (int j = 0; j < DC_RACKS; j++) {
    int i = D.floor * DC_RACKS + j;
    int live = i == D.wire_slot;
    if (!(g_rack[i] & RK_WIRED) && !live) continue;
    int rx = dc_rack_x(i) * 16, ry = dc_rack_y(i) * 16, k = i % 3;
    int sx = rx + 3 + k * 4, ty = ry - 12 + k * 4;
    int ax = 7 * 16 + (rx < 7 * 16 ? 1 : 9) + k * 2;
    uint16_t c = g_pal[WIRE_COL[k]];
    int hx1 = ax, top = 44;
    if (live) {
      int hc = D.hx + 8;
      if (D.wire_stage == 1) {
        hx1 = sx < ax ? (hc < sx ? sx : (hc > ax ? ax : hc)) : (hc > sx ? sx : (hc < ax ? ax : hc));
        top = -1;
      } else if (D.wire_stage == 2) {
        top = D.hy + 8 < 44 ? 44 : D.hy + 8;
        if (top > ty) top = ty;
      } else continue;
    }
    fill_rect(out, y0, sx, ty, 2, ry - ty, c);
    int xa = sx < hx1 ? sx : hx1, xb = sx < hx1 ? hx1 : sx;
    fill_rect(out, y0, xa, ty, xb - xa + 2, 2, c);
    if (top >= 0) fill_rect(out, y0, ax, top, 2, ty - top + 2, c);
  }
}

static void net_wires(uint16_t* out, int y0) {
  for (int i = 0; i < DC_ALL_RACKS; i++) {
    if (!(g_rack[i] & RK_PATCHED)) continue;
    int sx = 50 + i * 139 / (DC_ALL_RACKS - 1), px = 50 + ((i * 7) % DC_ALL_RACKS) * 139 / (DC_ALL_RACKS - 1);
    int my = 49 + (i % 7) * 2;
    uint16_t c = g_pal[WIRE_COL[i % 3]];
    fill_rect(out, y0, sx, 44, 1, my - 44, c);
    fill_rect(out, y0, sx < px ? sx : px, my, (sx < px ? px - sx : sx - px) + 1, 1, c);
    fill_rect(out, y0, px, my, 1, 69 - my, c);
  }
}

static void roof_wires(uint16_t* out, int y0) {
  uint16_t c = g_pal[P_RED];
  for (int r = 0; r < 3; r++) {
    int last = -1;
    for (int k = 0; k < 10; k++) {
      int gi = D.fscr * DC_PANELS + r * 10 + k;
      if (g_panel[gi] || gi < D.s.pslots) last = k;
    }
    if (last < 0) continue;
    int y = dc_panel_y(r * 10) * 16 + 15, x0 = dc_panel_x(r * 10) * 16 + 2, vx = 13 * 16 + 4 + r * 3;
    fill_rect(out, y0, x0, y, vx - x0 + 2, 2, c);
    fill_rect(out, y0, vx, y, 2, 12 * 16 + 4 - y, c);
  }
}

// little cleaner robots patrol the free lanes between the panel rows
static void cleaner_bots(uint16_t* out, int y0) {
  int base = D.fscr * DC_CLEANERS_PER_SCREEN, n = D.s.cleaners - base;
  if (n > DC_CLEANERS_PER_SCREEN) n = DC_CLEANERS_PER_SCREEN;
  for (int k = 0; k < n; k++) {
    int lane = (5 + k * 3) * 16, ph = (D.frame * 3 + k * 37) % 160;
    int x = 24 + (ph < 80 ? ph : 160 - ph) * 2;
    int y = lane - 3;
    if (y + 16 <= y0 || y >= y0 + GC_STRIP_H) continue;
    blit_sprite(out, y0, x, y, GC_CHARS(C_CLEANER_A + ((D.frame >> 1) & 1)), 0, 0, g_pal);
  }
}

static void item(uint16_t* out, int y0, int x, int y, int cell, const uint16_t* pal) {
  if (y + 16 <= y0 || y >= y0 + GC_STRIP_H) return;
  blit_sprite(out, y0, x, y, GC_ITEMS(cell), 0, 0, pal);
}

static void sky_things(uint16_t* out, int y0) {
  int m = D.s.minute, wx = dc_wx();
  int night = m >= 20 * 60 || m < 5 * 60;
  int span = night ? (m >= 20 * 60 ? m - 20 * 60 : m + 4 * 60) : m - 5 * 60;   // 0..(540 or 900)
  int x = 4 + span * 216 / (night ? 540 : 900);
  int icon = night ? I_WX_7 : I_WX_0;
  if (wx > GQ_W_CLEAR && wx < GQ_W_COUNT) icon = I_WX_0 + wx;
  item(out, y0, x, 18, icon, g_ui);
}

static void truck(uint16_t* out, int y0) {
  if (!D.truck) return;
  for (int ty = 0; ty < 2; ty++)
    for (int tx = 0; tx < 3; tx++) {
      int y = 9 * 16 + ty * 16;
      if (y + 16 <= y0 || y >= y0 + GC_STRIP_H) continue;
      blit_sprite(out, y0, D.truck_x + tx * 16, y, GC_TILES(T_TRUCK_00 + ty * 3 + tx), 0, 0, g_pal);
    }
}

static const uint8_t REMAP_ROGUE[16] = {0, 1, 2, 12, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15};

static void robot(uint16_t* out, int y0, const DcRobot* r) {
  int y = r->y - 3, x = r->x + (r->glitch ? (int)(D.frame & 1) : 0);
  if (y + 16 > y0 && y < y0 + GC_STRIP_H) {
    int cell = C_CRATEBOT_A + r->kind * 2 + ((r->anim >> 3) & 1);
    blit_sprite(out, y0, x, y, GC_CHARS(cell), r->dir == DIR_RIGHT, r->glitch ? REMAP_ROGUE : 0, g_pal);
  }
  if (r->flash) item(out, y0, r->x, y - 2, I_SPARK_A + (r->flash & 1), g_ui);
  else if (r->glitch && (D.frame & 2)) item(out, y0, r->x, y - 15, I_ALERT, g_ui);
}

static void hero(uint16_t* out, int y0) {
  static const uint8_t cycle[4] = {0, 1, 0, 2};
  int x = D.hx, y = D.hy - 4, flip = D.hdir == DIR_RIGHT;
  int rowi = D.hdir == DIR_RIGHT ? (int)DIR_LEFT : (int)D.hdir;
  int cell = HERO_CELL_BASE[rowi] + cycle[D.hstep & 3];
  switch (D.hpose) {
    case POSE_BED: cell = 6; x = 10 * 16 + 8; y = 4 * 16 - 3; flip = 0; break;
    case POSE_SIT: cell = 7; flip = 0; break;
    case POSE_HURT: cell = 13; flip = 0; break;
    case POSE_CHEER: cell = 14; flip = 0; break;
    case POSE_WORK:
      if ((s_ms / 300) & 1) cell = D.hdir == DIR_DOWN ? 12 : (D.hdir == DIR_UP ? 15 : 11);
      else cell = HERO_CELL_BASE[rowi];
      break;
    default: break;
  }
  if (y + 16 > y0 && y < y0 + GC_STRIP_H) blit_sprite(out, y0, x, y, GC_CHARS(cell), flip, 0, g_pal);
  if (D.hpose == POSE_BED) {
    int f = (int)((s_ms / 500) % 3);
    item(out, y0, x + 6 + f * 3, y - 10 - f * 3, I_ZZZ, g_ui);
    return;
  }
  if (D.hitem >= 0) item(out, y0, x, y - 12, D.hitem, g_pal);
  else if (D.hemote >= 0) item(out, y0, x + 9, y - 13 - (int)((s_ms / 400) & 1), D.hemote, g_ui);
}

static void weather_particles(uint16_t* out, int y0) {
  int kind = particles();
  if (!kind) return;
  uint16_t c = kind == 1 ? g_ui[P_FOAM] : g_ui[P_WHITE];
  for (int i = 0; i < 30; i++) {
    int x, y;
    if (kind == 1) { x = (i * 61 + i * i * 7) % 240; y = (int)((i * 97u + s_ms / 5u) % 208u) + 16; fill_rect(out, y0, x, y, 1, 4, c); }
    else { x = (int)((i * 61u + ((s_ms / 300u + i) & 3u)) % 240u); y = (int)((i * 97u + s_ms / 40u) % 208u) + 16; fill_rect(out, y0, x, y, 2, 2, c); }
  }
}

// ------------------------------------------------------------------ top bar
static uint16_t p_ok(int a, int b) { int m = a < b ? a : b; return m >= 100 ? g_ui[P_GREEN] : (m >= 70 ? g_ui[P_YELLOW] : g_ui[P_RED]); }

static void hud(uint16_t* out) {
  const int y0 = 0;
  fill_rect(out, y0, 0, 0, GC_W, 16, g_ui[P_OUTLINE]);
  int alarm = dc_alarm();
  if ((s_ms / 4000u) & 1u) {                          // every other few seconds: the power and water balance
    for (int k = 0; k < 2; k++) {
      int p = k ? D.cl_pct : D.pw_pct, w = (p > 100 ? 100 : p) * 56 / 100, yy = k * 8;
      uint16_t c = p >= 100 ? g_ui[P_GREEN] : (p >= 70 ? g_ui[P_YELLOW] : g_ui[P_RED]);
      draw_text(out, y0, 2, yy, k ? "H2O" : "PWR", g_ui[P_GREY], 1, 0);
      fill_rect(out, y0, 24, yy + 1, 58, 6, g_ui[P_DSLATE]);
      fill_rect(out, y0, 25, yy + 2, w, 4, c);
    }
    char pb[6];
    int pn = D.pw_pct < D.cl_pct ? D.pw_pct : D.cl_pct;
    fmt_uint(pb, (uint32_t)(pn > 999 ? 999 : pn));
    pn = str_len(pb); pb[pn] = '%'; pb[pn + 1] = 0;
    draw_text(out, y0, 86, 4, pb, p_ok(D.pw_pct, D.cl_pct), 1, 0);
  } else {
    char buf[8];
    dc_tlops_text(buf, D.tlops_shown);
    draw_text(out, y0, 2, 1, buf, alarm ? g_ui[P_RED] : g_ui[P_GREEN], 2, 0);
    int x = 2 + str_len(buf) * 12 + 2;
    draw_text(out, y0, x, 0, "TLOPS", g_ui[P_GREY], 1, 0);
    uint32_t load = D.frame * 2654435761u;
    for (int i = 0; i < 5; i++) {                     // activity lights under the label
      int on = D.tlops_shown && ((load >> (i * 3)) & 7) > 1;
      fill_rect(out, y0, x + i * 6, 9, 5, 5, on ? (alarm ? g_ui[P_YELLOW] : g_ui[P_GREEN]) : g_ui[P_DSLATE]);
    }
  }
  // right side, from the right: clock, temperature with its icon
  const int CLK_X = GC_W - 54 - 2;
  int w = dc_wx();
  if (w >= 0) {
    int tv = D.live_temp10, tf = (D.live_flags & GC_WXF_FAHRENHEIT) ? 1 : 0;
    if (tf) tv = tv * 9 / 5 + 320;
    tv = (tv + (tv < 0 ? -5 : 5)) / 10;
    if (tv < -99) tv = -99;
    if (tv > 199) tv = 199;
    char tb[6];
    int tn = 0;
    if (tv < 0) { tb[tn++] = '-'; tv = -tv; }
    fmt_uint(tb + tn, (uint32_t)tv);
    tn = str_len(tb);
    const int rw = 17 + tn * 12 + 1 + 8;
    const int ix = CLK_X - 4 - rw;
    int icon = I_WX_0 + w;
    if (w == GQ_W_CLEAR && (D.live_flags & GC_WXF_NIGHT)) icon = I_WX_7;
    blit_sprite(out, y0, ix, 0, GC_ITEMS(icon), 0, 0, g_ui);
    uint16_t tc = (D.live_flags & GC_WXF_STALE) ? g_ui[P_GREY] : g_ui[P_WHITE];
    const int tx = ix + 17;
    draw_text(out, y0, tx, 1, tb, tc, 2, 0);
    const int dx = tx + tn * 12 + 1;                  // 3x3 hollow degree square
    fill_rect(out, y0, dx, 1, 3, 1, tc);
    fill_rect(out, y0, dx, 3, 3, 1, tc);
    fill_rect(out, y0, dx, 2, 1, 1, tc);
    fill_rect(out, y0, dx + 2, 2, 1, 1, tc);
    draw_text(out, y0, dx, 8, tf ? "F" : "C", tc, 1, 0);
    if (D.net == GC_NET_OFFLINE) draw_text(out, y0, ix - 12, 1, "!", g_ui[P_RED], 2, 0);
  } else if (D.net == GC_NET_OFFLINE) {
    draw_text(out, y0, CLK_X - 16, 1, "!", g_ui[P_RED], 2, 0);
  }
  char t[6] = {'-', '-', ':', '-', '-', 0};
  if (g_clock_valid) {
    t[0] = (char)('0' + g_hour / 10); t[1] = (char)('0' + g_hour % 10);
    t[3] = (char)('0' + g_min / 10); t[4] = (char)('0' + g_min % 10);
  }
  const uint16_t cc = D.clock_approx ? g_ui[P_GREY] : g_ui[P_WHITE];
  draw_text_n(out, y0, CLK_X, 1, t, 2, cc, 2);
  draw_text_n(out, y0, CLK_X + 21, 1, t + 2, 1, cc, 2);
  draw_text_n(out, y0, CLK_X + 30, 1, t + 3, 2, cc, 2);
}

static void band(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, 16, g_ui[P_OUTLINE]);
  int len = str_len(s_band);
  if (len > 20) len = 20;
  int x = (GC_W - len * 12) / 2;
  if (x < 1) x = 1;
  draw_text_n(out, y0, x, y0 + 1, s_band, len, s_band_red ? g_ui[P_RED] : g_ui[P_WHITE], 2);
}

static void centred(uint16_t* out, int y0, int y, const char* s, uint16_t c, int scale) {
  int x = (GC_W - str_len(s) * 6 * scale + scale) / 2;
  draw_text(out, y0, x < 12 ? 12 : x, y, s, c, scale, 0);
}

// Info card: the one number, big, and whether everything is green.
static void card(uint16_t* out, int y0) {
  fill_rect(out, y0, 0, y0, GC_W, 16, g_ui[P_DSLATE]);
  fill_rect(out, y0, 8, 36, 224, 168, g_ui[P_OUTLINE]);
  char t[24];
  rom_cat(t, 0, DC_PS("GEEKQUEST AI"), 24);
  centred(out, y0, 46, t, g_ui[P_FOAM], 2);
  dc_tlops_text(t, D.tlops_shown);
  int alarm = dc_alarm();
  centred(out, y0, 78, t, alarm ? g_ui[P_RED] : g_ui[P_GREEN], 6);
  rom_cat(t, 0, DC_PS("TLOPS"), 24);
  centred(out, y0, 130, t, g_ui[P_GREY], 2);
  rom_cat(t, 0, alarm == 1 ? DC_PS("WATER PLANT DOWN") : (alarm == 2 ? DC_PS("ROGUE ROBOT LOOSE") : DC_PS("ALL SYSTEMS GREEN")), 24);
  centred(out, y0, 158, t, alarm ? g_ui[P_YELLOW] : g_ui[P_WHITE], 2);
  if (g_clock_valid && D.year > 0) { dc_date_text(t, 24); centred(out, y0, 184, t, g_ui[P_GREY], 1); }
}

void dc_render(int s, uint16_t* out) {
  const int y0 = s * GC_STRIP_H;
  if (s == 0) { hud(out); return; }
  if (s == GC_ROWS - 1) { band(out, y0); return; }
  if (D.card_ms) { card(out, y0); return; }
  if (D.curtain) { for (int i = 0; i < GC_STRIP_PIXELS; i++) out[i] = 0; return; }
  tiles_row(out, s);
  if (D.area == A_HALL) hall_cables(out, y0);
  else if (D.area == A_NET) net_wires(out, y0);
  else if (D.area == A_ROOF) { roof_wires(out, y0); sky_things(out, y0); cleaner_bots(out, y0); }
  else if (D.area == A_DOCK) truck(out, y0);
  else if (D.area == A_WATER && !D.s.water_down) {
    for (int x = 1; x <= 7; x += 2)
      if (g_obj[3][x] == T_TOWER_TOP_A) item(out, y0, x * 16 + (int)(D.frame & 1) * 2, 3 * 16 - 14, I_STEAM_A + (D.frame & 1), g_ui);
  }
  for (int pass = 0; pass < 2; pass++) {
    for (int i = 0; i < DC_ROBOTS; i++) {
      const DcRobot* r = &D.rb[i];
      if (!r->on || r->area != D.area || (r->y > D.hy) != pass) continue;
      robot(out, y0, r);
    }
    if (!pass) hero(out, y0);
  }
  weather_particles(out, y0);
}
