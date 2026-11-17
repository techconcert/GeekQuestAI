// GeekQuestAI: the data center scene. State shared by dc_game.cpp (rules) and dc_draw.cpp (pictures).
#pragma once
#include "gc_internal.h"

#define DC_RACKS   30
#define DC_PANELS  24
#define DC_ROBOTS  6          // 0..3 the crew, 4..5 robots of a mission room
#define DC_NODES   3
#define RK_TIER    0x03       // 0 = empty slot, 1 CPU, 2 GPU, 3 liquid cooled
#define RK_WIRED   0x40       // cable run to the network room door
#define RK_PATCHED 0x80       // patched in the network room: the rack produces
#define RK_DONE    (RK_WIRED | RK_PATCHED)

enum { A_HALL = GQ_AREA_HALL, A_NET, A_DOCK, A_WATER, A_ROOF, A_OFFICE, A_STORE, A_MISSION, A_COUNT };
enum { POSE_STAND, POSE_WORK, POSE_BED, POSE_HURT, POSE_CHEER, POSE_SIT };
enum { RB_CRATE, RB_SWEEP, RB_SPIDER, RB_DRONE, RB_OVERSEER };
enum { PN_NONE, PN_OK, PN_DIRTY, PN_CRACKED };

// The saved game: exactly GC_SAVE_BYTES. Offsets are fixed; new fields come out of `reserved`.
struct DcSave {
  uint32_t magic;            //  0  'GQAI'
  uint16_t version;          //  4
  uint16_t size;             //  6
  uint32_t crc32;            //  8  over bytes 12..191
  uint32_t seq;              // 12
  uint32_t rng;              // 16
  uint16_t day;              // 20  1-based
  uint16_t minute;           // 22
  uint8_t  rack[DC_RACKS];   // 24
  uint8_t  panel[DC_PANELS]; // 54
  uint8_t  crates_srv;       // 78
  uint8_t  crates_pan;       // 79
  uint8_t  boost;            // 80  overclock steps after the hall is full
  uint8_t  fiber;            // 81  fiber upgrades (missions)
  uint8_t  water_down;       // 82
  uint8_t  m_type;           // 83  GQ_MIS_*, 0 = no mission running
  uint8_t  m_room;           // 84
  uint8_t  m_rooms;          // 85
  uint8_t  m_day;            // 86  low byte of the day the last mission started
  uint8_t  deliv_done;       // 87  trucks that came today
  uint32_t m_seed;           // 88
  uint16_t installs;         // 92
  uint16_t fights;           // 94
  uint16_t missions;         // 96
  uint8_t  ev_done;          // 98  bit 0: today's small event happened
  uint8_t  m_min;            // 99  minutes spent in the running mission
  uint8_t  slots;            // 100 rack frames built so far (the hall grows 1 to 3 frames at a time)
  uint8_t  reserved[91];     // 101
};
static_assert(sizeof(DcSave) == GC_SAVE_BYTES, "DcSave is the saved record");

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
  int8_t   hitem, hemote;
  uint16_t walk_acc;
  // job
  uint8_t  job, ph;
  int16_t  arg, arg2;
  uint32_t t, job_ms;
  int16_t  stun;
  uint8_t  ev_pending, idle_n;
  // truck at the dock: 0 away, 1 driving in, 2 waiting for a signature, 3 driving off
  uint8_t  truck, load_srv, load_pan;
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
