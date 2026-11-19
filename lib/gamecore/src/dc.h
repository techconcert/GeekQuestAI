// GeekQuestAI: the data center scene. State shared by dc_game.cpp (rules) and dc_draw.cpp (pictures).
#pragma once
#include "gc_internal.h"

#define DC_RACKS   30          // rack frames per floor of the hall
#define DC_FLOORS  4
#define DC_ALL_RACKS (DC_RACKS * DC_FLOORS)
#define DC_PANELS  30          // panel frames per screen of the solar farm
#define DC_SCREENS 4
#define DC_ALL_PANELS (DC_PANELS * DC_SCREENS)
#define DC_TOWERS  7           // cooling towers the water plant can hold
#define DC_CLEANERS_PER_SCREEN 3
#define DC_ROBOTS  6          // 0..3 the crew, 4..5 robots of a mission room
#define DC_NODES   3
#define RK_TIER    0x03       // 0 = empty slot, 1 CPU, 2 GPU, 3 liquid cooled
#define RK_WIRED   0x04       // cable run to the network room door
#define RK_PATCHED 0x08       // patched in the network room: the rack produces
#define RK_DONE    (RK_WIRED | RK_PATCHED)

enum { A_HALL = GQ_AREA_HALL, A_NET, A_DOCK, A_WATER, A_ROOF, A_OFFICE, A_STORE, A_MISSION, A_COUNT };
enum { POSE_STAND, POSE_WORK, POSE_BED, POSE_HURT, POSE_CHEER, POSE_SIT };
enum { RB_CRATE, RB_SWEEP, RB_SPIDER, RB_DRONE, RB_OVERSEER };
enum { PN_NONE, PN_OK, PN_DIRTY, PN_CRACKED };

// The saved game: exactly GC_SAVE_BYTES. Racks and panels are packed (4 and 2 bits each); the game works on the
// unpacked g_rack / g_panel. Offsets are fixed; new fields come out of `reserved`.
struct DcSave {
  uint32_t magic;            //  0  'GQAI'
  uint16_t version;          //  4
  uint16_t size;             //  6
  uint32_t crc32;            //  8  over bytes 12..191
  uint32_t seq;              // 12
  uint32_t rng;              // 16
  uint32_t m_seed;           // 20
  uint16_t day;              // 24  1-based
  uint16_t minute;           // 26
  uint16_t installs;         // 28
  uint16_t fights;           // 30
  uint16_t missions;         // 32
  uint8_t  rack_p[60];       // 34  120 racks, 4 bits each: tier 0-3, wired, patched
  uint8_t  panel_p[30];      // 94  120 panels, 2 bits each
  uint8_t  crates_srv;       // 124
  uint8_t  crates_pan;       // 125
  uint8_t  crates_tow;       // 126 cooling tower kits on the dock
  uint8_t  boost;            // 127 overclock steps after everything is liquid cooled
  uint8_t  fiber;            // 128 fiber upgrades (missions)
  uint8_t  water_down;       // 129
  uint8_t  m_type;           // 130 GQ_MIS_*, 0 = no mission running
  uint8_t  m_room;           // 131
  uint8_t  m_rooms;          // 132
  uint8_t  m_day;            // 133 low byte of the day the last mission started
  uint8_t  deliv_done;       // 134 trucks that came today
  uint8_t  ev_done;          // 135 bit 0: today's small event happened
  uint8_t  m_min;            // 136 minutes spent in the running mission
  uint8_t  slots;            // 137 rack frames built (the hall grows 1 to 3 frames at a time, floor by floor)
  uint8_t  pslots;           // 138 panel frames built on the farm
  uint8_t  towers;           // 139 cooling towers
  uint8_t  cleaners;         // 140 panel cleaner robots
  uint8_t  reserved[51];     // 141
};
static_assert(sizeof(DcSave) == GC_SAVE_BYTES, "DcSave is the saved record");

extern uint8_t g_rack[DC_ALL_RACKS];     // unpacked: RK_TIER | RK_WIRED | RK_PATCHED
extern uint8_t g_panel[DC_ALL_PANELS];   // PN_*

struct DcRobot {
  int16_t  x, y;             // pixels (tile aligned when idle)
  uint8_t  kind, area, on, glitch, hp, dir, anim, flash;
  uint16_t acc;
  int16_t  wait;             // ms it stands still
  int16_t  flee;             // ms it runs after a hit
};

struct DcNode { uint8_t x, y, on, done; };

struct DcState {
  DcSave   s;
  uint32_t serial;
  uint8_t  area;
  // technician
  int16_t  hx, hy;
  uint8_t  hdir, hstep, hsub, hpose, hhp;
  uint8_t  floor, fscr;      // hall floor and solar farm screen he is on
  int8_t   hitem, hemote;
  uint16_t walk_acc;
  // job
  uint8_t  job, ph;
  int16_t  arg, arg2;
  uint32_t t, job_ms;
  int16_t  stun;
  uint8_t  ev_pending, idle_n;
  // truck at the dock: 0 away, 1 driving in, 2 waiting for a signature, 3 driving off
  uint8_t  truck, load_srv, load_pan, load_tow;
  int16_t  truck_x;
  uint16_t truck_acc;
  // cable being pulled
  int8_t   wire_slot;
  uint8_t  wire_stage;
  // environment
  uint8_t  net, card_period, night_quiet, sleep_mode, live_wx1, live_flags, clock_approx;
  int16_t  live_temp10;
  int16_t  year;
  uint8_t  month, mday, wday;
  // display
  uint32_t tlops, tlops_shown;
  uint16_t pw_pct, cl_pct;   // power and cooling supply as % of what the racks ask for (0..999)
  uint16_t curtain, anim_acc, card_ms;
  uint8_t  frame, blip, m_theme;
  uint32_t card_acc, min_acc, up_s, up_ms;
  uint16_t aborts;
  DcRobot  rb[DC_ROBOTS];
  DcNode   node[DC_NODES];
  uint8_t  exit_x, exit_y;
};
extern DcState D;

// ---- layout helpers (dc_game.cpp)
int dc_rack_x(int slot);       // tile
int dc_rack_y(int slot);
int dc_panel_x(int slot);
int dc_panel_y(int slot);
int dc_asleep(void);           // lying on the cot
int dc_is_night(void);         // the built-in night
int dc_wx(void);               // live weather GQ_W_* or -1
int dc_racks_done(void);
int dc_racks_total(void);       // racks in the building (not only the online ones)
int dc_resources_bad(void);     // 1 when power or cooling is short
int dc_alarm(void);            // 1 water plant down, 2 rogue robot, 0 all green
void dc_status_text(char* buf, int cap);
void dc_goal_text(char* buf, int cap);
void dc_tlops_text(char* buf, uint32_t v);   // at most 5 characters
void dc_date_text(char* buf, int cap);

// ---- dc_draw.cpp
void dc_ui_init(void);
void dc_ui_tick(uint32_t dt);
void dc_show_card(void);
int  dc_card_on(void);
