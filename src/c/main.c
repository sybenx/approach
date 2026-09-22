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
  #define RES_LABEL  RESOURCE_ID_FONT_SB_11
  #define SZ_TIME    54
  #define SZ_MID     22
  #define HEADER_FONT s_font_small
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
  #define RES_LABEL  RESOURCE_ID_FONT_SB_10
  #define SZ_TIME    40
  #define SZ_MID     16
  #define HEADER_FONT s_font_mid     // 12px bold digits (6 vs 8) blur together
#endif

#define WEATHER_EVERY 30   // minutes between weather refreshes

/* ============================ settings ============================== */
enum { MOD_NONE = 0, MOD_WEATHER, MOD_HEART, MOD_BATTERY, MOD_STEPS, MOD_DAY, MOD_DATE, MOD_MONTH };

// persist keys
enum { P_TEMP = 1, P_COND, P_THEME = 10, P_ACCENT, P_SLOT_TL, P_SLOT_TR, P_SLOT_B1, P_SLOT_B2, P_SLOT_B3, P_PERIOD, P_VIBE };

static struct {
  int  theme;        // 1 dark, 0 light
  int  accent;       // 0xRRGGBB
  int  slot[5];      // TL, TR, B1, B2, B3
  int  period;       // 1800 or 3600 seconds
  bool vibe;
} s = { 1, 0xFF0000, { MOD_WEATHER, MOD_HEART, MOD_DAY, MOD_DATE, MOD_MONTH }, 1800, false };

typedef struct { GColor bg, ink, muted, empty, accent; } Theme;

static Theme theme(void) {
  Theme th;
  bool dark = s.theme == 1;
  th.bg     = dark ? GColorBlack : GColorWhite;
  th.ink    = dark ? GColorWhite : GColorBlack;
  th.muted  = PBL_IF_COLOR_ELSE(dark ? GColorLightGray : GColorDarkGray, th.ink);
  th.empty  = PBL_IF_COLOR_ELSE(dark ? GColorDarkGray : GColorLightGray, th.bg);
  th.accent = PBL_IF_COLOR_ELSE(GColorFromHEX(s.accent), th.ink);
  return th;
}

/* ============================== state =============================== */
static Window   *s_window;
static Layer    *s_canvas;
static GFont     s_font_time, s_font_mid, s_font_small, s_font_label;
static AppTimer *s_timer;
static GPath    *s_heart_tri;
static GPoint    s_heart_pts[3];

static bool s_have_weather;
static int  s_temp, s_cond;      // cond: 0 sun, 1 sun+cloud, 2 rain
static int  s_hr;                // 0 = unavailable
static int  s_steps;
static bool s_vibed;

/* ============================ time maths ============================ */
static int current_step(struct tm *t, int *remaining_out) {
  int into = (t->tm_min * 60 + t->tm_sec) % s.period;
  int remaining = s.period - into;
  if (remaining_out) *remaining_out = remaining;
  if (remaining <= 60)  return 1;
  if (remaining <= 120) return 5;
  if (remaining <= 180) return 15;
  if (remaining <= 240) return 30;
  return 60;
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
static void draw_cloud(GContext *ctx, int x, int bottom, int sz) {
  graphics_fill_circle(ctx, GPoint(x + sz * 6 / 10, bottom - sz * 42 / 100), sz * 28 / 100);
  graphics_fill_circle(ctx, GPoint(x + sz * 32 / 100, bottom - sz * 30 / 100), sz * 20 / 100);
  graphics_fill_rect(ctx, GRect(x + sz * 12 / 100, bottom - sz * 30 / 100, sz * 78 / 100, sz * 30 / 100), 2, GCornersAll);
}

static void draw_sun(GContext *ctx, GPoint c, int r, int ray_from, int ray_to, int rays) {
  graphics_fill_circle(ctx, c, r);
  for (int i = 0; i < rays; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / rays;
    int32_t sx = sin_lookup(a), sy = -cos_lookup(a);
    graphics_draw_line(ctx,
      GPoint(c.x + sx * ray_from / TRIG_MAX_RATIO, c.y + sy * ray_from / TRIG_MAX_RATIO),
      GPoint(c.x + sx * ray_to   / TRIG_MAX_RATIO, c.y + sy * ray_to   / TRIG_MAX_RATIO));
  }
}

static void draw_weather_icon(GContext *ctx, GPoint o, int sz, int cond, GColor ink) {
  graphics_context_set_fill_color(ctx, ink);
  graphics_context_set_stroke_color(ctx, ink);
  graphics_context_set_stroke_width(ctx, sz >= 16 ? 2 : 1);
  if (cond == 0) {
    draw_sun(ctx, GPoint(o.x + sz / 2, o.y + sz / 2), sz * 3 / 16, sz * 5 / 16, sz * 8 / 16, 8);
  } else if (cond == 1) {
    draw_sun(ctx, GPoint(o.x + sz * 5 / 16, o.y + sz * 5 / 16), sz * 2 / 16, sz * 4 / 16, sz * 6 / 16, 8);
    draw_cloud(ctx, o.x, o.y + sz, sz);
  } else {
    draw_cloud(ctx, o.x, o.y + sz * 68 / 100, sz);
    for (int i = 0; i < 3; i++) {
      int x = o.x + sz * (30 + 20 * i) / 100;
      graphics_draw_line(ctx, GPoint(x, o.y + sz * 80 / 100), GPoint(x, o.y + sz));
    }
  }
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
      if (s_steps >= 10000) snprintf(m->value, sizeof m->value, "%dK", s_steps / 1000);
      else                  snprintf(m->value, sizeof m->value, "%d", s_steps);
      break;
    case MOD_DAY:   m->label = "DAY";   strftime(m->value, sizeof m->value, "%a", t); upper(m->value); break;
    case MOD_DATE:  m->label = "DATE";  snprintf(m->value, sizeof m->value, "%d", t->tm_mday);      break;
    case MOD_MONTH: m->label = "MONTH"; strftime(m->value, sizeof m->value, "%b", t); upper(m->value); break;
    default:        m->label = "";      m->icon = MOD_NONE;
  }
}

static int draw_module_icon(GContext *ctx, int mod, GPoint o, Theme *th) {
  switch (mod) {
    case MOD_WEATHER: draw_weather_icon(ctx, o, ICON, s_have_weather ? s_cond : 1, th->ink); return ICON;
    case MOD_HEART:   draw_heart(ctx, GPoint(o.x, o.y + 1), ICON - 1, th->accent);            return ICON;
    case MOD_BATTERY: draw_battery(ctx, o, ICON, battery_state_service_peek().charge_percent, th->ink); return ICON;
    case MOD_STEPS:   draw_steps(ctx, o, ICON, th->ink);                                      return ICON;
    default:          return 0;
  }
}

// Header cell: icon + value, vertically centred.
static void draw_header_cell(GContext *ctx, int mod, int x, int w, struct tm *t, Theme *th) {
  if (mod == MOD_NONE) return;
  Mod m; module_info(mod, t, &m);
  int iw = draw_module_icon(ctx, mod, GPoint(x + PAD, (HEADER_H - ICON) / 2), th);
  int tx = x + PAD + (iw ? iw + 5 : 0);
  draw_text_vcenter(ctx, m.value, HEADER_FONT, tx, x + w - tx, 0, HEADER_H, GTextAlignmentLeft, th->ink);
}

// Bottom cell: small label at top, large value at the bottom.
static void draw_bottom_cell(GContext *ctx, int mod, int x, int w, int top, int H, struct tm *t, Theme *th, bool accent) {
  if (mod == MOD_NONE) return;
  Mod m; module_info(mod, t, &m);
  int label_h = text_size("0", s_font_label).h;
  draw_text(ctx, m.label, s_font_label, GRect(x + PAD, top + 5, w - PAD, label_h + 2), GTextAlignmentLeft, th->muted);
  GSize vs = text_size(m.value, s_font_mid);
  // values too wide to sit after the left padding (e.g. MON on 144px) are centred in the cell instead
  bool fits = vs.w <= w - PAD;
  GRect box = fits ? GRect(x + PAD, 0, w - PAD, 0) : GRect(x, 0, w, 0);
  box.origin.y = H - 6 - vs.h + SZ_MID / 5;
  box.size.h = vs.h + 2;
  draw_text(ctx, m.value, s_font_mid, box, fits ? GTextAlignmentLeft : GTextAlignmentCenter,
            accent ? th->accent : th->ink);
}

/* ============================== drawing ============================= */
static void canvas_update(Layer *layer, GContext *ctx) {
  GRect full = layer_get_bounds(layer);
  GRect b    = layer_get_unobstructed_bounds(layer);   // shrinks when a timeline quick view is showing
  bool compact = b.size.h < full.size.h - 8;
  int W = b.size.w, H = b.size.h;
  Theme th = theme();

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int remaining;
  int step = current_step(t, &remaining);
  bool show_sec = step < 60;
  char buf[16];

  /* background + rules */
  graphics_context_set_fill_color(ctx, th.bg);
  graphics_fill_rect(ctx, full, 0, GCornerNone);

  int date_h  = compact ? 0 : DATE_H;
  int date_y  = H - date_h;
  int rule2_y = compact ? H : date_y - RULE;
  int half    = (W - RULE) / 2;

  graphics_context_set_fill_color(ctx, th.ink);
  graphics_fill_rect(ctx, GRect(0, HEADER_H, W, RULE), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(half, 0, RULE, HEADER_H), 0, GCornerNone);
  if (!compact) graphics_fill_rect(ctx, GRect(0, rule2_y, W, RULE), 0, GCornerNone);

  /* header modules */
  draw_header_cell(ctx, s.slot[0], 0, half, t, &th);
  draw_header_cell(ctx, s.slot[1], half + RULE, W - half - RULE, t, &th);

  /* time */
  int area_y = HEADER_H + RULE;
  if (clock_is_24h_style()) {
    strftime(buf, sizeof buf, "%H:%M", t);
  } else {
    strftime(buf, sizeof buf, "%I:%M", t);
    if (buf[0] == '0') memmove(buf, buf + 1, strlen(buf));
  }
  GSize ts = text_size(buf, s_font_time);
  int time_y = area_y + TIME_TOP - SZ_TIME / 5;
  draw_text(ctx, buf, s_font_time, GRect(PAD, time_y, W - 2 * PAD, ts.h + 2), GTextAlignmentLeft, th.ink);

  if (show_sec) {
    snprintf(buf, sizeof buf, "%02d", (t->tm_sec / step) * step);
    GSize ss = text_size(buf, s_font_mid);
    int sec_y = time_y + (ts.h - ss.h) - (SZ_TIME - SZ_MID) / 5;
    draw_text(ctx, buf, s_font_mid, GRect(PAD + ts.w + 5, sec_y, W, ss.h + 2), GTextAlignmentLeft, th.accent);
  }

  /* labels + progress bar */
  int bar_y   = rule2_y - PAD - BAR_H;
  int label_h = text_size("0", s_font_label).h;
  int label_y = bar_y - 4 - label_h;

  const char *mode = step == 60 ? "MINUTE" : step == 30 ? "30 SEC" : step == 15 ? "15 SEC"
                   : step == 5  ? "5 SEC"  : "SECONDS";
  draw_text(ctx, mode, s_font_label, GRect(PAD, label_y, W - 2 * PAD, label_h + 2), GTextAlignmentLeft, th.ink);

  int target_min = ((t->tm_min * 60 + t->tm_sec + remaining) / 60) % 60;
  if (show_sec) snprintf(buf, sizeof buf, ":%02d IN %d:%02d", target_min, remaining / 60, remaining % 60);
  else          snprintf(buf, sizeof buf, ":%02d IN %dM",     target_min, (remaining + 59) / 60);
  draw_text(ctx, buf, s_font_label, GRect(PAD, label_y, W - 2 * PAD, label_h + 2), GTextAlignmentRight, th.accent);

  int cells, filled;
  GColor fill_c;
  if (show_sec) { cells = 60 / step;      filled = t->tm_sec / step;                    fill_c = th.accent; }
  else          { cells = s.period / 60;  filled = ((t->tm_min * 60) % s.period) / 60;  fill_c = th.ink;    }
  int bar_w = W - 2 * PAD;
  for (int i = 0; i < cells; i++) {
    int x0 = PAD + (i * (bar_w + 1)) / cells;
    int x1 = PAD + ((i + 1) * (bar_w + 1)) / cells - 1;
    if (x1 <= x0) x1 = x0 + 1;
    GRect cell = GRect(x0, bar_y, x1 - x0, BAR_H);
    if (i < filled) {
      graphics_context_set_fill_color(ctx, fill_c);
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
    } else {
#if defined(PBL_COLOR)
      graphics_context_set_fill_color(ctx, th.empty);
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
#else
      graphics_context_set_stroke_color(ctx, th.ink);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_rect(ctx, cell);
#endif
    }
  }

  /* bottom modules (hidden while a quick view is on screen) */
  if (!compact) {
    int cw = (W - 2 * RULE) / 3;
    graphics_context_set_fill_color(ctx, th.ink);
    graphics_fill_rect(ctx, GRect(cw, date_y, RULE, DATE_H), 0, GCornerNone);
    graphics_fill_rect(ctx, GRect(2 * cw + RULE, date_y, RULE, DATE_H), 0, GCornerNone);
    for (int i = 0; i < 3; i++) {
      draw_bottom_cell(ctx, s.slot[2 + i], i * (cw + RULE), cw, date_y, H, t, &th, i == 1);
    }
  }
}

/* ============================ scheduling ============================ */
static void redraw(void) { layer_mark_dirty(s_canvas); }

static void schedule_timer(void);

static void check_vibe(struct tm *t) {
  int remaining;
  current_step(t, &remaining);
  if (remaining > 60) { s_vibed = false; return; }
  if (s.vibe && !s_vibed) { vibes_short_pulse(); s_vibed = true; }
}

static void timer_cb(void *ctx) {
  s_timer = NULL;
  time_t now = time(NULL);
  check_vibe(localtime(&now));
  redraw();
  schedule_timer();
}

// In the final four minutes, wake exactly on each 30/15/5/1-second boundary.
// Outside them no timer runs at all; the minute tick does everything.
static void schedule_timer(void) {
  if (s_timer) return;
  time_t sec; uint16_t ms;
  time_ms(&sec, &ms);
  struct tm *t = localtime(&sec);
  int step = current_step(t, NULL);
  if (step >= 60) return;
  int wait = (step - (t->tm_sec % step)) * 1000 - ms + 20;
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
static int tuple_int(Tuple *t, int base) {
  if (!t) return 0;
  if (t->type == TUPLE_CSTRING) return (int)strtol(t->value->cstring, NULL, base);
  return (int)t->value->int32;
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

  if (settings_changed) {
    if (s.period != 3600) s.period = 1800;
    persist_write_int(P_THEME, s.theme);
    persist_write_int(P_ACCENT, s.accent);
    for (int i = 0; i < 5; i++) persist_write_int(P_SLOT_TL + i, s.slot[i]);
    persist_write_int(P_PERIOD, s.period);
    persist_write_bool(P_VIBE, s.vibe);
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
  s_font_label = fonts_load_custom_font(resource_get_handle(RES_LABEL));

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
  app_message_open(256, 64);

  refresh_health();
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  unobstructed_area_service_subscribe((UnobstructedAreaHandlers){ .change = unobstructed_change }, NULL);
  schedule_timer();
}

static void deinit(void) {
  if (s_timer) app_timer_cancel(s_timer);
  tick_timer_service_unsubscribe();
  unobstructed_area_service_unsubscribe();
  window_destroy(s_window);
  gpath_destroy(s_heart_tri);
  fonts_unload_custom_font(s_font_time);
  fonts_unload_custom_font(s_font_mid);
  fonts_unload_custom_font(s_font_small);
  fonts_unload_custom_font(s_font_label);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
