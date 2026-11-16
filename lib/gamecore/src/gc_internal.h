// Shared internals of the game core (not part of the public API).
#pragma once
#include "gamecore.h"
#include "gq_types.h"
#include "assets_gen.h"
#include "dc_art.h"

#define T_NONE 0xFF

enum { DIR_DOWN = 0, DIR_UP = 1, DIR_LEFT = 2, DIR_RIGHT = 3 };
// chars sheet: hero down 0..2, up 3..5, left 8..10 (right = left flipped); index by DIR_DOWN/UP/LEFT
static const uint8_t HERO_CELL_BASE[3] = {0, 3, 8};

// palette indices
enum { P_OUTLINE = 1, P_DSLATE = 2, P_GREEN = 3, P_LSLATE = 4, P_BROWN = 6, P_SAND = 7, P_DBLUE = 8, P_BLUE = 9,
       P_FOAM = 10, P_RED = 12, P_WHITE = 13, P_GREY = 14, P_YELLOW = 15 };

// A string kept in flash on the clock. Read it with rom_copy / rom_cat only.
#define DC_PS(s) (__extension__({ static const char dc_ps_[] GC_ROM_ATTR = (s); &dc_ps_[0]; }))

// ---- shared state (gamecore.cpp) ----
extern uint8_t  g_ground[GC_ROWS][GC_COLS];
extern uint8_t  g_obj[GC_ROWS][GC_COLS];
extern uint16_t g_pal[16];      // tinted palette for the world
extern uint16_t g_ui[16];       // untinted palette for HUD and text
extern uint32_t g_dirty;
extern char     g_status[4][32];
extern int      g_hour, g_min, g_clock_valid;

uint32_t gc_rnd();
uint32_t gc_rng_get(void);
void gc_rng_set(uint32_t state);
int gc_scene_id(void);
void mark_rows(int y, int h);
void str_copy(char* dst, const char* src, int cap);
int str_len(const char* s);
int rom_cat(char* dst, int n, const char* rom, int cap);   // appends a DC_PS string, returns the new length
int str_cat(char* dst, int n, const char* src, int cap);
int fmt_uint(char* dst, uint32_t v);                        // returns the length
void set_tint(int r256, int g256, int b256);                // marks every strip dirty when the tint changed

// ---- drawing into one 240x16 strip whose top is at y0 ----
void blit_tile(uint16_t* out, int x, gc_cell tile, int transparent);
void blit_sprite(uint16_t* out, int y0, int sx, int sy, gc_cell cell, int flip, const uint8_t* remap, const uint16_t* pal);
void fill_rect(uint16_t* out, int y0, int x, int y, int w, int h, uint16_t c);
void darken_rect(uint16_t* out, int y0, int x, int y, int w, int h);
void draw_text(uint16_t* out, int y0, int x, int y, const char* s, uint16_t c, int scale, int shadow);
void draw_text_n(uint16_t* out, int y0, int x, int y, const char* s, int n, uint16_t c, int scale);

// ---- the data center scene (dc_game.cpp, dc_draw.cpp) ----
void dc_init(uint32_t seed);
void dc_tick(uint32_t dt);
void dc_render(int s, uint16_t* out);
void dc_status_changed(void);
