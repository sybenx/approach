#include <pebble.h>

/* ============================== tunables ============================== */
#define PERIOD_SEC     1800   // countdown target every :00 and :30 (3600 = hourly)
#define WEATHER_EVERY  30     // minutes between weather refreshes

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
#endif

/* ============================== colours ============================== */
#define C_BG      GColorWhite
#define C_INK     GColorBlack
#define C_ACCENT  PBL_IF_COLOR_ELSE(GColorRed, GColorBlack)
#define C_DEEP    PBL_IF_COLOR_ELSE(GColorDarkCandyAppleRed, GColorBlack)
#define C_MUTED   PBL_IF_COLOR_ELSE(GColorDarkGray, GColorBlack)
#define C_EMPTY   PBL_IF_COLOR_ELSE(GColorLightGray, GColorWhite)

/* ============================== state =============================== */
enum { KEY_TEMP = 1, KEY_COND = 2 };

static Window   *s_window;
static Layer    *s_canvas;
static GFont     s_font_time, s_font_mid, s_font_small, s_font_label;
static AppTimer *s_timer;
static GPath    *s_heart_tri;
static GPoint    s_heart_pts[3];

static bool s_have_weather;
static int  s_temp;
static int  s_cond;        // 0 sun, 1 sun+cloud, 2 rain
static int  s_hr;          // 0 = unavailable

/* ============================ time maths ============================ */
static int current_step(struct tm *t, int *remaining_out) {
  int into = (t->tm_min * 60 + t->tm_sec) % PERIOD_SEC;
  int remaining = PERIOD_SEC - into;
  if (remaining_out) *remaining_out = remaining;
  if (remaining <= 60)  return 1;
  if (remaining <= 120) return 5;
  if (remaining <= 180) return 15;
  if (remaining <= 240) return 30;
  return 60;
}

/* =========================== text helpers =========================== */
static GSize text_size(const char *s, GFont f) {
  return graphics_text_layout_get_content_size(s, f, GRect(0, 0, 500, 200),
                                               GTextOverflowModeWordWrap, GTextAlignmentLeft);
}

static void draw_text(GContext *ctx, const char *s, GFont f, GRect box, GTextAlignment a, GColor c) {
  graphics_context_set_text_color(ctx, c);
  graphics_draw_text(ctx, s, f, box, GTextOverflowModeTrailingEllipsis, a, NULL);
}

// Vertically centre a one-line string inside a band.
static void draw_text_centered(GContext *ctx, const char *s, GFont f, int x, int w,
                               int band_y, int band_h, GTextAlignment a, GColor c) {
  GSize sz = text_size(s, f);
  draw_text(ctx, s, f, GRect(x, band_y + (band_h - sz.h) / 2, w, sz.h + 2), a, c);
}

/* ============================== icons =============================== */
static void draw_cloud(GContext *ctx, int x, int bottom, int sz) {
  // filled silhouette: two bumps on a flat base
  graphics_fill_circle(ctx, GPoint(x + sz * 6 / 10, bottom - sz * 42 / 100), sz * 28 / 100);
  graphics_fill_circle(ctx, GPoint(x + sz * 32 / 100, bottom - sz * 30 / 100), sz * 20 / 100);
  graphics_fill_rect(ctx, GRect(x + sz * 12 / 100, bottom - sz * 30 / 100, sz * 78 / 100, sz * 30 / 100), 2, GCornersAll);
}

static void draw_sun(GContext *ctx, GPoint c, int r, int ray_from, int ray_to, int rays) {
  graphics_fill_circle(ctx, c, r);
  for (int i = 0; i < rays; i++) {
    int32_t a = TRIG_MAX_ANGLE * i / rays;
    int32_t sx = sin_lookup(a), sy = -cos_lookup(a);
    GPoint p0 = GPoint(c.x + sx * ray_from / TRIG_MAX_RATIO, c.y + sy * ray_from / TRIG_MAX_RATIO);
    GPoint p1 = GPoint(c.x + sx * ray_to   / TRIG_MAX_RATIO, c.y + sy * ray_to   / TRIG_MAX_RATIO);
    graphics_draw_line(ctx, p0, p1);
  }
}

static void draw_weather_icon(GContext *ctx, GPoint o, int sz, int cond) {
  graphics_context_set_fill_color(ctx, C_INK);
  graphics_context_set_stroke_color(ctx, C_INK);
  graphics_context_set_stroke_width(ctx, sz >= 16 ? 2 : 1);
  int h = sz / 2;
  if (cond == 0) {
    draw_sun(ctx, GPoint(o.x + h, o.y + h), sz * 3 / 16, sz * 5 / 16, sz * 8 / 16, 8);
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

static void draw_heart(GContext *ctx, GPoint o, int sz) {
  graphics_context_set_fill_color(ctx, C_ACCENT);
  int r = sz * 26 / 100;
  graphics_fill_circle(ctx, GPoint(o.x + sz * 30 / 100, o.y + sz * 33 / 100), r);
  graphics_fill_circle(ctx, GPoint(o.x + sz * 70 / 100, o.y + sz * 33 / 100), r);
  gpath_move_to(s_heart_tri, o);
  gpath_draw_filled(ctx, s_heart_tri);
}

static void draw_battery(GContext *ctx, GPoint o, int sz, int pct) {
  int w = sz, h = sz * 55 / 100, y = o.y + (sz - h) / 2;
  graphics_context_set_stroke_color(ctx, C_INK);
  graphics_context_set_fill_color(ctx, C_INK);
  graphics_context_set_stroke_width(ctx, 1);
  graphics_draw_rect(ctx, GRect(o.x, y, w - 2, h));
  graphics_fill_rect(ctx, GRect(o.x + w - 2, y + h / 3, 2, h / 3), 0, GCornerNone);
  int fill = (w - 6) * pct / 100;
  if (fill > 0) graphics_fill_rect(ctx, GRect(o.x + 2, y + 2, fill, h - 4), 0, GCornerNone);
}

/* ============================== drawing ============================= */
static void canvas_update(Layer *layer, GContext *ctx) {
  GRect b = layer_get_bounds(layer);
  int W = b.size.w, H = b.size.h;

  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  int remaining;
  int step = current_step(t, &remaining);
  bool show_sec = step < 60;

  char buf[16];

  /* background + rules */
  graphics_context_set_fill_color(ctx, C_BG);
  graphics_fill_rect(ctx, b, 0, GCornerNone);

  int date_y  = H - DATE_H;
  int rule2_y = date_y - RULE;
  int half    = (W - RULE) / 2;

  graphics_context_set_fill_color(ctx, C_INK);
  graphics_fill_rect(ctx, GRect(0, HEADER_H, W, RULE), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(0, rule2_y, W, RULE), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(half, 0, RULE, HEADER_H), 0, GCornerNone);

  /* ---- header left: weather ---- */
  int icon_y = (HEADER_H - ICON) / 2;
  int tx = PAD + ICON + 5;
  if (s_have_weather) {
    draw_weather_icon(ctx, GPoint(PAD, icon_y), ICON, s_cond);
    snprintf(buf, sizeof buf, "%d°", s_temp);
  } else {
    draw_weather_icon(ctx, GPoint(PAD, icon_y), ICON, 1);
    snprintf(buf, sizeof buf, "--°");
  }
  draw_text_centered(ctx, buf, s_font_small, tx, half - tx, 0, HEADER_H, GTextAlignmentLeft, C_INK);

  /* ---- header right: heart rate, or battery if no sensor ---- */
  int rx = half + RULE + PAD;
  if (s_hr > 0) {
    draw_heart(ctx, GPoint(rx, icon_y + 1), ICON - 1);
    snprintf(buf, sizeof buf, "%d", s_hr);
  } else {
    draw_battery(ctx, GPoint(rx, icon_y), ICON, battery_state_service_peek().charge_percent);
    snprintf(buf, sizeof buf, "%d%%", battery_state_service_peek().charge_percent);
  }
  draw_text_centered(ctx, buf, s_font_small, rx + ICON + 5, W - (rx + ICON + 5), 0, HEADER_H,
                     GTextAlignmentLeft, C_INK);

  /* ---- time ---- */
  int area_y = HEADER_H + RULE;
  if (clock_is_24h_style()) {
    strftime(buf, sizeof buf, "%H:%M", t);
  } else {
    strftime(buf, sizeof buf, "%I:%M", t);
    if (buf[0] == '0') memmove(buf, buf + 1, strlen(buf));
  }
  GSize ts = text_size(buf, s_font_time);
  int time_y = area_y + TIME_TOP - SZ_TIME / 5;           // pull cap-height up to the padding line
  draw_text(ctx, buf, s_font_time, GRect(PAD, time_y, W - 2 * PAD, ts.h + 2), GTextAlignmentLeft, C_INK);

  if (show_sec) {
    snprintf(buf, sizeof buf, "%02d", (t->tm_sec / step) * step);
    GSize ss = text_size(buf, s_font_mid);
    int sec_y = time_y + (ts.h - ss.h) - (SZ_TIME - SZ_MID) / 5; // roughly baseline-aligned
    draw_text(ctx, buf, s_font_mid, GRect(PAD + ts.w + 5, sec_y, W, ss.h + 2), GTextAlignmentLeft, C_ACCENT);
  }

  /* ---- labels + progress bar ---- */
  int bar_y   = rule2_y - PAD - BAR_H;
  int label_h = text_size("0", s_font_label).h;
  int label_y = bar_y - 4 - label_h;

  const char *mode = step == 60 ? "MINUTE" : step == 30 ? "30 SEC" : step == 15 ? "15 SEC"
                   : step == 5  ? "5 SEC"  : "SECONDS";
  draw_text(ctx, mode, s_font_label, GRect(PAD, label_y, W - 2 * PAD, label_h + 2), GTextAlignmentLeft, C_INK);

  int target_min = ((t->tm_min * 60 + t->tm_sec + remaining) / 60) % 60;
  if (show_sec) snprintf(buf, sizeof buf, ":%02d IN %d:%02d", target_min, remaining / 60, remaining % 60);
  else          snprintf(buf, sizeof buf, ":%02d IN %dM",     target_min, (remaining + 59) / 60);
  draw_text(ctx, buf, s_font_label, GRect(PAD, label_y, W - 2 * PAD, label_h + 2), GTextAlignmentRight, C_DEEP);

  int cells, filled;
  GColor fill_c;
  if (show_sec) { cells = 60 / step;         filled = t->tm_sec / step;                     fill_c = C_ACCENT; }
  else          { cells = PERIOD_SEC / 60;   filled = ((t->tm_min * 60) % PERIOD_SEC) / 60; fill_c = C_INK;    }
  int bar_w = W - 2 * PAD;
  for (int i = 0; i < cells; i++) {
    int x0 = PAD + (i * (bar_w + 1)) / cells;
    int x1 = PAD + ((i + 1) * (bar_w + 1)) / cells - 1;
    GRect cell = GRect(x0, bar_y, x1 - x0, BAR_H);
    if (i < filled) {
      graphics_context_set_fill_color(ctx, fill_c);
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
    } else {
#if defined(PBL_COLOR)
      graphics_context_set_fill_color(ctx, C_EMPTY);
      graphics_fill_rect(ctx, cell, 0, GCornerNone);
#else
      graphics_context_set_stroke_color(ctx, C_INK);
      graphics_context_set_stroke_width(ctx, 1);
      graphics_draw_rect(ctx, cell);
#endif
    }
  }

  /* ---- date row ---- */
  int cw = (W - 2 * RULE) / 3;
  graphics_context_set_fill_color(ctx, C_INK);
  graphics_fill_rect(ctx, GRect(cw, date_y, RULE, DATE_H), 0, GCornerNone);
  graphics_fill_rect(ctx, GRect(2 * cw + RULE, date_y, RULE, DATE_H), 0, GCornerNone);

  const char *labels[3] = { "DAY", "DATE", "MONTH" };
  char vals[3][8];
  strftime(vals[0], sizeof vals[0], "%a", t);
  snprintf(vals[1], sizeof vals[1], "%d", t->tm_mday);
  strftime(vals[2], sizeof vals[2], "%b", t);
  for (int i = 0; i < 3; i++) {
    for (char *p = vals[i]; *p; p++) if (*p >= 'a' && *p <= 'z') *p -= 32;
    int x = i * (cw + RULE) + PAD;
    int w = cw - 2 * PAD;
    draw_text(ctx, labels[i], s_font_label, GRect(x, date_y + 5, w, label_h + 2), GTextAlignmentLeft, C_MUTED);
    GSize vs = text_size(vals[i], s_font_mid);
    int vy = H - 6 - vs.h + SZ_MID / 5;
    draw_text(ctx, vals[i], s_font_mid, GRect(x, vy, w + PAD, vs.h + 2), GTextAlignmentLeft,
              i == 1 ? C_ACCENT : C_INK);
  }
}

/* ============================ scheduling ============================ */
static void redraw(void) { layer_mark_dirty(s_canvas); }

static void schedule_timer(void);

static void timer_cb(void *ctx) {
  s_timer = NULL;
  redraw();
  schedule_timer();
}

// In the final four minutes, wake exactly on each 30/15/5/1-second boundary.
// Outside them, no timer runs at all — the minute tick does everything.
static void schedule_timer(void) {
  if (s_timer) return;
  time_t s; uint16_t ms;
  time_ms(&s, &ms);
  struct tm *t = localtime(&s);
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

static void inbox_cb(DictionaryIterator *it, void *ctx) {
  Tuple *tt = dict_find(it, MESSAGE_KEY_TEMP);
  Tuple *tc = dict_find(it, MESSAGE_KEY_COND);
  if (tt) { s_temp = tt->value->int32; s_have_weather = true; persist_write_int(KEY_TEMP, s_temp); }
  if (tc) { s_cond = tc->value->int32; persist_write_int(KEY_COND, s_cond); }
  redraw();
}

static void refresh_hr(void) {
#if defined(PBL_HEALTH)
  time_t now = time(NULL);
  if (health_service_metric_accessible(HealthMetricHeartRateBPM, now, now) & HealthServiceAccessibilityMaskAvailable) {
    s_hr = (int)health_service_peek_current_value(HealthMetricHeartRateBPM);
  } else {
    s_hr = 0;
  }
#endif
}

static void minute_tick(struct tm *t, TimeUnits units) {
  refresh_hr();
  if (t->tm_min % WEATHER_EVERY == 0) request_weather();
  redraw();
  schedule_timer();
}

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

  if (persist_exists(KEY_TEMP)) {
    s_temp = persist_read_int(KEY_TEMP);
    s_cond = persist_read_int(KEY_COND);
    s_have_weather = true;
  }

  s_window = window_create();
  window_set_window_handlers(s_window, (WindowHandlers){ .load = window_load, .unload = window_unload });
  window_stack_push(s_window, true);

  app_message_register_inbox_received(inbox_cb);
  app_message_open(64, 64);

  refresh_hr();
  tick_timer_service_subscribe(MINUTE_UNIT, minute_tick);
  schedule_timer();
}

static void deinit(void) {
  if (s_timer) app_timer_cancel(s_timer);
  tick_timer_service_unsubscribe();
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
