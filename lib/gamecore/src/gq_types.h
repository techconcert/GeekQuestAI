// Shared ids of the GeekQuestAI game core: weather states, hero activities, query keys, debug commands.
#pragma once
#include "platform.h"

// Weather states (same numbers as lib/gqsafe wx_state()).
enum { GQ_W_CLEAR, GQ_W_CLOUDY, GQ_W_RAIN, GQ_W_STORM, GQ_W_FOG, GQ_W_SNOW, GQ_W_WINDY, GQ_W_COUNT };

// What the technician is doing (value of GQ_Q_HERO_ACT). GQ_ACT_SLEEP is read by the firmware.
enum { GQ_ACT_NONE, GQ_ACT_ROUNDS, GQ_ACT_COFFEE, GQ_ACT_INSPECT, GQ_ACT_CLEAN, GQ_ACT_SHOP, GQ_ACT_EVENT, GQ_ACT_PANEL, GQ_ACT_TOWER,
       GQ_ACT_INSTALL, GQ_ACT_RECEIVE, GQ_ACT_WATER, GQ_ACT_MISSION, GQ_ACT_FIGHT, GQ_ACT_REST,
       GQ_ACT_SLEEP, GQ_ACT_COUNT };

// Areas (value of GQ_Q_HERO_AREA).
enum { GQ_AREA_HALL, GQ_AREA_NET, GQ_AREA_DOCK, GQ_AREA_WATER, GQ_AREA_ROOF, GQ_AREA_OFFICE, GQ_AREA_STORE, GQ_AREA_MISSION,
       GQ_AREA_COUNT };

// Missions (value of GQ_Q_MISSION). 0 = none.
enum { GQ_MIS_NONE, GQ_MIS_COOLING, GQ_MIS_SOLAR, GQ_MIS_STORM, GQ_MIS_FIBER, GQ_MIS_ROBOTS, GQ_MIS_COUNT };

// gc_query keys
enum {
  GQ_Q_DAY,             // day of the game, 1-based
  GQ_Q_MINUTE,          // minute of the day the game runs on
  GQ_Q_HERO_AREA, GQ_Q_HERO_ACT, GQ_Q_HERO_TILE,
  GQ_Q_HERO_ABED,       // 1 while he lies on the cot
  GQ_Q_TLOPS,           // target output, in T (tera)
  GQ_Q_TLOPS_SHOWN,     // the number on the top bar
  GQ_Q_RACKS,           // racks that are racked, wired and patched
  GQ_Q_PANELS,          // solar panels installed
  GQ_Q_CRATES,          // crates waiting on the dock
  GQ_Q_WATER_DOWN, GQ_Q_GLITCHES,
  GQ_Q_MISSION, GQ_Q_MISSION_ROOM, GQ_Q_MISSION_ROOMS,
  GQ_Q_INSTALLS, GQ_Q_FIGHTS, GQ_Q_MISSIONS,
  GQ_Q_CARD,            // 1 while the info card is on screen
  GQ_Q_LIVE_WX, GQ_Q_LIVE_TEMP, GQ_Q_LIVE_FLAGS, GQ_Q_SLEEP_MODE,
  GQ_Q_PHASE, GQ_Q_ABORTS,
  GQ_Q_EMPTY_FRAMES,    // rack frames built but still empty (the hall grows 1 to 3 at a time)
  GQ_Q_POWER_PCT, GQ_Q_COOL_PCT,   // power / cooling supply as % of what the online racks ask for
  GQ_Q_FLOORS, GQ_Q_SCREENS, GQ_Q_TOWERS, GQ_Q_CLEANERS, GQ_Q_FLOOR, GQ_Q_RACKS_TOTAL
};

// gc_debug commands (tests and the browser preview)
enum {
  GQ_DBG_SET_DAY, GQ_DBG_NEXT_DAY,
  GQ_DBG_SET_MINUTE,    // only used while the wall clock is unknown
  GQ_DBG_ADD_RACKS,     // arg = n: n more racks (or upgrades), finished
  GQ_DBG_ADD_PANELS,    // arg = n
  GQ_DBG_DELIVERY,      // a truck arrives now
  GQ_DBG_WATER_DOWN,    // the water plant fails now
  GQ_DBG_GLITCH,        // arg = robot 0..3: it goes rogue now
  GQ_DBG_MISSION,       // arg = mission 1..5 | seed << 3: start it now
  GQ_DBG_GOTO,          // arg = area 0..6: put the technician there
  GQ_DBG_SHOW_CARD,
  GQ_DBG_FORCE_JOB,     // arg = GQ_ACT_* | detail << 8 (inspect: the area, event: 1 fire drill, 2 coffee maker, 3 power blip, shop: 0 server 1 rack 2 chips 3 cables 4 panel 5 tower 6 cleaner)
  GQ_DBG_MISSION_ROOM,  // arg = room: jump to that room of the running mission
  GQ_DBG_ADD_TOWERS,    // arg = n cooling towers
  GQ_DBG_ADD_CLEANERS   // arg = n panel cleaner robots
};
