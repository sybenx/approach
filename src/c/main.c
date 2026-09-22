#include <pebble.h>

/* ======================= per-platform layout ========================= */
#if defined(PBL_PLATFORM_EMERY)          // Pebble Time 2, 200 x 228
  #define HEADER_H   34
  #define DATE_H     62
  #define RULE       2
  #define PAD        8
  #define BAR_H      8
  #define ICON       16
  #define TIME_TOP   10
  #define RES_TIME   RESOURCE_ID_FONT_XB_54
  #define RES_MID    RESOURCE_ID_FONT_XB_22
  #define RES_SMALL  RESOURCE_ID_FONT_XB_15
  #define SZ_TIME    54
  #define SZ_MID     22
  #define SZ_SMALL   15
  #define RES_HEADER RESOURCE_ID_FONT_XB_17   // 15px read as tiny on this display
  #define HEADER_FALLBACK s_font_small
  #define PEEK_BAR_LABEL 1   // room for the MINUTE / :30 IN 6M line above a quick view
  #define LABEL_FONT FONT_KEY_GOTHIC_14_BOLD
#else                                     // 144 x 168 watches
  #define HEADER_H   25
  #define DATE_H     46
  #define RULE       2
  #define PAD        6
  #define BAR_H      6
  #define ICON       12
  #define TIME_TOP   7
  #define RES_TIME   RESOURCE_ID_FONT_XB_40
  #define RES_MID    RESOURCE_ID_FONT_XB_16
  #define RES_SMALL  RESOURCE_ID_FONT_XB_12
  #define SZ_TIME    40
  #define SZ_MID     16
  #define SZ_SMALL   12
  #define HEADER_FONT s_font_mid     // 12px bold digits (6 vs 8) blur together
  #define HEADER_FALLBACK s_font_small
  #define PEEK_BAR_LABEL 0   // no room: the dots carry the countdown on their own
  #define LABEL_FONT FONT_KEY_GOTHIC_14          // bold is too wide for MONTH in a 46px cell
#endif


// Labels use Pebble's hand-drawn Gothic (LABEL_FONT above): Archivo SemiBold is too thin to
// rasterise evenly this small.
#define WEATHER_EVERY 30   // minutes between weather refreshes

/* ============================ settings ============================== */
enum { MOD_NONE = 0, MOD_WEATHER, MOD_HEART, MOD_BATTERY, MOD_STEPS, MOD_DAY, MOD_DATE, MOD_MONTH,
       MOD_BT_OFF = 100 };   // not selectable; replaces the top-right slot while the phone is disconnected

// Custom colours (colour watches only). -1 means "follow the theme".
enum { C_BG, C_TIME, C_TEXT, C_LABEL, C_RULE, C_BAR, C_EMPTY, C_HEART, C_COUNT };

// persist keys
enum { P_TEMP = 1, P_COND, P_THEME = 10, P_ACCENT, P_SLOT_TL, P_SLOT_TR, P_SLOT_B1, P_SLOT_B2, P_SLOT_B3, P_PERIOD, P_VIBE,
       P_COLORS, P_BT_VIBE, P_BT_ICON, P_LEAD_ZERO, P_CLOCK,
       P_DAY_COLORS, P_DAY_ACCENT, P_AUTO_THEME, P_DAY_START, P_NIGHT_START, P_MARK_VIBE, P_LOW_BATT, P_BOTTOM_ICONS };

static struct {
  int  theme;        // 1 dark, 0 light
  int  accent;       // 0xRRGGBB
  int  slot[5];      // TL, TR, B1, B2, B3
  int  period;       // 1800 or 3600 seconds
  bool vibe;
  int  color[C_COUNT];  // 0xRRGGBB or -1
  bool bt_vibe;      // buzz when the phone disconnects
  bool bt_icon;      // show the disconnected icon
  bool lead_zero;    // 09:15 rather than 9:15
  int  clock;        // 0 follow the watch, 12 or 24
  bool auto_theme;   // switch between the day and night looks by the clock
  int  day_start, night_start;   // hours
  int  day_color[C_COUNT];       // day set, used only with auto_theme on colour watches
  int  day_accent;
  bool mark_vibe;    // double buzz at :00 / :30
  bool low_batt;     // take over the top-right slot when the battery is low
  bool bottom_icons; // symbols instead of DAY / DATE / MONTH captions in the bottom row
} s = { 1, 0xFF0000, { MOD_WEATHER, MOD_HEART, MOD_DAY, MOD_DATE, MOD_MONTH }, 1800, false,
        { -1, -1, -1, -1, -1, -1, -1, -1 }, false, true, false, 0,
        false, 7, 19, { -1, -1, -1, -1, -1, -1, -1, -1 }, 0xFF0000, false, true, false };

typedef struct { GColor bg, time, text, label, rule, bar, empty, accent, heart; } Theme;

static bool is_daytime(struct tm *t) {
  int h = t->tm_hour;
  return s.day_start <= s.night_start ? (h >= s.day_start && h < s.night_start)
                                      : (h >= s.day_start || h < s.night_start);
}

// With the automatic switch on, black-and-white watches go light by day and dark by night, and
// colour watches swap to the day colour set (which falls back to the light theme where unset).
static Theme theme(struct tm *t) {
  Theme th;
  bool day = s.auto_theme && is_daytime(t);
  bool dark = s.auto_theme ? !day : s.theme == 1;
#if defined(PBL_COLOR)
  const int *colors = day ? s.day_color : s.color;
  int accent = day ? s.day_accent : s.accent;
#endif
  GColor ink = dark ? GColorWhite : GColorBlack;
  th.bg     = dark ? GColorBlack : GColorWhite;
  th.time   = th.text = th.rule = th.bar = ink;
  th.label  = PBL_IF_COLOR_ELSE(dark ? GColorLightGray : GColorDarkGray, ink);
  th.empty  = PBL_IF_COLOR_ELSE(dark ? GColorDarkGray : GColorLightGray, th.bg);
  th.accent = PBL_IF_COLOR_ELSE(GColorFromHEX(accent), ink);
  th.heart  = th.accent;
#if defined(PBL_COLOR)
  GColor *slots[C_COUNT] = { &th.bg, &th.time, &th.text, &th.label, &th.rule, &th.bar, &th.empty, &th.heart };
  for (int i = 0; i < C_COUNT; i++) if (colors[i] >= 0) *slots[i] = GColorFromHEX(colors[i]);
#endif
  return th;
}

/* ============================== state =============================== */
static Window   *s_window;
static Layer    *s_canvas;
static GFont     s_font_time, s_font_mid, s_font_small, s_font_label;
#if defined(RES_HEADER)
static GFont     s_font_header;
#define HEADER_FONT s_font_header
#endif
static AppTimer *s_timer;
static GPath    *s_heart_tri;
static GPoint    s_heart_pts[3];

static bool s_have_weather;
static int  s_temp, s_cond;      // cond: 0 sun, 1 sun+cloud, 2 rain
static int  s_hr;                // 0 = unavailable
static int  s_steps;
static bool s_vibed;
static bool s_connected = true;

/* ============================ time maths ============================ */
// Countdown phases, finest first: the bar covers `from` down to `to` seconds left in `step`s.
// "Live" phases light the segment you're in, so the bar is full for its last step and every
// last-minute bar finishes full; the others light a segment once its step has passed.
static const struct { int from, to, step; bool live; } PHASES[] = {
  { 10, 0, 1, true }, { 60, 10, 5, true }, { 600, 0, 60, false },
};
#define N_PHASES (int)(sizeof PHASES / sizeof PHASES[0])

typedef struct { int remaining, step, from, cells, filled, next; } Phase;   // next: where the finer phase starts

// Outside every listed phase the bar spans the whole period in minutes.
static Phase current_phase(struct tm *t) {
  Phase ph = { .step = 60, .from = s.period, .cells = s.period / 60, .next = PHASES[N_PHASES - 1].from };
  ph.remaining = s.period - (t->tm_min * 60 + t->tm_sec) % s.period;
  bool live = false;
  for (int i = 0; i < N_PHASES; i++) {
    if (ph.remaining <= PHASES[i].from) {
      ph.step = PHASES[i].step; ph.from = PHASES[i].from;
      ph.cells = (PHASES[i].from - PHASES[i].to) / ph.step;
      ph.next = i ? PHASES[i - 1].from : 0;
      live = PHASES[i].live;
      break;
    }
  }
  ph.filled = (ph.from - ph.remaining) / ph.step + (live ? 1 : 0);
  return ph;
}

/* =========================== text helpers =========================== */
static GSize text_size(const char *str, GFont f) {
  return graphics_text_layout_get_content_size(str, f, GRect(0, 0, 500, 200),
                                               GTextOverflowModeWordWrap, GTextAlignmentLeft);
}

static void draw_text(GContext *ctx, const char *str, GFont f, GRect box, GTextAlignment a, GColor c) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, str, f, box, GTextOverflowModeTrailingEllipsis, a, NULL);
}

static void draw_text_vcenter(GContext *ctx, const char *str, GFont f, int x, int w,
                              int band_y, int band_h, GTextAlignment a, GColor c) {
  GSize sz = text_size(str, f);
  draw_text(ctx, str, f, GRect(x, band_y + (band_h - sz.h) / 2, w, sz.h + 2), a, c);
}

static void upper(char *p) { for (; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32; }

/* ============================== icons =============================== */
// Weather icons as pixel art, one bit per pixel (bit 0 = leftmost column), indexed by cond.
// Hand-drawn per size because shapes built from circles turn to mush at 12px.
static const uint16_t WX12[3][12] = {
  { // sun
    0x0060, 0x0462, 0x0204, 0x00F0, 0x01F8, 0x0DFB, 0x0DFB, 0x01F8, 0x00F0, 0x0204, 0x0462, 0x0060
  },
  { // cloud_sun
    0x0080, 0x0410, 0x01C0, 0x03E0, 0x0BE0, 0x0380, 0x0038, 0x00FE, 0x01FF, 0x01FF, 0x00FE, 0x0000
  },
  { // rain
    0x0000, 0x0070, 0x01FC, 0x03FE, 0x07FF, 0x07FF, 0x03FE, 0x0000, 0x0444, 0x0444, 0x0222, 0x0000
  },
};
static const uint16_t WX16[3][16] = {
  { // sun
    0x0180, 0x0180, 0x300C, 0x300C, 0x03C0, 0x07E0, 0x0FF0, 0xCFF3, 0xCFF3, 0x0FF0, 0x07E0, 0x03C0, 0x300C, 0x300C, 0x0180, 0x0180
  },
  { // cloud_sun
    0x0400, 0x4440, 0x2080, 0x0E00, 0x1F00, 0xDF60, 0x1F00, 0x2E00, 0x40F0, 0x03FC, 0x07FE, 0x0FFF, 0x0FFF, 0x0FFF, 0x07FE, 0x0000
  },
  { // rain
    0x0000, 0x01F0, 0x1FFC, 0x3FFE, 0x7FFF, 0x7FFF, 0x7FFF, 0x3FFE, 0x0000, 0x2108, 0x2108, 0x1084, 0x1084, 0x0842, 0x0000, 0x0000
  },
};

static const uint16_t BOLT12[12] = { 0x0000, 0x01C0, 0x00E0, 0x0070, 0x0038, 0x03FC, 0x01FC, 0x00E0, 0x0070, 0x0038, 0x000C, 0x0000 };   // charging
static const uint16_t CAL12[12] = { 0x0000, 0x0104, 0x07FE, 0x07FE, 0x0402, 0x04DA, 0x0402, 0x04DA, 0x0402, 0x07FE, 0x0000, 0x0000 };   // date
static const uint16_t CAL16[16] = { 0x0000, 0x0C18, 0x7FFE, 0x7FFE, 0x7FFE, 0x4002, 0x46DA, 0x46DA, 0x4002, 0x46DA, 0x46DA, 0x4002, 0x7FFE, 0x0000, 0x0000, 0x0000 };
static const uint16_t BOLT16[16] = { 0x0000, 0x1E00, 0x0F00, 0x0780, 0x03C0, 0x01E0, 0x3FF0, 0x1FF8, 0x0FF8, 0x0780, 0x03C0, 0x01E0, 0x00F0, 0x0030, 0x0008, 0x0000 };

static void draw_pixel_icon(GContext *ctx, GPoint o, const uint16_t *rows, int n, GColor ink) {
  graphics_context_set_fill_color(ctx, ink);
  for (int y = 0; y < n; y++) {
    for (int x = 0; x < n; ) {   // one rect per horizontal run
      if (!(rows[y] >> x & 1)) { x++; continue; }
      int x1 = x;
      while (x1 < n && (rows[y] >> x1 & 1)) x1++;
      graphics_fill_rect(ctx, GRect(o.x + x, o.y + y, x1 - x, 1), 0, GCornerNone);
      x = x1;
    }
  }
}

static void draw_weather_icon(GContext *ctx, GPoint o, int sz, int cond, GColor ink) {
  if (cond < 0 || cond > 2) cond = 1;
  if (sz >= 16) draw_pixel_icon(ctx, o, WX16[cond], 16, ink);
  else          draw_pixel_icon(ctx, o, WX12[cond], 12, ink);
}

static void draw_heart(GContext *ctx, GPoint o, int sz, GColor c) {
  graphics_context_set_fill_color(ctx, c);
  int r = sz * 26 / 100;
  graphics_fill_circle(ctx, GPoint(o.x + sz * 30 / 100, o.y + sz * 33 / 100), r);
  graphics_fill_circle(ctx, GPoint(o.x + sz * 70 / 100, o.y + sz * 33 / 100), r);
  gpath_move_to(s_heart_tri, o);
  gpath_draw_filled(ctx, s_heart_tri);
}

static void draw_battery(GContext *ctx, GPoint o, int sz, int pct, GColor ink) {
  int w = sz, h = sz * 55 / 100, y = o.y + (sz - h) / 2;
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(o.x, y, w - 2, h));
  graphics_fill_rect(ctx, GRect(o.x + w - 2, y + h / 3, 2, h / 3), 0, GCornerNone);
  int fill = (w - 6) * pct / 100;
  if (fill > 0) graphics_fill_rect(ctx, GRect(o.x + 2, y + 2, fill, h - 4), 0, GCornerNone);
}

static void draw_steps(GContext *ctx, GPoint o, int sz, GColor ink) {
  // two footprints: sole + toe each
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, GRect(o.x + sz * 15 / 100, o.y + sz * 35 / 100, sz * 28 / 100, sz * 50 / 100), sz / 6, GCornersAll);
  graphics_fill_circle(ctx, GPoint(o.x + sz * 29 / 100, o.y + sz * 22 / 100), sz * 10 / 100);
  graphics_fill_rect(ctx, GRect(o.x + sz * 57 / 100, o.y + sz * 15 / 100, sz * 28 / 100, sz * 50 / 100), sz / 6, GCornersAll);
  graphics_fill_circle(ctx, GPoint(o.x + sz * 71 / 100, o.y + sz * 2 / 100 + sz * 78 / 100), sz * 10 / 100);
}

// Bluetooth rune with a slash through it.
static void draw_bt_off(GContext *ctx, GPoint o, int sz, GColor ink) {
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, sz >= 16 ? 2 : 1);
  int cx = o.x + sz * 45 / 100, l = o.x + sz * 20 / 100, r = o.x + sz * 70 / 100;
  int y0 = o.y, y1 = o.y + sz * 25 / 100, y3 = o.y + sz * 75 / 100, y4 = o.y + sz - 1;
  graphics_draw_line(ctx, GPoint(cx, y0), GPoint(cx, y4));
  graphics_draw_line(ctx, GPoint(cx, y0), GPoint(r, y1));
  graphics_draw_line(ctx, GPoint(r, y1), GPoint(l, y3));
  graphics_draw_line(ctx, GPoint(l, y1), GPoint(r, y3));
  graphics_draw_line(ctx, GPoint(r, y3), GPoint(cx, y4));
  graphics_draw_line(ctx, GPoint(o.x, o.y + sz - 1), GPoint(o.x + sz - 1, o.y));
}

/* ============================= modules ============================== */
typedef struct { const char *label; char value[10]; int icon; } Mod;

static void module_info(int mod, struct tm *t, Mod *m) {
  m->icon = mod;
  m->value[0] = '\0';
  switch (mod) {
    case MOD_WEATHER:
      m->label = "TEMP";
      if (s_have_weather) snprintf(m->value, sizeof m->value, "%d°", s_temp);
      else                snprintf(m->value, sizeof m->value, "--°");
      break;
    case MOD_HEART:
      m->label = "PULSE";
      if (s_hr > 0) snprintf(m->value, sizeof m->value, "%d", s_hr);
      else          snprintf(m->value, sizeof m->value, "--");
      break;
    case MOD_BATTERY:
      m->label = "BATT";
      snprintf(m->value, sizeof m->value, "%d%%", battery_state_service_peek().charge_percent);
      break;
    case MOD_STEPS:
      m->label = "STEPS";
      if (s_steps >= 100000) snprintf(m->value, sizeof m->value, "%dK", s_steps / 1000);
      else                  snprintf(m->value, sizeof m->value, "%d", s_steps);
      break;
    case MOD_DAY:   m->label = "DAY";   strftime(m->value, sizeof m->value, "%a", t); upper(m->value); break;
    case MOD_DATE:  m->label = "DATE";  snprintf(m->value, sizeof m->value, "%d", t->tm_mday);      break;
    case MOD_MONTH: m->label = "MONTH"; strftime(m->value, sizeof m->value, "%b", t); upper(m->value); break;
    case MOD_BT_OFF: m->label = "";     snprintf(m->value, sizeof m->value, "OFF");       break;
    default:        m->label = "";      m->icon = MOD_NONE;
  }
}

static int draw_module_icon(GContext *ctx, int mod, GPoint o, Theme *th, GColor ink) {
  switch (mod) {
    case MOD_WEATHER: draw_weather_icon(ctx, o, ICON, s_have_weather ? s_cond : 1, ink);     return ICON;
    case MOD_HEART:   draw_heart(ctx, GPoint(o.x, o.y + 1), ICON - 1, th->heart);              return ICON;
    case MOD_BATTERY: {
      BatteryChargeState b = battery_state_service_peek();
      if (b.is_charging) { if (ICON >= 16) draw_pixel_icon(ctx, o, BOLT16, 16, ink); else draw_pixel_icon(ctx, o, BOLT12, 12, ink); }
      else draw_battery(ctx, o, ICON, b.charge_percent, ink);
      return ICON;
    }
    case MOD_STEPS:   draw_steps(ctx, o, ICON, ink);                                           return ICON;
    case MOD_BT_OFF:  draw_bt_off(ctx, o, ICON, th->accent);                                   return ICON;
    case MOD_DATE:    if (ICON >= 16) draw_pixel_icon(ctx, o, CAL16, 16, ink); else draw_pixel_icon(ctx, o, CAL12, 12, ink); return ICON;
    default:          return 0;   // day and month names explain themselves
  }
}

// Header cell: icon + value, vertically centred. Alerts (low battery) draw in the accent colour.
static void draw_header_cell(GContext *ctx, int mod, int x, int w, struct tm *t, Theme *th, bool alert) {
  if (mod == MOD_NONE) return;
  Mod m; module_info(mod, t, &m);
  GColor ink = alert ? th->accent : th->text;
  int iw = draw_module_icon(ctx, mod, GPoint(x + PAD, (HEADER_H - ICON) / 2), th, ink);
  int tx = x + PAD + (iw ? iw + 5 : 0);
  GFont f = HEADER_FONT;
#if defined(HEADER_FALLBACK)
  if (text_size(m.value, f).w > x + w - tx) f = HEADER_FALLBACK;   // "100%" etc. drop back rather than truncate
#endif
  draw_text_vcenter(ctx, m.value, f, tx, x + w - tx, 0, HEADER_H, GTextAlignmentLeft, ink);
}

// The date inside a little calendar (filled band, two binding tabs), centred in the given area.
static void draw_framed_date(GContext *ctx, const char *text, GFont f, int sz, GRect area, GColor ink) {
  GSize ts = text_size(text, f);
  int cap = sz * 72 / 100, band = sz >= 15 ? 4 : 3;
  int fw = ts.w + 8, fh = band + cap + 7;
  int fx = area.origin.x + (area.size.w - fw) / 2;
  int fy = area.origin.y + (area.size.h - fh + 2) / 2;   // +2 leaves room for the tabs above
  graphics_context_set_fill_color(ctx, ink);
  graphics_fill_rect(ctx, GRect(fx, fy, fw, band), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(fx + fw / 4, fy - 2, 2, 2), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(fx + fw - fw / 4 - 2, fy - 2, 2, 2), 0, GCornerNone);
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(fx, fy, fw, fh));
  // centre the digits' cap height in the space under the band (the text box ends at the
  // baseline: this charset has no descenders)
  int inner_top = fy + band, inner_h = fh - band;
  int cap_top = inner_top + (inner_h - cap) / 2;
  int box_y = cap_top - (ts.h - cap);
  draw_text(ctx, text, f, GRect(fx, box_y, fw, ts.h + 2), GTextAlignmentCenter, ink);
}

// Symbols that can stand in for a caption. Day and month names explain themselves.
static bool has_symbol(int mod) {
  return mod != MOD_NONE && mod != MOD_DAY && mod != MOD_MONTH;
}

// Bottom cell. Full height: caption (words, or a symbol with the bottom_icons setting) at the top,
// large value at the bottom. Compact (a quick view is showing, captions gone): symbols always,
// inline before the value; the value drops to the small font to make room, and the symbol only
// gives way when even that won't fit (4-digit steps). With symbols on, the date sits in a calendar.
static void draw_bottom_cell(GContext *ctx, int mod, int x, int w, int top, int H, struct tm *t, Theme *th, bool accent, bool compact) {
  if (mod == MOD_NONE) return;
  Mod m; module_info(mod, t, &m);
  GColor ink = accent ? th->accent : th->text;
  bool symbols = compact || s.bottom_icons;

  if (mod == MOD_DATE && symbols) {
    GFont f = compact ? s_font_small : s_font_mid;
    draw_framed_date(ctx, m.value, f, compact ? SZ_SMALL : SZ_MID, GRect(x, top, w, H - top), ink);
    return;
  }
  if (!compact) {
    if (symbols && has_symbol(mod)) {
      draw_module_icon(ctx, mod, GPoint(x + PAD, top + 6), th, th->label);
    } else {
      int label_h = text_size("0", s_font_label).h;
      draw_text(ctx, m.label, s_font_label, GRect(x + PAD, top + 5, w - PAD, label_h + 2), GTextAlignmentLeft, th->label);
    }
  }

  // Symbol + value as one unit: left-aligned when it fits after the padding, otherwise centred in
  // the cell; failing that the value drops to the small font, and only then does the symbol go.
  // Values too wide even alone (5-digit steps) drop to the small font too.
  GFont f = s_font_mid;
  int sz = SZ_MID;
  GSize vs = text_size(m.value, f);
  int lead = compact && has_symbol(mod) ? ICON + 2 : 0;
  if (lead && lead + vs.w > w - 4) { f = s_font_small; sz = SZ_SMALL; vs = text_size(m.value, f); }
  if (lead && lead + vs.w > w - 4) { lead = 0; f = s_font_mid; sz = SZ_MID; vs = text_size(m.value, f); }
  if (vs.w > w - 4) { f = s_font_small; sz = SZ_SMALL; vs = text_size(m.value, f); }
  int unit = lead + vs.w;
  int ux = unit + 2 <= w - PAD ? x + PAD : x + (w - unit) / 2;   // keep a gap before the divider
  int vy = H - 6 - vs.h + sz / 5;
  if (lead) {
    int cap = sz * 72 / 100;   // centre the symbol on the digits
    draw_module_icon(ctx, mod, GPoint(ux, H - 6 - cap / 2 - ICON / 2), th, ink);
  }
  draw_text(ctx, m.value, f, GRect(ux + lead, vy, vs.w + 4, vs.h + 2), GTextAlignmentLeft, ink);
}

/* ============================== drawing ============================= */
static void canvas_update(Layer *layer, GContext *ctx) {
  GRect full = layer_get_bounds(layer);
  GRect b    = layer_get_unobstructed_bounds(layer);   // shrinks when a timeline quick view is showing
  bool compact = b.size.h < full.size.h - 8;
  int W = b.size.w, H = b.size.h;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  Theme th = theme(t);
  Phase ph = current_phase(t);
  int remaining = ph.remaining, step = ph.step;
  bool show_sec = step < 60;
  char buf[16];

  /* background + rules */
  graphics_context_set_fill_color(ctx, th.bg);
  graphics_fill_rect(ctx, full, 0, GCornerNone);

  // While a quick view covers the bottom, the bottom row shrinks to values only (no captions),
  // and on the 144px watches the label line above the bar is dropped to make room.
  int date_h  = compact ? SZ_MID + 8 : DATE_H;
  int date_y  = H - date_h;
  int rule2_y = date_y - RULE;
  int half    = (W - RULE) / 2;

  graphics_context_set_fill_color(ctx, th.rule);
  graphics_fill_rect(ctx, GRect(0, HEADER_H, W, RULE), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(half, 0, RULE, HEADER_H), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(0, rule2_y, W, RULE), 0, GCornerNone);

  /* header modules */
  // A low battery takes the top-right slot unless the battery is already showing; a lost phone
  // connection takes top-right too, or top-left if the battery warning is already there.
  BatteryChargeState batt = battery_state_service_peek();
  bool low = s.low_batt && !batt.is_charging && batt.charge_percent <= 20;
  int tl = s.slot[0], tr = s.slot[1];
  if (low && tl != MOD_BATTERY && tr != MOD_BATTERY) tr = MOD_BATTERY;
  if (!s_connected && s.bt_icon) {
    if (low && tr == MOD_BATTERY) tl = MOD_BT_OFF; else tr = MOD_BT_OFF;
  }
  draw_header_cell(ctx, tl, 0, half, t, &th, low && tl == MOD_BATTERY);
  draw_header_cell(ctx, tr, half + RULE, W - half - RULE, t, &th, low && tr == MOD_BATTERY);

  /* time */
  int area_y = HEADER_H + RULE;
  bool h24 = s.clock ? s.clock == 24 : clock_is_24h_style();
  strftime(buf, sizeof buf, h24 ? "%H:%M" : "%I:%M", t);
  if (!s.lead_zero && buf[0] == '0') memmove(buf, buf + 1, strlen(buf));
  GSize ts = text_size(buf, s_font_time);
  int time_y = area_y + TIME_TOP - SZ_TIME / 5;
  draw_text(ctx, buf, s_font_time, GRect(PAD, time_y, W - 2 * PAD, ts.h + 2), GTextAlignmentLeft, th.time);

  // AM/PM tucks into the top-right corner of the time; the last-minute seconds take the bottom.
  if (!h24) {
    int ap_y = area_y + TIME_TOP + (SZ_TIME - 40) / 7;   // +2px on emery to meet the taller digits
    draw_text(ctx, t->tm_hour < 12 ? "AM" : "PM", s_font_label, GRect(PAD + ts.w + 5, ap_y, W, 20), GTextAlignmentLeft, th.label);
  }

  // Ticking seconds only for the final 10; the 5-second stretch before it redraws every 5s.
  if (step == 1) {
    snprintf(buf, sizeof buf, "%02d", t->tm_sec);
    GSize ss = text_size(buf, s_font_mid);
    int sec_y = time_y + (ts.h - ss.h) - (SZ_TIME - SZ_MID) / 5;
    draw_text(ctx, buf, s_font_mid, GRect(PAD + ts.w + 5, sec_y, W, ss.h + 2), GTextAlignmentLeft, th.accent);
  }

  /* labels + progress bar */
  int bar_y   = rule2_y - PAD - BAR_H;
  int label_h = text_size("0", s_font_label).h;
  int label_y = bar_y - 4 - label_h;
  // Every segment count (60, 30, 10, 5, 4) divides 60, so a bar whose width plus one gap is a
  // multiple of 60 splits into whole, identical pixels in every phase. Labels line up with its ends.
  int bar_w = ((W - 2 * PAD + 1) / 60) * 60 - 1;
  int bar_x = (W - bar_w) / 2;

  int target_min = ((t->tm_min * 60 + t->tm_sec + remaining) / 60) % 60;
  int shown = (remaining + step - 1) / step * step;   // whole 5s in the 5-second stretch, so it's never stale
  if (show_sec) snprintf(buf, sizeof buf, ":%02d IN %d:%02d", target_min, shown / 60, shown % 60);
  else          snprintf(buf, sizeof buf, ":%02d IN %dM",     target_min, (remaining + 59) / 60);

  if (!compact || PEEK_BAR_LABEL) {
    const char *mode = step == 60 ? "MINUTE" : step == 5 ? "5 SEC" : "SECONDS";
    draw_text(ctx, mode, s_font_label, GRect(bar_x, label_y, bar_w, label_h + 2), GTextAlignmentLeft, th.text);
    draw_text(ctx, buf, s_font_label, GRect(bar_x, label_y, bar_w, label_h + 2), GTextAlignmentRight, th.accent);
  }

  int cells  = ph.cells;
  int filled = ph.filled;
  GColor fill_c = show_sec ? th.accent : th.bar;
  int pitch  = (bar_w + 1) / cells;
  bar_x += (bar_w + 1 - pitch * cells) / 2;   // only matters if a count that doesn't divide 60 is ever added

  // Dots mean "the final ten": white minutes in the run-up, red seconds at the very end.
  if (step == 1 || (step == 60 && ph.from < s.period)) {
    int r = BAR_H / 2 + 1;
    for (int i = 0; i < cells; i++) {
      GPoint c = GPoint(bar_x + i * pitch + (pitch - 1) / 2, bar_y + BAR_H / 2);
      if (i < filled) {
        graphics_context_set_fill_color(ctx, fill_c);
        graphics_fill_circle(ctx, c, r);
#if !defined(PBL_COLOR)
        // Without red to tell them apart, lit seconds get a hole so they differ from lit minutes.
        if (step == 1) { graphics_context_set_fill_color(ctx, th.bg); graphics_fill_circle(ctx, c, 1); }
#endif
      } else {
#if defined(PBL_COLOR)
        graphics_context_set_fill_color(ctx, th.empty);
        graphics_fill_circle(ctx, c, r);
#else
        graphics_context_set_stroke_color(ctx, th.bar);
        graphics_context_set_stroke_width(ctx, 1);
        graphics_draw_circle(ctx, c, r);
#endif
      }
    }
    cells = 0;
  }

  for (int i = 0; i < cells; i++) {
    GRect cell = GRect(bar_x + i * pitch, bar_y, pitch - 1, BAR_H);
    if (i < filled) {
      graphics_context_set_fill_color(ctx, fill_c);
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
    } else {
#if defined(PBL_COLOR)
      graphics_context_set_fill_color(ctx, th.empty);
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
#else
      graphics_context_set_stroke_color(ctx, th.bar);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_rect(ctx, cell);
#endif
    }
  }

  /* bottom modules */
  int cw = (W - 2 * RULE) / 3;
  graphics_context_set_fill_color(ctx, th.rule);
  graphics_fill_rect(ctx, GRect(cw, date_y, RULE, date_h), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(2 * cw + RULE, date_y, RULE, date_h), 0, GCornerNone);
  for (int i = 0; i < 3; i++) {
    draw_bottom_cell(ctx, s.slot[2 + i], i * (cw + RULE), cw, date_y, H, t, &th, i == 1, compact);
  }
}

/* ============================ scheduling ============================ */
static void redraw(void) { layer_mark_dirty(s_canvas); }

static void schedule_timer(void);

static void check_vibe(struct tm *t) {
  // Double buzz at the mark itself; remember which mark so a timer and the minute tick can't both fire it.
  static time_t last_mark;
  int into = (t->tm_min * 60 + t->tm_sec) % s.period;
  time_t mark = time(NULL) - into;
  if (s.mark_vibe && into < 3 && mark != last_mark) { last_mark = mark; vibes_double_pulse(); }

  if (current_phase(t).remaining > 60) { s_vibed = false; return; }
  if (s.vibe && !s_vibed) { vibes_short_pulse(); s_vibed = true; }
}

static void timer_cb(void *ctx) {
  s_timer = NULL;
  time_t now = time(NULL);
  check_vibe(localtime(&now));
  redraw();
  schedule_timer();
}

// In the final minute, wake exactly on each segment or phase boundary.
// Outside it no timer runs at all; the minute tick does everything.
static void schedule_timer(void) {
  if (s_timer) return;
  time_t sec; uint16_t ms;
  time_ms(&sec, &ms);
  struct tm *t = localtime(&sec);
  Phase ph = current_phase(t);
  if (ph.step >= 60) return;
  int r = ph.remaining;
  int target = r - (r % ph.step ? r % ph.step : ph.step);   // next segment edge
  if (target < ph.next) target = ph.next;                    // or next phase start
  int wait = (r - target) * 1000 - ms + 20;
  s_timer = app_timer_register(wait, timer_cb, NULL);
}

/* ========================= weather / health ========================= */
static void request_weather(void) {
  DictionaryIterator *it;
  if (app_message_outbox_begin(&it) != APP_MSG_OK) return;
  dict_write_uint8(it, 0, 0);
  app_message_outbox_send();
}

static void refresh_health(void) {
#if defined(PBL_HEALTH)
  time_t now = time(NULL);
  if (health_service_metric_accessible(HealthMetricHeartRateBPM, now, now) & HealthServiceAccessibilityMaskAvailable) {
    s_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  } else {
    s_hr = 0;
  }
  s_steps = (int)health_service_sum_today(HealthMetricStepCount);
#endif
}

/* ============================ settings io =========================== */
// Clay sends select values as strings and everything else as ints; accept both.
// (newlib's strtol faults on Pebble because it needs the C library's reentrancy state.)
static int tuple_int(Tuple *t, int base) {
  if (!t) return 0;
  if (t->type != TUPLE_CSTRING) return (int)t->value->int32;
  const char *p = t->value->cstring;
  bool neg = *p == '-';
  if (neg) p++;
  int v = 0;
  for (; *p; p++) {
    int d = (*p >= '0' && *p <= '9') ? *p - '0'
          : (*p >= 'a' && *p <= 'f') ? *p - 'a' + 10
          : (*p >= 'A' && *p <= 'F') ? *p - 'A' + 10 : 99;
    if (d >= base) break;
    v = v * base + d;
  }
  return neg ? -v : v;
}

static void inbox_cb(DictionaryIterator *it, void *ctx) {
  Tuple *tp;
  bool settings_changed = false;

  if ((tp = dict_find(it, MESSAGE_KEY_TEMP))) { s_temp = tuple_int(tp, 10); s_have_weather = true; persist_write_int(P_TEMP, s_temp); }
  if ((tp = dict_find(it, MESSAGE_KEY_COND))) { s_cond = tuple_int(tp, 10); persist_write_int(P_COND, s_cond); }

  if ((tp = dict_find(it, MESSAGE_KEY_THEME)))   { s.theme   = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_ACCENT)))  { s.accent  = tuple_int(tp, 16); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_SLOT_TL))) { s.slot[0] = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_SLOT_TR))) { s.slot[1] = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_SLOT_B1))) { s.slot[2] = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_SLOT_B2))) { s.slot[3] = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_SLOT_B3))) { s.slot[4] = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_PERIOD)))  { s.period  = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_VIBE)))    { s.vibe    = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_BT_VIBE))) { s.bt_vibe = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_BT_ICON))) { s.bt_icon = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_LEADING_ZERO))) { s.lead_zero = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_CLOCK))) { s.clock = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_AUTO_THEME)))  { s.auto_theme  = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_DAY_START)))   { s.day_start   = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_NIGHT_START))) { s.night_start = tuple_int(tp, 10); settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_MARK_VIBE)))   { s.mark_vibe   = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_LOW_BATT)))    { s.low_batt    = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_BOTTOM_ICONS))) { s.bottom_icons = tuple_int(tp, 10) != 0; settings_changed = true; }
  if ((tp = dict_find(it, MESSAGE_KEY_DAY_ACCENT)))  { s.day_accent  = tuple_int(tp, 16); settings_changed = true; }
  const uint32_t day_keys[C_COUNT] = { MESSAGE_KEY_DAY_COLOR_BG, MESSAGE_KEY_DAY_COLOR_TIME, MESSAGE_KEY_DAY_COLOR_TEXT,
    MESSAGE_KEY_DAY_COLOR_LABEL, MESSAGE_KEY_DAY_COLOR_RULE, MESSAGE_KEY_DAY_COLOR_BAR, MESSAGE_KEY_DAY_COLOR_EMPTY, MESSAGE_KEY_DAY_COLOR_HEART };
  for (int i = 0; i < C_COUNT; i++) {
    if ((tp = dict_find(it, day_keys[i]))) { s.day_color[i] = tuple_int(tp, 16); settings_changed = true; }
  }
  const uint32_t color_keys[C_COUNT] = { MESSAGE_KEY_COLOR_BG, MESSAGE_KEY_COLOR_TIME, MESSAGE_KEY_COLOR_TEXT,
    MESSAGE_KEY_COLOR_LABEL, MESSAGE_KEY_COLOR_RULE, MESSAGE_KEY_COLOR_BAR, MESSAGE_KEY_COLOR_EMPTY, MESSAGE_KEY_COLOR_HEART };
  for (int i = 0; i < C_COUNT; i++) {
    if ((tp = dict_find(it, color_keys[i]))) { s.color[i] = tuple_int(tp, 16); settings_changed = true; }
  }

  if (settings_changed) {
    if (s.period != 3600) s.period = 1800;
    persist_write_int(P_THEME, s.theme);
    persist_write_int(P_ACCENT, s.accent);
    for (int i = 0; i < 5; i++) persist_write_int(P_SLOT_TL + i, s.slot[i]);
    persist_write_int(P_PERIOD, s.period);
    persist_write_bool(P_VIBE, s.vibe);
    persist_write_data(P_COLORS, s.color, sizeof s.color);
    persist_write_bool(P_BT_VIBE, s.bt_vibe);
    persist_write_bool(P_BT_ICON, s.bt_icon);
    persist_write_bool(P_LEAD_ZERO, s.lead_zero);
    persist_write_int(P_CLOCK, s.clock);
    persist_write_bool(P_AUTO_THEME, s.auto_theme);
    persist_write_int(P_DAY_START, s.day_start);
    persist_write_int(P_NIGHT_START, s.night_start);
    persist_write_data(P_DAY_COLORS, s.day_color, sizeof s.day_color);
    persist_write_int(P_DAY_ACCENT, s.day_accent);
    persist_write_bool(P_MARK_VIBE, s.mark_vibe);
    persist_write_bool(P_LOW_BATT, s.low_batt);
    persist_write_bool(P_BOTTOM_ICONS, s.bottom_icons);
    if (s_timer) { app_timer_cancel(s_timer); s_timer = NULL; }
    schedule_timer();
  }
  redraw();
}

static void load_settings(void) {
  if (persist_exists(P_TEMP)) { s_temp = persist_read_int(P_TEMP); s_cond = persist_read_int(P_COND); s_have_weather = true; }
  if (persist_exists(P_THEME))  s.theme  = persist_read_int(P_THEME);
  if (persist_exists(P_ACCENT)) s.accent = persist_read_int(P_ACCENT);
  for (int i = 0; i < 5; i++) if (persist_exists(P_SLOT_TL + i)) s.slot[i] = persist_read_int(P_SLOT_TL + i);
  if (persist_exists(P_PERIOD)) s.period = persist_read_int(P_PERIOD);
  if (persist_exists(P_VIBE))   s.vibe   = persist_read_bool(P_VIBE);
  if (persist_exists(P_COLORS)) persist_read_data(P_COLORS, s.color, sizeof s.color);
  if (persist_exists(P_BT_VIBE))   s.bt_vibe   = persist_read_bool(P_BT_VIBE);
  if (persist_exists(P_BT_ICON))   s.bt_icon   = persist_read_bool(P_BT_ICON);
  if (persist_exists(P_LEAD_ZERO)) s.lead_zero = persist_read_bool(P_LEAD_ZERO);
  if (persist_exists(P_CLOCK))     s.clock     = persist_read_int(P_CLOCK);
  if (persist_exists(P_AUTO_THEME))  s.auto_theme  = persist_read_bool(P_AUTO_THEME);
  if (persist_exists(P_DAY_START))   s.day_start   = persist_read_int(P_DAY_START);
  if (persist_exists(P_NIGHT_START)) s.night_start = persist_read_int(P_NIGHT_START);
  if (persist_exists(P_DAY_COLORS))  persist_read_data(P_DAY_COLORS, s.day_color, sizeof s.day_color);
  if (persist_exists(P_DAY_ACCENT))  s.day_accent  = persist_read_int(P_DAY_ACCENT);
  if (persist_exists(P_MARK_VIBE))   s.mark_vibe   = persist_read_bool(P_MARK_VIBE);
  if (persist_exists(P_LOW_BATT))    s.low_batt    = persist_read_bool(P_LOW_BATT);
  if (persist_exists(P_BOTTOM_ICONS)) s.bottom_icons = persist_read_bool(P_BOTTOM_ICONS);
  if (s.period != 3600) s.period = 1800;
}

/* ============================== events ============================== */
static void minute_tick(struct tm *t, TimeUnits units) {
  refresh_health();
  if (t->tm_min % WEATHER_EVERY == 0) request_weather();
  check_vibe(t);
  redraw();
  schedule_timer();
}

static void connection_change(bool connected) {
  if (connected == s_connected) return;
  s_connected = connected;
  if (!connected && s.bt_vibe) {
    static const uint32_t pattern[] = { 200, 100, 100, 100, 500 };   // same buzz as TimeStyle
    vibes_enqueue_custom_pattern((VibePattern){ .durations = pattern, .num_segments = ARRAY_LENGTH(pattern) });
  }
  if (connected) request_weather();
  redraw();
}

static void battery_change(BatteryChargeState state) { redraw(); }

static void unobstructed_change(AnimationProgress progress, void *ctx) { redraw(); }

/* ============================== window ============================== */
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  s_canvas = layer_create(layer_get_bounds(root));
  layer_set_update_proc(s_canvas, canvas_update);
  layer_add_child(root, s_canvas);
}

static void window_unload(Window *window) {
  layer_destroy(s_canvas);
}

static void init(void) {
  load_settings();

  s_font_time  = fonts_load_custom_font(resource_get_handle(RES_TIME));
  s_font_mid   = fonts_load_custom_font(resource_get_handle(RES_MID));
  s_font_small = fonts_load_custom_font(resource_get_handle(RES_SMALL));
  s_font_label = fonts_get_system_font(LABEL_FONT);
#if defined(RES_HEADER)
  s_font_header = fonts_load_custom_font(resource_get_handle(RES_HEADER));
#endif

  int hs = ICON - 1;
  s_heart_pts[0] = GPoint(hs * 6 / 100,  hs * 42 / 100);
  s_heart_pts[1] = GPoint(hs * 94 / 100, hs * 42 / 100);
  s_heart_pts[2] = GPoint(hs / 2,        hs * 96 / 100);
  static GPathInfo heart_info;
  heart_info.num_points = 3;
  heart_info.points = s_heart_pts;
  s_heart_tri = gpath_create(&heart_info);

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_cb);
  app_message_open(1024, 64);   // a full settings save is ~40 keys

  refresh_health();
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers){ .change = unobstructed_change }, NULL);
  s_connected = connection_service_peek_pebble_app_connection();
  connection_service_subscribe((ConnectionHandlers){ .pebble_app_connection_handler = connection_change });
  battery_state_service_subscribe(battery_change);
  schedule_timer();
}

static void deinit(void) {
  if (s_timer) app_timer_cancel(s_timer);
  tick_timer_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
  connection_service_unsubscribe();
  battery_state_service_unsubscribe();
  window_destroy(s_window);
  gpath_destroy(s_heart_tri);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_mid);
  fonts_unload_custom_font(s_font_small);
#if defined(RES_HEADER)
  fonts_unload_custom_font(s_font_header);
#endif
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
