// UX for sonos-jukebox — the wall-mounted 7" controller. Implements core/unit.h.
//
// Laid out from the design system (`.claude/skills/sonos-jukebox-design/`), specifically
// `ui_kits/jukebox-screen/{App,NowPlaying}.jsx` and the component specs under `components/`.
// Read those before changing geometry, and prefer the tokens in ui_scale.h over ad-hoc values.
//
// Deviations from the design, all deliberate:
//   - Lucide icons -> LVGL's built-in symbol font. No icon set is converted for the device yet;
//     LV_SYMBOL_* is the closest equivalent and costs nothing.
//   - JetBrains Mono -> Montserrat. LVGL ships no mono face, so timecodes jitter slightly as
//     digits change width. A converted mono digit subset is the proper fix (see the TODO below).
//   - Type sizes snap to the nearest enabled Montserrat (52->48, 15->16, 13/10->12).
//
// House rule (CLAUDE.md > Architecture): the UI never calls Sonos/SOAP or board pins directly. It
// reads the mutex-guarded g_player, posts work to g_pending, and reaches hardware only through
// core/board.h.
#include <Arduino.h>
#include "core/net/logmirror.h"
#include <lvgl.h>

#include <math.h>       // sinf — the screensaver's drift path
#include <time.h>       // localtime_r — the screensaver clock (NTP + CLOCK_TZ, set in appBoot)
#include <vector>

#include "core/ui/album_art.h"
#include "core/board.h"
#include "core/library.h"
#include "core/settings.h"
#include "core/player_state.h"
#include "core/app.h"          // g_link* — netTask's link snapshot. NEVER call WiFi.* from here:
                              // on this board the radio is a co-processor and those are blocking
                              // RPCs, so a UI-task caller freezes rendering when the link dies.
#include "core/amazon.h"
#include "core/ui/art_cache.h"
#include "core/fav_cache.h"
#include "core/radio_cache.h"
#include "core/room_status.h"   // per-room volume + play state for the Rooms page (netTask polls)
#include "core/sonos/ssdp.h"
#include "core/unit.h"
#include "core/webconfig.h"    // webConfigReportLvMem — the LVGL pool sample for the health JSON
#include "../../boards/crowpanel_p4_7in/bringup_console.h"   // no-op unless the
                                                            // bring-up flag is set
#include "ui_scale.h"
#include "core/heap_watch.h"   // heapwatch::note — attribute the heap low-water (heap_watch.h)

// Two-glyph Lucide subset — see lv_font_lucide_28.c for why and how to regenerate.
LV_FONT_DECLARE(lv_font_lucide_28);
// Digits-only Montserrat at 120 px for the screensaver clock. Real font rather than a scaled 48 px
// label — see lv_font_clock_120.c for why that distinction is load-bearing here.
LV_FONT_DECLARE(lv_font_clock_120);
#define ICON_HEART "\xEE\x83\xB2"   // U+E0F2
#define ICON_RADIO "\xEE\x85\x82"   // U+E142
#define ICON_SPEAKER "\xEE\x85\xA6" // U+E166

// --- Geometry, from the design's device shell -------------------------------------------------
// Rail widened from the design's 66 px and its 48 px items scaled 1.5x to 72 px. A DELIBERATE
// deviation: 48 px is under the design system's own --hit-min of 44 px only on paper — in the hand
// it is too small to hit reliably on a wall-mounted panel, which is the whole point of this unit.
// Physical accuracy to the mock loses to being usable.
static const lv_coord_t RAIL_W    = 96;    // left nav rail (design: 66)
static const lv_coord_t RAIL_BTN  = 72;    // rail item (design: 48)
static const lv_coord_t RAIL_STEP = 86;    // item pitch
static const lv_coord_t PAD_X     = 30;    // content gutter (NowPlaying.jsx padding 22px 30px 18px)
static const lv_coord_t PAD_TOP   = 22;
static const lv_coord_t PAD_BOT   = 18;
static const lv_coord_t ART       = 280;   // album art tile, radius --r-lg
static const lv_coord_t GAP       = 34;    // art -> text column

// LVGL's built-in Montserrat fonts carry ASCII 0x20-0x7F and exactly two extras: 0xB0 (degree) and
// 0x2022 (bullet). Anything else — the U+00B7 MIDDLE DOT this file used as a separator, an em dash,
// an ellipsis — has no glyph and draws as a MISSING-GLYPH BOX on the panel. It looks like mojibake
// and there is no build warning. Use these, or extend the font's range; do not paste a nicer
// character in and assume it renders.
#define JB_SEP  "  \xE2\x80\xA2  "   // U+2022 BULLET, in range
#define JB_DASH "--"                 // stands in for an em dash

// --- Now Playing vertical rhythm --------------------------------------------------------------
// The text column is laid out against the ART TILE, not the page centre: the playhead's timecodes
// bottom-align with the bottom edge of the cover, so the two columns read as one block. Every
// value below is an absolute page Y, so the relationships are visible without running it.
//
// The title reserves TWO lines permanently. LV_LABEL_LONG_DOT only ellipsises when the label has a
// fixed height — without one the label grows downwards and a two-line title lands on top of the
// metadata line, which is exactly what it was doing.
static const lv_coord_t NP_ART_TOP  = (SCREEN_H - ART) / 2;            // 160
static const lv_coord_t NP_ART_BOT  = NP_ART_TOP + ART;                // 440
static const lv_coord_t NP_TITLE_LH = 52;   // lv_font_montserrat_48 .line_height
static const lv_coord_t NP_TITLE_H  = NP_TITLE_LH * 2;
static const lv_coord_t NP_META_H   = 24;   // lv_font_montserrat_22 .line_height
static const lv_coord_t NP_SMALL_H  = 15;   // lv_font_montserrat_12 .line_height
static const lv_coord_t NP_BADGE_Y  = NP_ART_TOP + 26;                 // 186
static const lv_coord_t NP_TITLE_Y  = NP_BADGE_Y + NP_SMALL_H + 13;    // 214
static const lv_coord_t NP_META_Y   = NP_TITLE_Y + NP_TITLE_H + 18;    // 336
static const lv_coord_t NP_TIMES_Y  = NP_ART_BOT - NP_SMALL_H;         // 425 -> bottom == art bottom
static const lv_coord_t NP_TRACK_Y  = NP_TIMES_Y - 8 - 6;              // 411

static lv_obj_t *s_content = nullptr;
static lv_obj_t *s_provisioning = nullptr;

// Pages live inside the content area and are shown/hidden rather than rebuilt: the design's rail
// is instant navigation, and tearing down LVGL trees on every switch would both stutter and churn
// the LV_MEM_SIZE pool.
// PAGE_FAVORITES is the Sonos favourites list (FV:2) — it was called "Radio" until it acquired a
// neighbour that actually is radio. PAGE_RADIO is Amazon Prime Stations, browsed from the SD cache.
enum Page { PAGE_NOW = 0, PAGE_FAVORITES = 1, PAGE_RADIO = 2, PAGE_ROOMS = 3, PAGE_SETTINGS = 4,
            PAGE_COUNT = 5 };
static lv_obj_t *s_page[PAGE_COUNT]     = {nullptr};
static lv_obj_t *s_railBtn[PAGE_COUNT]  = {nullptr};
static lv_obj_t *s_railIcon[PAGE_COUNT] = {nullptr};
static int s_cur = PAGE_NOW;

// Settings
static lv_obj_t *s_nameTa = nullptr, *s_kb = nullptr, *s_soundSlider = nullptr,
                *s_soundVal = nullptr, *s_saveHint = nullptr;

// Rooms — see the block above buildRooms() for the layout and the rebuild/refresh split.
static lv_obj_t *s_roomsWrap = nullptr;
static uint32_t s_roomsGen = UINT32_MAX;   // last-rendered g_zonesGen (topology => full rebuild)
static uint32_t s_roomsStatusGen = UINT32_MAX;   // last-rendered roomstatus::gen() (=> refresh)
static String   s_roomsActive;             // last-rendered active zone

// Group summary bar.
static lv_obj_t *s_grpCount = nullptr, *s_grpMembers = nullptr, *s_grpVolFill = nullptr,
                *s_grpVolPct = nullptr, *s_grpUngroup = nullptr, *s_grpUngroupLbl = nullptr,
                *s_grpPlay = nullptr, *s_grpPlayLbl = nullptr;

// One entry per room row. Rows are built ONLY when the topology changes and then updated in
// place: the status poller bumps its generation every ~400 ms, and tearing down nine rows of a
// dozen objects each at that rate would churn the LVGL pool continuously (CLAUDE.md: browse lists
// cost ~1 KB/row, and pool exhaustion freezes the UI rather than failing).
struct RoomRowUi {
  lv_obj_t *row = nullptr, *check = nullptr, *checkGlyph = nullptr, *name = nullptr,
           *badge = nullptr, *caption = nullptr, *volFill = nullptr, *volPct = nullptr,
           *minus = nullptr, *plus = nullptr, *play = nullptr, *playLbl = nullptr;
};
static std::vector<RoomRowUi> s_roomRows;
// The snapshot the rows were last drawn from. Callbacks read the room's IP/coordinator/volume
// from here, so they never touch sonos:: or roomstatus:: state directly on the UI task.
static std::vector<roomstatus::Room> s_roomsData;
// Optimistic transport, held briefly against the poller. Tapping play must light the button NOW,
// but the reading already in flight (or the one taken before Sonos acted) still says the old
// state, and refreshRooms() merges the poller over s_roomsData on every pass — without this the
// icon flicks straight back and only settles a round later. Volume has the same problem and is
// held inside roomstatus; transport is held here because only the UI knows a tap happened.
// Parallel to s_roomsData; resized with it in rebuildRooms().
static std::vector<uint32_t>       s_roomTransHoldUntil;
static std::vector<TransportState> s_roomTransOpt;
static const uint32_t kRoomTransHoldMs = 3000;

// Optimistic GROUP MEMBERSHIP, same idea, and it is what makes the checkbox feel instant. A tap
// cannot be confirmed until netTask has issued the SOAP op AND re-read the whole topology
// (ssdpDiscover is a full GetZoneGroupState fetch + parse) — easily a second. Until then every
// snapshot still reports the OLD coordinator, so without this the box stays unticked and the tap
// reads as ignored. Held longer than the others because a topology re-read is the slowest thing
// on this path; the g_zonesGen bump ends the hold early by forcing a full rebuild anyway.
static std::vector<uint32_t> s_roomGroupHoldUntil;
static std::vector<String>   s_roomGroupOpt;      // optimistic coordinatorUuid
static const uint32_t kRoomGroupHoldMs = 6000;

// Status bar
static lv_obj_t *s_dot = nullptr, *s_room = nullptr, *s_group = nullptr, *s_clock = nullptr;
// Now playing
static lv_obj_t *s_art = nullptr, *s_artImg = nullptr, *s_artPh = nullptr, *s_badgeSrc = nullptr, *s_title = nullptr, *s_meta = nullptr;
static lv_obj_t *s_elapsed = nullptr, *s_remain = nullptr, *s_track = nullptr, *s_fill = nullptr;
static lv_coord_t s_textW = 0;   // width of the Now Playing text column; placeTitle() measures against it
// The decoded cover, kept after Now Playing consumes it. albumArtTake() reports a CHANGE and hands
// the descriptor over once, so a second consumer (the screensaver) cannot ask for it again later —
// it has to be remembered here. nullptr means "no art", which the screensaver treats as "no cover
// layout available" rather than drawing an empty tile.
static const lv_image_dsc_t *s_artDsc = nullptr;

// Screensaver hooks, defined next to the screensaver itself further down. Forward-declared because
// the Settings page (built above it) has to tell it a setting moved.
static void saverArtChanged();
static void saverConfigChanged();
static void saverPreviewBrightness(int pct);
// Transport + volume
static lv_obj_t *s_play = nullptr, *s_playLbl = nullptr;
static lv_obj_t *s_volIcon = nullptr, *s_volFill = nullptr, *s_volPct = nullptr;

static bool s_wasPlaying = false;

// Last-rendered values. LVGL's setters do NOT compare — lv_label_set_text() copies and
// invalidates unconditionally — so calling them every tick redraws those regions every frame.
// With LV_DISPLAY_RENDER_MODE_DIRECT there is a single frame buffer that the DSI is scanning out
// live, so a needless redraw is visible as flicker/tearing. Only touch a widget when its value
// has actually changed.
struct Shown {
  // Seeded to a sentinel no real value can equal, for the SAME reason pct/elapsed/playing below
  // use out-of-range seeds: the first tick must always paint. An empty String here was a bug —
  // the meta label is built showing "starting up", so a track with no artist AND no album (a
  // direct Spotify track reports neither) rendered "" , compared equal to the empty cache, and the
  // boot placeholder stayed on screen forever under a correct title.
  String   room = "\x01", title = "\x01", meta = "\x01", badge = "\x01";
  int      pct = -1, volPct = -1;
  uint32_t elapsed = UINT32_MAX, remain = UINT32_MAX;
  int      playing = -1;      // tri-state so the first tick always paints
  uint8_t  volIcon = 0xFF;
  uint8_t  fillOpa = 0;       // scrubber fill opacity; dimmed when the duration is unknown
};
static Shown s_shown;

static inline void setTextIfChanged(lv_obj_t *l, String &cache, const String &next) {
  if (cache == next) return;
  cache = next;
  lv_label_set_text(l, next.c_str());
}

// The title slot is two lines tall whether or not the title needs both (LV_LABEL_LONG_DOT can only
// ellipsise against a FIXED height). A one-line title left at the top of that slot reads as a
// mis-alignment against the badge above and the metadata below, so measure the wrapped text and
// drop a short one by half a line to sit centred. Called on every title change, not per frame.
static void placeTitle() {
  if (!s_title || !s_textW) return;
  lv_point_t sz;
  lv_text_get_size(&sz, lv_label_get_text(s_title), &lv_font_montserrat_48,
                   lv_obj_get_style_text_letter_space(s_title, LV_PART_MAIN),
                   lv_obj_get_style_text_line_space(s_title, LV_PART_MAIN),
                   s_textW, LV_TEXT_FLAG_NONE);
  lv_obj_set_y(s_title, NP_TITLE_Y + (sz.y <= NP_TITLE_LH ? NP_TITLE_LH / 2 : 0));
}

// --- Commands ---------------------------------------------------------------------------------
static void prevCb(lv_event_t *) {
  uiSoundPlay(UiSound::Tick);
  if (stateLock()) { g_pending.restartTrack = true; stateUnlock(); }
}
static void nextCb(lv_event_t *) {
  uiSoundPlay(UiSound::Tick);
  if (stateLock()) { g_pending.next = true; stateUnlock(); }
}
static void playCb(lv_event_t *) {
  // Decide from the last rendered state, exactly as the nest does: the UI owns the intent, the
  // net task owns the SOAP call.
  uiSoundPlay(UiSound::Tick);
  if (stateLock()) { g_pending.setPlay = s_wasPlaying ? 0 : 1; stateUnlock(); }
}

// --- Physical dial ----------------------------------------------------------------------------
// The Modulino Knob on the I2C bus (boards/crowpanel_p4_7in/knob.cpp). The board only reports
// motion and press events; what they MEAN is decided here, exactly as wake-word phrases work on
// the sleep-machine — a board must never reach into g_pending itself.
//
// Bindings are global rather than per-page: twist = volume, press = play/pause, from anywhere.
// A jukebox dial that changed meaning depending on which page happened to be showing would be a
// worse physical control, and volume is the thing you reach for without looking.
static lv_obj_t *s_volToast = nullptr, *s_volToastFill = nullptr;
static lv_obj_t *s_volToastPct = nullptr, *s_volToastIcon = nullptr;
static uint32_t  s_volToastUntil = 0;

static const lv_coord_t VT_W = 300, VT_H = 84, VT_BAR_W = 210;
static const uint32_t   VT_HOLD_MS = 1400;

// --- Small builders ---------------------------------------------------------------------------
static lv_obj_t *panel(lv_obj_t *parent, lv_coord_t w, lv_coord_t h, uint32_t bg, lv_coord_t r) {
  lv_obj_t *o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, w, h);
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(o, r, 0);
  lv_obj_clear_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static lv_obj_t *label(lv_obj_t *parent, const char *txt, const lv_font_t *font, uint32_t colour) {
  lv_obj_t *l = lv_label_create(parent);
  lv_label_set_text(l, txt);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(colour), 0);
  return l;
}

// Volume readout for dial turns made away from Now Playing, which has no volume bar of its own.
// Built once and shown/hidden — never created per turn, which would churn the LVGL pool.
// It lives on the top layer so it floats over whichever page is up.
static void buildVolToast() {
  s_volToast = panel(lv_layer_top(), VT_W, VT_H, JB_SCREEN_ELEV, JB_R_LG);
  lv_obj_set_style_border_width(s_volToast, 1, 0);
  lv_obj_set_style_border_color(s_volToast, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_align(s_volToast, LV_ALIGN_BOTTOM_MID, 0, -46);
  lv_obj_add_flag(s_volToast, LV_OBJ_FLAG_HIDDEN);

  s_volToastIcon = label(s_volToast, LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_20, JB_TEXT);
  lv_obj_align(s_volToastIcon, LV_ALIGN_LEFT_MID, 22, -12);

  s_volToastPct = label(s_volToast, "0", &lv_font_montserrat_22, JB_TEXT);
  lv_obj_align(s_volToastPct, LV_ALIGN_RIGHT_MID, -22, -12);

  lv_obj_t *track = panel(s_volToast, VT_BAR_W, 6, JB_SCREEN_ELEV_2, 3);
  lv_obj_align(track, LV_ALIGN_BOTTOM_MID, 0, -20);
  s_volToastFill = panel(track, 0, 6, JB_ACCENT, 3);
  lv_obj_align(s_volToastFill, LV_ALIGN_LEFT_MID, 0, 0);
}

static void showVolToast(int vol) {
  if (!s_volToast) return;
  lv_obj_set_width(s_volToastFill, VT_BAR_W * vol / 100);
  char b[8];
  snprintf(b, sizeof(b), "%d", vol);
  lv_label_set_text(s_volToastPct, b);
  lv_label_set_text(s_volToastIcon, vol == 0 ? LV_SYMBOL_MUTE
                                  : (vol < 50 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX));
  lv_obj_remove_flag(s_volToast, LV_OBJ_FLAG_HIDDEN);
  s_volToastUntil = lv_tick_get() + VT_HOLD_MS;
}

// Round transport control. `solid` is the design's accent-filled primary — reserved for
// play/pause; everything else is `ghost`.
static lv_obj_t *transportBtn(lv_obj_t *parent, const char *sym, lv_coord_t d, bool solid,
                              lv_event_cb_t cb, lv_obj_t **labelOut = nullptr) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, d, d);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, solid ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(JB_ACCENT), 0);
  // Touch is the only input on this board until the physical caps are wired, so the pressed
  // state has to be unmistakable.
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_set_style_bg_color(b, lv_color_hex(solid ? JB_ACCENT_INK : JB_SCREEN_ELEV_2),
                            LV_STATE_PRESSED);
  if (cb) lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, nullptr);

  lv_obj_t *l = label(b, sym, &lv_font_montserrat_24, solid ? JB_ACCENT_INK : JB_TEXT_MUTED);
  lv_obj_center(l);
  if (labelOut) *labelOut = l;
  return b;
}

// --- Screen -----------------------------------------------------------------------------------
// Forward decls: buildRail() wires the nav callbacks, which are defined further down alongside
// the pages they switch to.
static void showPage(int page);
static void railCb(lv_event_t *e);

static void buildRail(lv_obj_t *scr) {
  lv_obj_t *rail = panel(scr, RAIL_W, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_align(rail, LV_ALIGN_TOP_LEFT, 0, 0);
  // Hairline divider — the design uses a 1px --screen-line border, not a filled panel.
  lv_obj_t *line = panel(scr, 1, SCREEN_H, JB_SCREEN_LINE, 0);
  lv_obj_align(line, LV_ALIGN_TOP_LEFT, RAIL_W, 0);

  // Now / Favorites / Radio / Rooms / Settings. Favorites and Radio use the real Lucide glyphs;
  // the rest stay on LVGL's built-in symbols, which already match well enough that subsetting more
  // of Lucide would be flash spent for no gain.
  const char *icons[PAGE_COUNT] = {LV_SYMBOL_AUDIO, ICON_HEART, ICON_RADIO,
                                   ICON_SPEAKER, LV_SYMBOL_SETTINGS};
  const lv_font_t *iconFonts[PAGE_COUNT] = {&lv_font_montserrat_28, &lv_font_lucide_28,
                                            &lv_font_lucide_28, &lv_font_lucide_28,
                                            &lv_font_montserrat_28};
  for (int i = 0; i < PAGE_COUNT; i++) {
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, RAIL_BTN, RAIL_BTN);
    lv_obj_set_style_radius(b, JB_R_LG, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(JB_SCREEN_BG), 0);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, (RAIL_W - RAIL_BTN) / 2, PAD_TOP + i * RAIL_STEP);
    lv_obj_add_event_cb(b, railCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = label(b, icons[i], iconFonts[i], JB_TEXT_DIM);
    lv_obj_center(l);
    s_railBtn[i] = b;
    s_railIcon[i] = l;
  }
}

static void buildStatusBar() {
  s_dot = panel(s_content, 9, 9, JB_ACCENT, LV_RADIUS_CIRCLE);
  lv_obj_align(s_dot, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 6);

  s_room = label(s_content, JB_DASH, &lv_font_montserrat_16, JB_TEXT);
  lv_obj_align(s_room, LV_ALIGN_TOP_LEFT, 18, PAD_TOP);

  s_group = label(s_content, "", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_group, LV_ALIGN_TOP_LEFT, 18, PAD_TOP + 20);

  s_clock = label(s_content, LV_SYMBOL_WIFI, &lv_font_montserrat_16, JB_TEXT_MUTED);
  lv_obj_align(s_clock, LV_ALIGN_TOP_RIGHT, 0, PAD_TOP);
}

static void buildNowPlaying() {
  // Album art. Solid --screen-elev until core/album_art delivers a real cover; the design uses a
  // rounded tile with a hairline, never a bare rectangle.
  s_art = panel(s_page[PAGE_NOW], ART, ART, JB_SCREEN_ELEV, JB_R_LG);
  lv_obj_align(s_art, LV_ALIGN_TOP_LEFT, 0, NP_ART_TOP);
  lv_obj_set_style_border_width(s_art, 1, 0);
  lv_obj_set_style_border_color(s_art, lv_color_hex(JB_SCREEN_LINE), 0);
  s_artPh = label(s_art, LV_SYMBOL_AUDIO, &lv_font_montserrat_48, JB_SCREEN_LINE);
  lv_obj_center(s_artPh);

  // Real cover, decoded by the art task into a PSRAM buffer (core/album_art). Sits above the
  // placeholder and is revealed only once there is something to show. TJpg picks a power-of-2
  // downscale so the long edge fits ART_MAX_PX (280 here), which means the decoded image is
  // often smaller than the tile — centre it rather than stretching.
  s_artImg = lv_image_create(s_art);
  lv_obj_center(s_artImg);
  lv_obj_add_flag(s_artImg, LV_OBJ_FLAG_HIDDEN);

  const lv_coord_t textX = ART + GAP;
  const lv_coord_t textW = SCREEN_W - RAIL_W - PAD_X * 2 - textX;
  s_textW = textW;

  s_badgeSrc = label(s_page[PAGE_NOW], "", &lv_font_montserrat_12, JB_ACCENT);
  lv_obj_align(s_badgeSrc, LV_ALIGN_TOP_LEFT, textX, NP_BADGE_Y);

  // Two reserved lines, ellipsised past that (see NP_TITLE_H). placeTitle() re-centres a one-line
  // title inside the slot on every change so a short title isn't pinned to the top of a hole.
  s_title = label(s_page[PAGE_NOW], "Sonos Jukebox", &lv_font_montserrat_48, JB_TEXT);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_set_size(s_title, textW, NP_TITLE_H);
  lv_obj_align(s_title, LV_ALIGN_TOP_LEFT, textX, NP_TITLE_Y);

  // Artist · album, ONE line. It used to be height-less too, so a long album name wrapped onto a
  // second line and landed on the scrubber.
  s_meta = label(s_page[PAGE_NOW], "starting up", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_label_set_long_mode(s_meta, LV_LABEL_LONG_DOT);
  lv_obj_set_size(s_meta, textW, NP_META_H);
  lv_obj_align(s_meta, LV_ALIGN_TOP_LEFT, textX, NP_META_Y);

  // Scrubber: 6px track, accent fill, timecodes beneath. The timecodes' baseline is the bottom of
  // the album art — anchor them to the track so the pair stays glued if the rhythm is retuned.
  s_track = panel(s_page[PAGE_NOW], textW, 6, JB_SCREEN_ELEV_2, 3);
  lv_obj_align(s_track, LV_ALIGN_TOP_LEFT, textX, NP_TRACK_Y);
  s_fill = panel(s_track, 0, 6, JB_ACCENT, 3);
  lv_obj_align(s_fill, LV_ALIGN_LEFT_MID, 0, 0);

  s_elapsed = label(s_page[PAGE_NOW], "0:00", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align_to(s_elapsed, s_track, LV_ALIGN_OUT_BOTTOM_LEFT, 0, 8);
  // Right-aligned against the end of the track. The old fixed -44 offset guessed the width of the
  // string, so "-1:08" and "-12:08" did not end in the same place.
  s_remain = label(s_page[PAGE_NOW], "-0:00", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align_to(s_remain, s_track, LV_ALIGN_OUT_BOTTOM_RIGHT, 0, 8);

  placeTitle();
}

static void buildTransport() {
  // Bottom row: volume left, transport right (design: VolumeBar flex + button cluster gap 14).
  const lv_coord_t rowY = -PAD_BOT;

  s_volIcon = label(s_page[PAGE_NOW], LV_SYMBOL_VOLUME_MAX, &lv_font_montserrat_20, JB_TEXT_MUTED);
  lv_obj_align(s_volIcon, LV_ALIGN_BOTTOM_LEFT, 0, rowY - 12);

  lv_obj_t *volTrack = panel(s_page[PAGE_NOW], 260, 6, JB_SCREEN_ELEV_2, 3);
  lv_obj_align(volTrack, LV_ALIGN_BOTTOM_LEFT, 34, rowY - 18);
  s_volFill = panel(volTrack, 0, 6, JB_ACCENT, 3);
  lv_obj_align(s_volFill, LV_ALIGN_LEFT_MID, 0, 0);

  s_volPct = label(s_page[PAGE_NOW], "0", &lv_font_montserrat_12, JB_TEXT_MUTED);
  lv_obj_align(s_volPct, LV_ALIGN_BOTTOM_LEFT, 306, rowY - 14);

  // 44px is the design system's --hit-min. These are the ONLY transport controls until the
  // physical caps are wired, so they are sized generously rather than as a secondary affordance.
  lv_obj_t *prev = transportBtn(s_page[PAGE_NOW], LV_SYMBOL_PREV, 56, false, prevCb);
  lv_obj_align(prev, LV_ALIGN_BOTTOM_RIGHT, -(72 + 56 + 28), rowY);

  s_play = transportBtn(s_page[PAGE_NOW], LV_SYMBOL_PLAY, 72, true, playCb, &s_playLbl);
  lv_obj_align(s_play, LV_ALIGN_BOTTOM_RIGHT, -(56 + 14), rowY);

  lv_obj_t *next = transportBtn(s_page[PAGE_NOW], LV_SYMBOL_NEXT, 56, false, nextCb);
  lv_obj_align(next, LV_ALIGN_BOTTOM_RIGHT, 0, rowY);
}

static void showPage(int page) {
  if (page < 0 || page >= PAGE_COUNT) return;

  // Leaving Favorites no longer needs to free anything: the list is rebuilt from the SD cache on
  // entry, and the LVGL rows are replaced wholesale by favShowAll(). The old code also had to drop
  // a library:: result set, which the cache made unnecessary.

  s_cur = page;
  for (int i = 0; i < PAGE_COUNT; i++) {
    if (!s_page[i]) continue;
    if (i == page) lv_obj_remove_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
    else           lv_obj_add_flag(s_page[i], LV_OBJ_FLAG_HIDDEN);
    // Rail selected state: accent tint + accent glyph, per the design's nav rail.
    if (s_railBtn[i]) {
      lv_obj_set_style_bg_color(s_railBtn[i],
                                lv_color_hex(i == page ? JB_SCREEN_ELEV_2 : JB_SCREEN_BG), 0);
      lv_obj_set_style_text_color(s_railIcon[i],
                                  lv_color_hex(i == page ? JB_ACCENT : JB_TEXT_DIM), 0);
    }
  }
}

static void railCb(lv_event_t *e) {
  uiSoundPlay(UiSound::Tick);
  showPage((int)(intptr_t)lv_event_get_user_data(e));
}

// --- Rooms ------------------------------------------------------------------------------------
// Per `ui_kits/jukebox-screen/RoomPicker.jsx`: a group summary bar over a scrolling list of room
// rows, each with a group checkbox, name + state, volume and controls.
//
// TWO-PHASE RENDER, and the split matters. rebuildRooms() creates the LVGL tree and runs only when
// the TOPOLOGY changes (g_zonesGen); refreshRooms() only sets text, colours and bar widths, and
// runs whenever the status poller reports new readings (~every 400 ms). Rebuilding on every poll
// would recycle ~12 objects x N rooms several times a second through the LV_MEM pool.
//
// DEVIATIONS from the design, all deliberate:
//   - Controls are bigger than the mock's 26/32 px: this is a wall panel, and the design system's
//     own --hit-min is 44. Same reasoning already applied to the nav rail (48 -> 72).
//   - Per-row play/pause appears ONLY on group coordinators and ungrouped rooms. Sonos transport
//     is a property of the GROUP — there is no way to pause one member of a group — so a button on
//     a grouped member would silently act on its whole group. The design puts one on every row;
//     showing a control that lies about its scope is worse than omitting it.
//   - The active room's checkbox is disabled: a group's own anchor cannot be removed from it.
static const lv_coord_t RM_W       = SCREEN_W - RAIL_W - PAD_X * 2;   // 868
static const lv_coord_t RM_BAR_Y   = 76;
static const lv_coord_t RM_BAR_H   = 80;
static const lv_coord_t RM_LIST_Y  = 192;
static const lv_coord_t RM_ROW_H   = 62;
static const lv_coord_t RM_ROW_PITCH = 66;

// Row geometry (x offsets within a row of width RM_W).
static const lv_coord_t RX_CHECK = 8,   RC_SZ    = 28;
static const lv_coord_t RX_NAME  = 52,  RN_W     = 156;
static const lv_coord_t RX_BADGE = 214;
static const lv_coord_t RX_VOL   = 272, RV_W     = 300;
static const lv_coord_t RX_VPCT  = 584;
static const lv_coord_t RX_MINUS = 708, RX_PLUS  = 760, R_STEP_SZ = 44;
static const lv_coord_t RX_PLAY  = 812, R_PLAY_SZ = 48;

static void rebuildRooms();
static void refreshRooms();

// Which room is the active one, and what group is it in? Both come from the snapshot the rows were
// drawn from, so every callback agrees with what is on screen.
static int activeRoomIdx() {
  String cur;
  if (stateLock()) { cur = g_player.zoneName; stateUnlock(); }
  for (size_t i = 0; i < s_roomsData.size(); ++i)
    if (s_roomsData[i].name == cur) return (int)i;
  return -1;
}

// Takes the anchor index rather than looking it up: the render loop calls this once per row, and
// activeRoomIdx() acquires g_stateMutex — nine takes per repaint, at ~2.5 repaints a second, for a
// value that cannot change within a single pass.
static bool roomInActiveGroup(size_t i, int a) {
  if (a < 0 || i >= s_roomsData.size()) return false;
  return s_roomsData[i].coordinatorUuid == s_roomsData[(size_t)a].coordinatorUuid;
}

// Record an optimistic transport state for a row and start its hold window. Applies to the whole
// GROUP, not just the row: Sonos moves every member together, so holding only the tapped row would
// let its group-mates flicker to the stale reading instead.
static void noteRoomTransport(size_t i, TransportState st) {
  if (i >= s_roomsData.size() || s_roomTransOpt.size() != s_roomsData.size()) return;
  const String coord = s_roomsData[i].coordinatorUuid;
  const uint32_t until = lv_tick_get() + kRoomTransHoldMs;
  for (size_t k = 0; k < s_roomsData.size(); ++k) {
    if (s_roomsData[k].coordinatorUuid != coord) continue;
    s_roomsData[k].transport = st;
    s_roomsData[k].transportOk = true;
    s_roomTransOpt[k] = st;
    s_roomTransHoldUntil[k] = until;
  }
}

// --- Row callbacks ------------------------------------------------------------------------------

// The checkbox: add this room to the active group, or split it back out. Queued (not assigned) so
// several quick taps all survive — see PendingCmds::groupOps.
static void roomCheckCb(lv_event_t *e) {
  const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= s_roomsData.size()) return;
  const int a = activeRoomIdx();
  if (a < 0 || (int)i == a) return;    // the anchor cannot leave its own group
  const bool joining = !roomInActiveGroup(i, a);
  uiSoundPlay(UiSound::Confirm);
  LOG.printf("[ui    ] group %s %s\n", joining ? "+" : "-", s_roomsData[i].name.c_str());
  if (stateLock()) {
    g_pending.groupOps.push_back({s_roomsData[i].ip, joining});
    stateUnlock();
  }

  // Show it NOW. Joining adopts the active group's coordinator and stops being one; leaving makes
  // the room its own coordinator. Both are exactly what the topology re-read will confirm.
  if (s_roomGroupOpt.size() == s_roomsData.size()) {
    s_roomsData[i].coordinatorUuid = joining ? s_roomsData[(size_t)a].coordinatorUuid
                                             : s_roomsData[i].uuid;
    s_roomsData[i].isCoordinator = !joining;
    if (joining) {
      // A room that just joined inherits the group's play state; leaving it on its own old
      // reading would caption it "Idle" next to rooms that are playing the same thing.
      s_roomsData[i].transport   = s_roomsData[(size_t)a].transport;
      s_roomsData[i].transportOk = s_roomsData[(size_t)a].transportOk;
    }
    s_roomGroupOpt[i] = s_roomsData[i].coordinatorUuid;
    s_roomGroupHoldUntil[i] = lv_tick_get() + kRoomGroupHoldMs;
  }
  refreshRooms();
}

// The name: make this the controlled room. Unlike the old chip grid this does NOT jump to Now
// Playing — the new design keeps you on Rooms (the MAIN badge is the feedback) so that picking a
// room and then grouping to it is one uninterrupted gesture.
static void roomNameCb(lv_event_t *e) {
  const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= s_roomsData.size()) return;
  uiSoundPlay(UiSound::Confirm);
  if (stateLock()) { g_pending.requestZoneIp = s_roomsData[i].ip; stateUnlock(); }
}

// Volume steppers, +/-5 per the design. The new level is computed from the DISPLAYED value and
// pushed back into the status cache immediately, so repeated taps accumulate instead of all
// deriving from the same stale reading, and the bar tracks the finger.
static void roomVolStep(size_t i, int delta) {
  if (i >= s_roomsData.size()) return;
  const int next = constrain((int)s_roomsData[i].vol + delta, 0, 100);
  if (next == (int)s_roomsData[i].vol && s_roomsData[i].volOk) return;
  uiSoundPlay(UiSound::Tick);
  s_roomsData[i].vol = (uint8_t)next;
  s_roomsData[i].volOk = true;
  roomstatus::noteVolume(s_roomsData[i].ip, (uint8_t)next);
  if (stateLock()) {
    g_pending.roomVolIp = s_roomsData[i].ip;
    g_pending.roomVolTarget = next;
    stateUnlock();
  }
  refreshRooms();   // paint this frame, not on the next poll
}
static void roomVolDownCb(lv_event_t *e) {
  roomVolStep((size_t)(intptr_t)lv_event_get_user_data(e), -5);
}
static void roomVolUpCb(lv_event_t *e) {
  roomVolStep((size_t)(intptr_t)lv_event_get_user_data(e), +5);
}

// Per-row play/pause. Only built for coordinators/ungrouped rooms, and always addressed to the
// coordinator IP — that is the only thing Sonos will accept a transport command on.
static void roomPlayCb(lv_event_t *e) {
  const size_t i = (size_t)(intptr_t)lv_event_get_user_data(e);
  if (i >= s_roomsData.size()) return;
  const bool playing = s_roomsData[i].transportOk &&
                       s_roomsData[i].transport == TransportState::Playing;
  uiSoundPlay(UiSound::Confirm);
  const String target = s_roomsData[i].coordIp.length() ? s_roomsData[i].coordIp
                                                        : s_roomsData[i].ip;
  if (stateLock()) {
    g_pending.roomPlayCoordIp = target;
    g_pending.roomSetPlay = playing ? 0 : 1;
    stateUnlock();
  }
  // Optimistic, and held so the merge in refreshRooms() can't immediately undo it.
  noteRoomTransport(i, playing ? TransportState::Paused : TransportState::Playing);
  refreshRooms();
}

// --- Group summary callbacks ---------------------------------------------------------------------

static void ungroupCb(lv_event_t *e) {
  (void)e;
  uiSoundPlay(UiSound::Confirm);
  LOG.println("[ui    ] ungroup all");
  if (stateLock()) { g_pending.ungroupAll = true; stateUnlock(); }
}

static void groupPlayCb(lv_event_t *e) {
  (void)e;
  const int a = activeRoomIdx();
  if (a < 0) return;
  const bool playing = s_roomsData[a].transportOk &&
                       s_roomsData[a].transport == TransportState::Playing;
  uiSoundPlay(UiSound::Confirm);
  const String target = s_roomsData[a].coordIp.length() ? s_roomsData[a].coordIp
                                                        : s_roomsData[a].ip;
  if (stateLock()) {
    g_pending.roomPlayCoordIp = target;
    g_pending.roomSetPlay = playing ? 0 : 1;
    stateUnlock();
  }
  noteRoomTransport((size_t)a, playing ? TransportState::Paused : TransportState::Playing);
  refreshRooms();
}

// --- Builders -------------------------------------------------------------------------------------

// A square, elevated control (the design's stepper: r-10, --screen-line border, --screen-elev-2).
static lv_obj_t *roomStepBtn(lv_obj_t *parent, const char *sym, lv_coord_t sz, lv_event_cb_t cb,
                             size_t idx) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, sz, sz);
  lv_obj_set_style_radius(b, 10, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  lv_obj_set_style_border_width(b, 1, 0);
  lv_obj_set_style_border_color(b, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(JB_ACCENT), LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
  lv_obj_t *l = label(b, sym, &lv_font_montserrat_20, JB_TEXT_MUTED);
  lv_obj_center(l);
  return b;
}

// Round play/pause, sized for touch. Kept local rather than reusing transportBtn() because these
// need a per-row index in user_data.
static lv_obj_t *roomPlayBtn(lv_obj_t *parent, lv_coord_t sz, lv_event_cb_t cb, size_t idx,
                             lv_obj_t **lblOut) {
  lv_obj_t *b = lv_button_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_set_size(b, sz, sz);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(JB_ACCENT), LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void *)(intptr_t)idx);
  *lblOut = label(b, LV_SYMBOL_PLAY, &lv_font_montserrat_20, JB_TEXT);
  lv_obj_center(*lblOut);
  return b;
}

// Full teardown + rebuild. Topology changes only.
static void rebuildRooms() {
  lv_obj_clean(s_roomsWrap);
  s_roomRows.clear();

  roomstatus::snapshot(s_roomsData);
  if (s_roomsData.empty()) {
    // Fall back to plain discovery so the page says something useful before the first poll round
    // (roomstatus only populates once netTask has seen the page asking).
    std::vector<sonos::Zone> zs;
    sonos::zonesSnapshot(zs);
    if (zs.empty()) {
      lv_obj_t *l = label(s_roomsWrap, "Searching for speakers" LV_SYMBOL_REFRESH,
                          &lv_font_montserrat_22, JB_TEXT_MUTED);
      lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
      return;
    }
    for (const auto &z : zs) {
      roomstatus::Room r;
      r.name = z.name; r.ip = z.ip; r.uuid = z.uuid;
      r.coordinatorUuid = z.coordinatorUuid; r.coordIp = z.coordIp;
      r.isCoordinator = z.isCoordinator;
      s_roomsData.push_back(r);
    }
  }

  s_roomRows.resize(s_roomsData.size());
  // Held optimistic state is per-row and the row set just changed — start clean. A rebuild is
  // triggered by the g_zonesGen bump, i.e. the topology re-read that CONFIRMS a pending checkbox
  // tap, so dropping the holds here is exactly right: the real answer has arrived.
  s_roomTransHoldUntil.assign(s_roomsData.size(), 0);
  s_roomTransOpt.assign(s_roomsData.size(), TransportState::Unknown);
  s_roomGroupHoldUntil.assign(s_roomsData.size(), 0);
  s_roomGroupOpt.assign(s_roomsData.size(), String());
  for (size_t i = 0; i < s_roomsData.size(); ++i) {
    RoomRowUi &u = s_roomRows[i];

    u.row = panel(s_roomsWrap, RM_W, RM_ROW_H, JB_SCREEN_ELEV, JB_R_MD);
    lv_obj_set_style_border_width(u.row, 1, 0);
    // set_pos, not align: aligned children don't extend the scrollable content area, so rows past
    // the fold would be unreachable (the same trap the old chip grid hit).
    lv_obj_set_pos(u.row, 0, (lv_coord_t)(i * RM_ROW_PITCH));

    u.check = lv_button_create(u.row);
    lv_obj_remove_style_all(u.check);
    lv_obj_set_size(u.check, RC_SZ, RC_SZ);
    lv_obj_set_style_radius(u.check, 8, 0);
    lv_obj_set_style_bg_opa(u.check, LV_OPA_COVER, 0);
    lv_obj_align(u.check, LV_ALIGN_LEFT_MID, RX_CHECK, 0);
    lv_obj_add_event_cb(u.check, roomCheckCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    u.checkGlyph = label(u.check, LV_SYMBOL_OK, &lv_font_montserrat_16, JB_ACCENT_INK);
    lv_obj_center(u.checkGlyph);

    // The name is a button so the whole block is a comfortable target, not just the glyph.
    lv_obj_t *nameBtn = lv_button_create(u.row);
    lv_obj_remove_style_all(nameBtn);
    lv_obj_set_size(nameBtn, RN_W + 40, RM_ROW_H);
    lv_obj_align(nameBtn, LV_ALIGN_LEFT_MID, RX_NAME, 0);
    lv_obj_add_event_cb(nameBtn, roomNameCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    u.name = label(u.row, s_roomsData[i].name.c_str(), &lv_font_montserrat_16, JB_TEXT);
    lv_label_set_long_mode(u.name, LV_LABEL_LONG_DOT);
    lv_obj_set_width(u.name, RN_W);
    lv_obj_align(u.name, LV_ALIGN_LEFT_MID, RX_NAME, -10);

    u.badge = label(u.row, "MAIN", &lv_font_montserrat_12, JB_ACCENT);
    lv_obj_align(u.badge, LV_ALIGN_LEFT_MID, RX_BADGE, -10);

    u.caption = label(u.row, "", &lv_font_montserrat_12, JB_TEXT_DIM);
    lv_obj_align(u.caption, LV_ALIGN_LEFT_MID, RX_NAME, 12);

    lv_obj_t *track = panel(u.row, RV_W, 6, JB_SCREEN_ELEV_2, 3);
    lv_obj_align(track, LV_ALIGN_LEFT_MID, RX_VOL, 0);
    u.volFill = panel(track, 0, 6, JB_ACCENT, 3);
    lv_obj_align(u.volFill, LV_ALIGN_LEFT_MID, 0, 0);

    u.volPct = label(u.row, "--", &lv_font_montserrat_12, JB_TEXT_DIM);
    lv_obj_align(u.volPct, LV_ALIGN_LEFT_MID, RX_VPCT, 0);

    u.minus = roomStepBtn(u.row, LV_SYMBOL_MINUS, R_STEP_SZ, roomVolDownCb, i);
    lv_obj_align(u.minus, LV_ALIGN_LEFT_MID, RX_MINUS, 0);
    u.plus = roomStepBtn(u.row, LV_SYMBOL_PLUS, R_STEP_SZ, roomVolUpCb, i);
    lv_obj_align(u.plus, LV_ALIGN_LEFT_MID, RX_PLUS, 0);

    u.play = roomPlayBtn(u.row, R_PLAY_SZ, roomPlayCb, i, &u.playLbl);
    lv_obj_align(u.play, LV_ALIGN_LEFT_MID, RX_PLAY, 0);
  }

  refreshRooms();
}

// Cheap in-place update: text, colours, bar widths. No allocation, no object churn.
static void refreshRooms() {
  if (s_roomRows.empty()) return;

  // Re-read the poller, but keep the existing row count: if discovery has changed the room set,
  // rebuildRooms() will run on the g_zonesGen bump and this pass would only mis-index.
  std::vector<roomstatus::Room> fresh;
  roomstatus::snapshot(fresh);
  if (fresh.size() == s_roomsData.size()) {
    for (size_t i = 0; i < fresh.size(); ++i) {
      // Preserve an optimistic volume the user just set; roomstatus holds it too, but this pass
      // may run from the stepper callback before the poller has seen it at all.
      // Keep a local reading the poller does not have yet — either seeded from g_player on page
      // entry, or set optimistically by a stepper. Only ever when fresh has nothing: a real
      // reading always wins.
      const bool keepVol = s_roomsData[i].volOk && !fresh[i].volOk;
      const uint8_t v = s_roomsData[i].vol;
      const bool keepTrans = s_roomsData[i].transportOk && !fresh[i].transportOk;
      const TransportState ts = s_roomsData[i].transport;
      // Within its hold window a tapped row keeps the state the user just asked for.
      const bool holdTrans = i < s_roomTransHoldUntil.size() &&
                             (int32_t)(lv_tick_get() - s_roomTransHoldUntil[i]) < 0;
      const TransportState opt = holdTrans ? s_roomTransOpt[i] : TransportState::Unknown;
      s_roomsData[i] = fresh[i];
      if (keepVol) { s_roomsData[i].vol = v; s_roomsData[i].volOk = true; }
      if (keepTrans) { s_roomsData[i].transport = ts; s_roomsData[i].transportOk = true; }
      if (holdTrans) { s_roomsData[i].transport = opt; s_roomsData[i].transportOk = true; }
    }

    // Re-apply any still-pending checkbox taps over the freshly merged topology.
    for (size_t i = 0; i < s_roomsData.size() && i < s_roomGroupHoldUntil.size(); ++i) {
      if ((int32_t)(lv_tick_get() - s_roomGroupHoldUntil[i]) >= 0) continue;
      s_roomsData[i].coordinatorUuid = s_roomGroupOpt[i];
      s_roomsData[i].isCoordinator = (s_roomGroupOpt[i] == s_roomsData[i].uuid);
    }

    // Group sizes come from the snapshot, which predates those optimistic edits — recount over
    // what is actually about to be drawn, or a just-joined room still captions itself ungrouped.
    for (auto &r : s_roomsData) {
      uint8_t n = 0;
      for (const auto &o : s_roomsData) if (o.coordinatorUuid == r.coordinatorUuid) n++;
      r.groupSize = n ? n : 1;
    }
  }

  const int a = activeRoomIdx();
  int    groupSize = 0;
  String members;
  int    groupVolSum = 0, groupVolN = 0;

  for (size_t i = 0; i < s_roomRows.size() && i < s_roomsData.size(); ++i) {
    RoomRowUi &u = s_roomRows[i];
    const roomstatus::Room &r = s_roomsData[i];
    const bool active  = ((int)i == a);
    const bool inGroup = roomInActiveGroup(i, a);
    const bool playing = r.transportOk && r.transport == TransportState::Playing;

    if (inGroup) {
      groupSize++;
      if (members.length()) members += JB_SEP;
      members += r.name;
      if (r.volOk) { groupVolSum += r.vol; groupVolN++; }
    }

    // Row surface: in-group rows are elevated, the active one takes the accent hairline.
    lv_obj_set_style_bg_opa(u.row, inGroup ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_color(u.row,
        lv_color_hex(active ? JB_ACCENT : (inGroup ? JB_SCREEN_LINE : JB_SCREEN_BG)), 0);

    // Checkbox: accent fill + tick when grouped, hollow outline when not.
    lv_obj_set_style_bg_opa(u.check, inGroup ? LV_OPA_COVER : LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_color(u.check, lv_color_hex(JB_ACCENT), 0);
    lv_obj_set_style_border_width(u.check, inGroup ? 0 : 2, 0);
    lv_obj_set_style_border_color(u.check, lv_color_hex(JB_TEXT_DIM), 0);
    if (inGroup) lv_obj_remove_flag(u.checkGlyph, LV_OBJ_FLAG_HIDDEN);
    else         lv_obj_add_flag(u.checkGlyph, LV_OBJ_FLAG_HIDDEN);
    // The anchor cannot be removed from its own group, so its box is inert and reads as such.
    if (active) {
      lv_obj_add_state(u.check, LV_STATE_DISABLED);
      lv_obj_set_style_bg_opa(u.check, LV_OPA_40, 0);
    } else {
      lv_obj_remove_state(u.check, LV_STATE_DISABLED);
    }

    lv_obj_set_style_text_color(u.name, lv_color_hex(inGroup ? JB_TEXT : JB_TEXT_MUTED), 0);
    if (active) lv_obj_remove_flag(u.badge, LV_OBJ_FLAG_HIDDEN);
    else        lv_obj_add_flag(u.badge, LV_OBJ_FLAG_HIDDEN);

    // Caption, per the design: "Playing / grouped" only when the group really has >1 member.
    const char *cap;
    if (!r.transportOk)   cap = "--";
    else if (!playing)    cap = "Idle";
    else if (r.groupSize > 1) cap = "Playing" JB_SEP "grouped";
    else                  cap = "Playing";
    lv_label_set_text(u.caption, cap);

    // Volume. Dimmed when the room is idle, per the design's opacity .45.
    lv_obj_set_width(u.volFill, r.volOk ? (lv_coord_t)(RV_W * r.vol / 100) : 0);
    lv_obj_set_style_bg_opa(u.volFill, playing ? LV_OPA_COVER : LV_OPA_50, 0);
    char pb[8];
    if (r.volOk) snprintf(pb, sizeof(pb), "%u", (unsigned)r.vol);
    else         snprintf(pb, sizeof(pb), "--");
    lv_label_set_text(u.volPct, pb);

    // Play/pause only where it is honest — see the header comment.
    const bool showPlay = r.isCoordinator || r.groupSize <= 1;
    if (showPlay) {
      lv_obj_remove_flag(u.play, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(u.playLbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
      lv_obj_set_style_bg_color(u.play, lv_color_hex(playing ? JB_ACCENT : JB_SCREEN_ELEV_2), 0);
      lv_obj_set_style_text_color(u.playLbl, lv_color_hex(playing ? JB_ACCENT_INK : JB_TEXT_MUTED), 0);
    } else {
      lv_obj_add_flag(u.play, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // --- Group summary bar ---
  if (!s_grpCount) return;
  const bool grouped = groupSize > 1;
  char cb[32];
  if (a < 0)        snprintf(cb, sizeof(cb), "No room");
  else if (grouped) snprintf(cb, sizeof(cb), "%d rooms", groupSize);
  else              snprintf(cb, sizeof(cb), "%s", s_roomsData[a].name.c_str());
  lv_label_set_text(s_grpCount, cb);
  lv_label_set_text(s_grpMembers, grouped ? members.c_str() : "Playing alone");

  const int gv = groupVolN ? (groupVolSum / groupVolN) : 0;
  lv_obj_set_width(s_grpVolFill, (lv_coord_t)(320 * gv / 100));
  char gb[8];
  if (groupVolN) snprintf(gb, sizeof(gb), "%d", gv);
  else           snprintf(gb, sizeof(gb), "--");
  lv_label_set_text(s_grpVolPct, gb);

  // UNGROUP is meaningless on a group of one — dim it rather than let it no-op silently.
  if (grouped) {
    lv_obj_remove_state(s_grpUngroup, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(s_grpUngroupLbl, lv_color_hex(JB_TEXT), 0);
    lv_obj_set_style_border_color(s_grpUngroup, lv_color_hex(JB_SCREEN_LINE), 0);
  } else {
    lv_obj_add_state(s_grpUngroup, LV_STATE_DISABLED);
    lv_obj_set_style_text_color(s_grpUngroupLbl, lv_color_hex(JB_TEXT_DIM), 0);
    lv_obj_set_style_border_color(s_grpUngroup, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  }

  const bool anyPlaying = (a >= 0) && s_roomsData[a].transportOk &&
                          s_roomsData[a].transport == TransportState::Playing;
  lv_label_set_text(s_grpPlayLbl, anyPlaying ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
}

static void buildRooms() {
  lv_obj_t *pg = s_page[PAGE_ROOMS];

  // Group summary bar. No "Rooms" heading: the design drops it, and the shared status bar already
  // names the controlled room.
  lv_obj_t *bar = panel(pg, RM_W, RM_BAR_H, JB_SCREEN_ELEV, JB_R_LG);
  lv_obj_align(bar, LV_ALIGN_TOP_LEFT, 0, RM_BAR_Y);
  lv_obj_set_style_border_width(bar, 1, 0);
  lv_obj_set_style_border_color(bar, lv_color_hex(JB_SCREEN_LINE), 0);

  s_grpCount = label(bar, "--", &lv_font_montserrat_24, JB_TEXT);
  lv_obj_align(s_grpCount, LV_ALIGN_TOP_LEFT, 18, 14);
  s_grpMembers = label(bar, "", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_label_set_long_mode(s_grpMembers, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_grpMembers, 260);
  lv_obj_align(s_grpMembers, LV_ALIGN_TOP_LEFT, 18, 48);

  lv_obj_t *gvl = label(bar, "GROUP VOLUME", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(gvl, LV_ALIGN_TOP_LEFT, 300, 18);
  lv_obj_t *gtrack = panel(bar, 320, 6, JB_SCREEN_ELEV_2, 3);
  lv_obj_align(gtrack, LV_ALIGN_TOP_LEFT, 300, 46);
  s_grpVolFill = panel(gtrack, 0, 6, JB_ACCENT, 3);
  lv_obj_align(s_grpVolFill, LV_ALIGN_LEFT_MID, 0, 0);
  s_grpVolPct = label(bar, "--", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_grpVolPct, LV_ALIGN_TOP_LEFT, 628, 42);

  s_grpUngroup = lv_button_create(bar);
  lv_obj_remove_style_all(s_grpUngroup);
  lv_obj_set_size(s_grpUngroup, 120, 44);
  lv_obj_set_style_radius(s_grpUngroup, 10, 0);
  lv_obj_set_style_bg_opa(s_grpUngroup, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_grpUngroup, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  lv_obj_set_style_border_width(s_grpUngroup, 1, 0);
  lv_obj_set_style_border_color(s_grpUngroup, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_set_style_bg_color(s_grpUngroup, lv_color_hex(JB_ACCENT), LV_STATE_PRESSED);
  lv_obj_align(s_grpUngroup, LV_ALIGN_RIGHT_MID, -(16 + R_PLAY_SZ + 10), 0);
  lv_obj_add_event_cb(s_grpUngroup, ungroupCb, LV_EVENT_CLICKED, nullptr);
  s_grpUngroupLbl = label(s_grpUngroup, "UNGROUP", &lv_font_montserrat_12, JB_TEXT);
  lv_obj_center(s_grpUngroupLbl);

  s_grpPlay = lv_button_create(bar);
  lv_obj_remove_style_all(s_grpPlay);
  lv_obj_set_size(s_grpPlay, R_PLAY_SZ, R_PLAY_SZ);
  lv_obj_set_style_radius(s_grpPlay, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_opa(s_grpPlay, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_grpPlay, lv_color_hex(JB_ACCENT), 0);
  lv_obj_set_style_bg_color(s_grpPlay, lv_color_hex(JB_ACCENT_INK), LV_STATE_PRESSED);
  lv_obj_align(s_grpPlay, LV_ALIGN_RIGHT_MID, -16, 0);
  lv_obj_add_event_cb(s_grpPlay, groupPlayCb, LV_EVENT_CLICKED, nullptr);
  s_grpPlayLbl = label(s_grpPlay, LV_SYMBOL_PLAY, &lv_font_montserrat_20, JB_ACCENT_INK);
  lv_obj_center(s_grpPlayLbl);

  lv_obj_t *sec = label(pg, "ALL ROOMS" JB_SEP "TAP THE BOX TO GROUP", &lv_font_montserrat_12,
                        JB_TEXT_DIM);
  lv_obj_align(sec, LV_ALIGN_TOP_LEFT, 2, RM_LIST_Y - 24);

  s_roomsWrap = panel(pg, RM_W, SCREEN_H - RM_LIST_Y - 16, JB_SCREEN_BG, 0);
  lv_obj_align(s_roomsWrap, LV_ALIGN_TOP_LEFT, 0, RM_LIST_Y);
  lv_obj_add_flag(s_roomsWrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_roomsWrap, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_roomsWrap, LV_SCROLLBAR_MODE_AUTO);
}

// Favorites — the same navigation as Radio: a snapping carousel with artwork, an A-Z jump strip
// and global type-to-filter. One level rather than two, because favourites are a flat list.
//
// Backed by favcache (SD) rather than a live FV:2 browse, so entering the page is instant and works
// with a dead link. The difference from the station cache is the refresh policy: favourites are
// edited by the owner in the Sonos app and expected to appear straight away, so the page asks for a
// refresh on entry whenever the cache is more than a few minutes old, on top of the daily schedule
// and the manual button. See fav_cache.h.
static lv_obj_t *s_favList = nullptr, *s_favStatus = nullptr, *s_favAz = nullptr;
static lv_obj_t *s_favSearchTa = nullptr, *s_favKb = nullptr, *s_favBack = nullptr,
                *s_favTitle = nullptr, *s_favSearchBtn = nullptr;
static std::vector<favcache::Fav> s_favs;
static std::vector<lv_obj_t *>    s_favTiles;
static uint32_t s_favShownGen = UINT32_MAX;
static uint32_t s_favArtGen = 0;
static bool     s_favEntered = false;
static int      s_favSnapped = -1;
static bool     s_favSearching = false;
static String   s_favPending;
static uint32_t s_favSearchAt = 0;

static void favShowAll();
static void favPaintArt();

static void favLayout(bool withSearch, bool withAz) {
  const lv_coord_t w   = SCREEN_W - RAIL_W - PAD_X * 2;
  const lv_coord_t top = PAD_TOP + 106 + (withSearch ? 68 : 0);
  const lv_coord_t bot = PAD_BOT + (withAz ? 54 : 0);
  lv_obj_set_size(s_favList, w, SCREEN_H - top - bot);
  lv_obj_align(s_favList, LV_ALIGN_TOP_LEFT, 0, top);
}

static void favPlayCb(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= (int)s_favs.size()) return;
  uiSoundPlay(UiSound::Confirm);
  // The cached URI + DIDL are enough: processPending picks enqueue-vs-transport off the scheme,
  // so a playlist favourite and a track favourite both work without touching FV:2 again.
  if (stateLock()) {
    g_pending.playUri  = s_favs[i].uri;
    g_pending.playMeta = s_favs[i].meta;
    stateUnlock();
  }
}

static void favScrollCb(lv_event_t *e) {
  favPaintArt();
  lv_obj_t *list = (lv_obj_t *)lv_event_get_target(e);
  const int32_t mid = lv_obj_get_scroll_y(list) + lv_obj_get_height(list) / 2;
  int best = -1; int32_t bd = INT32_MAX;
  const uint32_t n = lv_obj_get_child_count(list);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *c = lv_obj_get_child(list, i);
    const int32_t cy = lv_obj_get_y(c) + lv_obj_get_height(c) / 2;
    const int32_t d = abs(cy - mid);
    if (d < bd) { bd = d; best = (int)i; }
  }
  if (best < 0 || best == s_favSnapped) return;
  s_favSnapped = best;
  static uint32_t lastTick = 0;
  if (settingsScrollSound() && millis() - lastTick >= 45) { lastTick = millis(); uiSoundPlay(UiSound::Tick); }
}

static void favPaintArt() {
  if (s_favTiles.empty()) return;
  const int32_t top = lv_obj_get_scroll_y(s_favList);
  const int32_t bot = top + lv_obj_get_height(s_favList);
  for (size_t i = 0; i < s_favTiles.size() && i < s_favs.size(); i++) {
    lv_obj_t *tile = s_favTiles[i];
    if (!tile) continue;
    const int32_t y = lv_obj_get_y(tile);
    if (y + 200 < top || y - 200 > bot) continue;
    if (s_favs[i].artUrl.isEmpty()) continue;
    const lv_image_dsc_t *d = artcache::get(artcache::keyOfUrl(s_favs[i].artUrl), s_favs[i].artUrl);
    if (d && lv_image_get_src(tile) != d) lv_image_set_src(tile, d);
  }
}

static void favRenderRows() {
  lv_obj_clean(s_favList);
  s_favTiles.clear();
  s_favSnapped = -1;
  const lv_coord_t w = SCREEN_W - RAIL_W - PAD_X * 2, h = 96;
  for (size_t i = 0; i < s_favs.size(); i++) {
    lv_obj_t *row = lv_button_create(s_favList);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, w, h);
    lv_obj_set_pos(row, 0, (lv_coord_t)(i * (h + 10)));
    lv_obj_set_style_radius(row, JB_R_LG, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(JB_SCREEN_ELEV), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(JB_SCREEN_ELEV_2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(row, favPlayCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *tile = panel(row, 72, 72, JB_SCREEN_ELEV_2, JB_R_MD);
    lv_obj_align(tile, LV_ALIGN_LEFT_MID, 12, 0);
    lv_obj_t *img = lv_image_create(tile);
    lv_obj_set_size(img, 72, 72);
    lv_obj_center(img);
    lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);
    s_favTiles.push_back(img);

    lv_obj_t *t = label(row, s_favs[i].title.c_str(), &lv_font_montserrat_22, JB_TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, w - 120);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 100, 0);
  }
  favPaintArt();
}

static void favAzJump(lv_event_t *e) {
  const char c = (char)(intptr_t)lv_event_get_user_data(e);
  for (size_t i = 0; i < s_favs.size(); i++) {
    char f = s_favs[i].title.length() ? s_favs[i].title[0] : 0;
    if (f >= 'a' && f <= 'z') f -= 32;
    if (f == c) { lv_obj_scroll_to_y(s_favList, (lv_coord_t)(i * 106), LV_ANIM_ON);
                  uiSoundPlay(UiSound::Tick); return; }
  }
}

static void favBuildAz() {
  lv_obj_clean(s_favAz);
  bool have[26] = {false};
  for (const auto &v : s_favs) {
    char f = v.title.length() ? v.title[0] : 0;
    if (f >= 'a' && f <= 'z') f -= 32;
    if (f >= 'A' && f <= 'Z') have[f - 'A'] = true;
  }
  const lv_coord_t step = (SCREEN_W - RAIL_W - PAD_X * 2) / 26;
  for (int i = 0; i < 26; i++) {
    lv_obj_t *b = lv_button_create(s_favAz);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, step, 44);
    lv_obj_set_pos(b, i * step, 0);
    char t[2] = {(char)('A' + i), 0};
    lv_obj_t *l = label(b, t, &lv_font_montserrat_16, have[i] ? JB_TEXT_MUTED : JB_SCREEN_LINE);
    lv_obj_center(l);
    if (have[i]) lv_obj_add_event_cb(b, favAzJump, LV_EVENT_CLICKED, (void *)(intptr_t)('A' + i));
  }
}

static void favShowAll() {
  s_favSearching = false;
  s_favs.clear();
  favcache::all(s_favs);
  lv_obj_add_flag(s_favSearchTa, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_favKb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_favBack, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_favTitle, "Favorites");
  favLayout(false, true);
  favRenderRows();
  favBuildAz();
  lv_obj_remove_flag(s_favAz, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
}

static void favRunSearch(const String &q) {
  s_favs.clear();
  lv_obj_add_flag(s_favAz, LV_OBJ_FLAG_HIDDEN);
  if (q.length() < 2) {
    lv_obj_clean(s_favList); s_favTiles.clear();
    lv_label_set_text(s_favStatus, "Type at least two letters.");
    lv_obj_remove_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  favcache::search(q, s_favs, 60);
  favLayout(true, false);
  if (s_favs.empty()) {
    lv_obj_clean(s_favList); s_favTiles.clear();
    lv_label_set_text(s_favStatus, "No favourites match.");
    lv_obj_remove_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
  favRenderRows();
}

static void favSearchCb(lv_event_t *) {
  s_favPending = String(lv_textarea_get_text(s_favSearchTa));
  s_favSearchAt = millis();
}
static void favSearchBtnCb(lv_event_t *) {
  uiSoundPlay(UiSound::Tick);
  s_favSearching = true;
  s_favs.clear();
  lv_obj_clean(s_favList); s_favTiles.clear();
  lv_obj_add_flag(s_favAz, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_favSearchTa, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_favKb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_favBack, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_favTitle, "Search favorites");
  favLayout(true, false);
  lv_keyboard_set_textarea(s_favKb, s_favSearchTa);
  lv_textarea_set_text(s_favSearchTa, "");
  lv_label_set_text(s_favStatus, "Type at least two letters.");
  lv_obj_remove_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
}
static void favBackCb(lv_event_t *) { uiSoundPlay(UiSound::Tick); favShowAll(); }

static void buildFavourites() {
  lv_obj_t *pg = s_page[PAGE_FAVORITES];

  s_favBack = lv_button_create(pg);
  lv_obj_remove_style_all(s_favBack);
  lv_obj_set_size(s_favBack, 52, 52);
  lv_obj_align(s_favBack, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 44);
  lv_obj_add_event_cb(s_favBack, favBackCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = label(s_favBack, LV_SYMBOL_LEFT, &lv_font_montserrat_24, JB_TEXT);
  lv_obj_center(bl);
  lv_obj_add_flag(s_favBack, LV_OBJ_FLAG_HIDDEN);

  s_favTitle = label(pg, "Favorites", &lv_font_montserrat_28, JB_TEXT);
  lv_obj_align(s_favTitle, LV_ALIGN_TOP_LEFT, 62, PAD_TOP + 52);

  s_favSearchBtn = lv_button_create(pg);
  lv_obj_remove_style_all(s_favSearchBtn);
  lv_obj_set_size(s_favSearchBtn, 56, 52);
  lv_obj_align(s_favSearchBtn, LV_ALIGN_TOP_RIGHT, 0, PAD_TOP + 44);
  lv_obj_set_style_radius(s_favSearchBtn, JB_R_MD, 0);
  lv_obj_set_style_bg_opa(s_favSearchBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_favSearchBtn, lv_color_hex(JB_SCREEN_ELEV), 0);
  lv_obj_add_event_cb(s_favSearchBtn, favSearchBtnCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sb = label(s_favSearchBtn, LV_SYMBOL_LIST, &lv_font_montserrat_20, JB_TEXT_MUTED);
  lv_obj_center(sb);

  s_favStatus = label(pg, "", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_obj_align(s_favStatus, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 176);

  s_favList = lv_obj_create(pg);
  lv_obj_remove_style_all(s_favList);
  lv_obj_set_style_bg_opa(s_favList, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(s_favList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_favList, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_set_scroll_snap_y(s_favList, LV_SCROLL_SNAP_CENTER);
  lv_obj_add_event_cb(s_favList, favScrollCb, LV_EVENT_SCROLL, nullptr);
  favLayout(false, true);

  s_favSearchTa = lv_textarea_create(pg);
  lv_textarea_set_one_line(s_favSearchTa, true);
  lv_textarea_set_placeholder_text(s_favSearchTa, "Favourite name");
  lv_obj_set_size(s_favSearchTa, SCREEN_W - RAIL_W - PAD_X * 2, 56);
  lv_obj_align(s_favSearchTa, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 106);
  lv_obj_set_style_bg_color(s_favSearchTa, lv_color_hex(JB_SCREEN_ELEV), 0);
  lv_obj_set_style_border_color(s_favSearchTa, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_set_style_text_color(s_favSearchTa, lv_color_hex(JB_TEXT), 0);
  lv_obj_set_style_text_font(s_favSearchTa, &lv_font_montserrat_22, 0);
  lv_obj_set_style_radius(s_favSearchTa, JB_R_MD, 0);
  lv_obj_add_event_cb(s_favSearchTa, favSearchCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_flag(s_favSearchTa, LV_OBJ_FLAG_HIDDEN);

  s_favAz = lv_obj_create(pg);
  lv_obj_remove_style_all(s_favAz);
  lv_obj_set_size(s_favAz, SCREEN_W - RAIL_W - PAD_X * 2, 44);
  lv_obj_align(s_favAz, LV_ALIGN_BOTTOM_LEFT, 0, -PAD_BOT);
  lv_obj_clear_flag(s_favAz, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_favAz, LV_OBJ_FLAG_HIDDEN);

  s_favKb = lv_keyboard_create(pg);
  lv_obj_set_size(s_favKb, SCREEN_W - RAIL_W - PAD_X * 2, 250);
  lv_obj_align(s_favKb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_add_flag(s_favKb, LV_OBJ_FLAG_HIDDEN);
}

// --- Settings -----------------------------------------------------------------------------------
static void soundCb(lv_event_t *e) {
  const int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  settingsSetUiSound((uint8_t)v);
  lv_label_set_text_fmt(s_soundVal, "%d", v);
  // Preview at the new level on release, so the slider is judged by ear rather than by number.
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) uiSoundPlay(UiSound::Tick);
}

static lv_obj_t *s_hourLbl = nullptr, *s_radioMeta = nullptr;
static lv_obj_t *s_amzStatus = nullptr, *s_amzBtn = nullptr, *s_amzBtnLbl = nullptr;
static lv_obj_t *s_linkPanel = nullptr, *s_linkQr = nullptr, *s_linkMsg = nullptr;

static void hourStep(int delta) {
  int h = (int)settingsRadioRefreshHour() + delta;
  if (h < 0) h = 23; if (h > 23) h = 0;
  settingsSetRadioRefreshHour((uint8_t)h);
  if (s_hourLbl) lv_label_set_text_fmt(s_hourLbl, "%02d:00", h);
  uiSoundPlay(UiSound::Tick);
}
static void hourDownCb(lv_event_t *) { hourStep(-1); }
static void hourUpCb(lv_event_t *)   { hourStep(+1); }
static void autoRefreshCb(lv_event_t *e) {
  const bool on = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
  settingsSetRadioAutoRefresh(on);
  uiSoundPlay(UiSound::Tick);
}
static void scrollSoundCb(lv_event_t *e) {
  settingsSetScrollSound(lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED));
  uiSoundPlay(UiSound::Tick);
}
// The ceremony overlay: a QR of the authorisation URL, because a ~250 character link cannot be
// read off a panel, let alone typed. Everything blocking happens on amazon's own task; this only
// starts it and reflects state.
static void linkCloseCb(lv_event_t *) {
  amazon::linkCancel();
  lv_obj_add_flag(s_linkPanel, LV_OBJ_FLAG_HIDDEN);
}
static void amzBtnCb(lv_event_t *) {
  if (amazon::linked()) {                 // second press unlinks
    amazon::unlink();
    uiSoundPlay(UiSound::Confirm);
    return;
  }
  uiSoundPlay(UiSound::Tick);
  amazon::linkStart();
  lv_obj_remove_flag(s_linkPanel, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_linkMsg, "Requesting a code from Amazon...");
}

static void refreshNowCb(lv_event_t *) {
  // Radio only. Favourites used to ride along here because the two shared one schedule; they now
  // have their own, and their own button below.
  uiSoundPlay(UiSound::Confirm);
  radiocache::requestRefresh();
}

// --- Favourites schedule (mirrors the radio controls above, and the web admin) ---
static lv_obj_t *s_favHourLbl = nullptr, *s_favMeta = nullptr;

static void favHourStep(int delta) {
  int h = (int)settingsFavRefreshHour() + delta;
  if (h < 0) h = 23; else if (h > 23) h = 0;
  settingsSetFavRefreshHour((uint8_t)h);
  if (s_favHourLbl) lv_label_set_text_fmt(s_favHourLbl, "%02d:00", h);
  uiSoundPlay(UiSound::Tick);
}
static void favHourDownCb(lv_event_t *) { favHourStep(-1); }
static void favHourUpCb(lv_event_t *)   { favHourStep(+1); }

static void favAutoCb(lv_event_t *e) {
  const bool on = lv_obj_has_state((lv_obj_t *)lv_event_get_target(e), LV_STATE_CHECKED);
  settingsSetFavAutoRefresh(on);
  uiSoundPlay(UiSound::Tick);
}

static void favRefreshNowCb(lv_event_t *) {
  uiSoundPlay(UiSound::Confirm);
  favcache::requestRefresh();
}

// Device name drives the DHCP hostname, the mDNS name and the OTA name, and all three are derived
// once at boot (wifiHostname()/otaHostname()). A reboot is therefore the honest way to apply it —
// which is exactly what g_pending.reboot exists for.
static void saveNameCb(lv_event_t *) {
  String n = String(lv_textarea_get_text(s_nameTa));
  n.trim();
  if (n.length() == 0) {
    uiSoundPlay(UiSound::Error);
    lv_label_set_text(s_saveHint, "Name cannot be empty");
    return;
  }
  uiSoundPlay(UiSound::Confirm);
  settingsSetDeviceName(n);
  lv_label_set_text(s_saveHint, "Saved " JB_DASH " restarting to apply...");
  if (stateLock()) { g_pending.reboot = true; stateUnlock(); }
}

static void kbShowCb(lv_event_t *e) {
  lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_kb, (lv_obj_t *)lv_event_get_target(e));
}
static void kbDoneCb(lv_event_t *) { lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN); }

// --- Radio (Amazon Prime Stations) ---
// Two levels over the SD cache: genres, then that genre's stations. Level 1 is a plain text grid
// because the service ships only two grey placeholder images for all 26 genres — tiles there would
// be 26 copies of the same square. Level 2 is the snapping carousel (option B in the design mock).
//
// Rows are built from a std::vector read off the card, and freed on the way out: at ~1 KB of LVGL
// pool per row plus a decoded image each, a 50-row list left resident would be the single largest
// thing on the heap. See the pool arithmetic in plans/08.-----------------------------------------------------------
// Placeholder until the SD cache and the station carousel land (plans/08 steps 4-5). It reports the
// real precondition rather than pretending: with no card there is nothing to browse, and saying so
// beats an empty list that looks broken.
static lv_obj_t *s_radioStatus = nullptr;
static lv_obj_t *s_radioList = nullptr;      // holds either the genre grid or the station carousel
static lv_obj_t *s_radioTitle = nullptr, *s_radioBack = nullptr;
static int  s_radioLevel = 0;                // 0 = genres, 1 = stations
static int  s_radioGenre = -1;               // which genre we descended into
static String s_radioGenreId, s_radioGenreName;
static std::vector<radiocache::Station> s_radioStations;
static uint32_t s_radioShownGen = UINT32_MAX;   // cache generation actually rendered
static int  s_radioSnapped = -1;                // centred row, for the detent cue
static std::vector<lv_obj_t *> s_radioTiles;    // per-row image widget, parallel to s_radioStations
static uint32_t s_artGen = 0;                   // last artcache generation we painted
static lv_obj_t *s_azStrip = nullptr;           // A-Z jump strip (level 2 only)
static lv_obj_t *s_searchBtn = nullptr, *s_searchTa = nullptr, *s_radioKb = nullptr;
static std::vector<radiocache::Hit> s_searchHits;
static String   s_searchPending;                // last text seen, debounced in uiTick
static uint32_t s_searchAt = 0;

static void radioShowGenres();
static void radioShowStations(int genreIdx);
static void radioPaintArt();
static void radioShowSearch();
static void radioRunSearch(const String &q);

// Tap a station: play it on the active room's coordinator, exactly as a favourite would be.
static void radioPlayCb(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= (int)s_radioStations.size()) return;
  uiSoundPlay(UiSound::Confirm);
  amazon::Station st;
  st.title = s_radioStations[i].title;
  st.id    = s_radioStations[i].id;
  if (stateLock()) {
    g_pending.playUri  = amazon::playUri(st);
    g_pending.playMeta = amazon::playMeta(st, s_radioGenreId);
    stateUnlock();
  }
}

static void radioGenreCb(lv_event_t *e) {
  uiSoundPlay(UiSound::Tick);
  radioShowStations((int)(intptr_t)lv_event_get_user_data(e));
}
static void radioBackCb(lv_event_t *) {
  uiSoundPlay(UiSound::Tick);
  radioShowGenres();
}

// Detents: fire when the CENTRED ROW CHANGES, never on scroll events — those arrive far more often
// than rows cross and would smear into noise. Rate-limited so a fast flick thins out rather than
// queueing; the cue queue drops when full, but relying on that would be accidental design.
static void radioScrollCb(lv_event_t *e) {
  lv_obj_t *list = (lv_obj_t *)lv_event_get_target(e);
  const int32_t mid = lv_obj_get_scroll_y(list) + lv_obj_get_height(list) / 2;
  int best = -1; int32_t bd = INT32_MAX;
  const uint32_t n = lv_obj_get_child_count(list);
  for (uint32_t i = 0; i < n; i++) {
    lv_obj_t *c = lv_obj_get_child(list, i);
    const int32_t cy = lv_obj_get_y(c) + lv_obj_get_height(c) / 2;
    const int32_t d  = abs(cy - mid);
    if (d < bd) { bd = d; best = (int)i; }
  }
  radioPaintArt();      // scrolling brings new rows into range; ask for their art
  if (best < 0 || best == s_radioSnapped) return;
  s_radioSnapped = best;
  static uint32_t lastTick = 0;
  if (settingsScrollSound() && millis() - lastTick >= 45) {
    lastTick = millis();
    uiSoundPlay(UiSound::Tick);
  }
}

// The list shares the page with a search field (above) and the A-Z strip (below), both of which
// appear only at certain levels. Geometry lives here so the three can never overlap — laying it out
// at each call site is exactly how a row ends up hidden behind the strip.
static void radioLayout(bool withSearch, bool withAz) {
  const lv_coord_t w   = SCREEN_W - RAIL_W - PAD_X * 2;
  const lv_coord_t top = PAD_TOP + 106 + (withSearch ? 68 : 0);
  const lv_coord_t bot = PAD_BOT + (withAz ? 54 : 0);
  lv_obj_set_size(s_radioList, w, SCREEN_H - top - bot);
  lv_obj_align(s_radioList, LV_ALIGN_TOP_LEFT, 0, top);
}

static void radioClear() {
  if (s_radioList) lv_obj_clean(s_radioList);
  s_radioStations.clear();
  s_searchHits.clear();
  s_radioTiles.clear();
  s_radioSnapped = -1;
}

// Fill in any tile whose artwork has finished decoding. Only rows near the viewport are asked for,
// so a 50-row genre queues a handful of fetches rather than fifty — the cache is bounded and would
// evict the early ones before they were ever seen anyway.
static void radioPaintArt() {
  if (s_radioLevel != 1 || s_radioTiles.empty()) return;
  const int32_t top = lv_obj_get_scroll_y(s_radioList);
  const int32_t bot = top + lv_obj_get_height(s_radioList);
  for (size_t i = 0; i < s_radioTiles.size(); i++) {
    lv_obj_t *tile = s_radioTiles[i];
    if (!tile) continue;
    const int32_t y = lv_obj_get_y(tile);
    if (y + 200 < top || y - 200 > bot) continue;      // a screen of headroom either way
    const String key = artcache::keyOf(s_radioStations[i].id);
    const lv_image_dsc_t *d = artcache::get(key, s_radioStations[i].artUrl);
    if (!d) continue;
    if (lv_image_get_src(tile) == d) continue;
    lv_image_set_src(tile, d);
  }
}

static void radioShowGenres() {
  radioClear();
  s_radioLevel = 0; s_radioGenre = -1;
  lv_obj_add_flag(s_azStrip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_searchTa, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_radioKb, LV_OBJ_FLAG_HIDDEN);
  radioLayout(false, false);
  lv_obj_add_flag(s_radioBack, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_radioTitle, "Radio");
  lv_obj_remove_flag(s_radioList, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_scroll_snap_y(s_radioList, LV_SCROLL_SNAP_NONE);

  const int n = radiocache::genreCount();
  const lv_coord_t w = (SCREEN_W - RAIL_W - PAD_X * 2 - 20) / 2, h = 64;
  for (int i = 0; i < n; i++) {
    String title, id;
    if (!radiocache::genre(i, title, id)) continue;
    lv_obj_t *b = lv_button_create(s_radioList);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, w, h);
    lv_obj_set_pos(b, (i % 2) * (w + 20), (i / 2) * (h + 10));
    lv_obj_set_style_radius(b, JB_R_MD, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(JB_SCREEN_ELEV), 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(JB_SCREEN_ELEV_2), LV_STATE_PRESSED);
    lv_obj_add_event_cb(b, radioGenreCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = label(b, title.c_str(), &lv_font_montserrat_22, JB_TEXT);
    lv_label_set_long_mode(l, LV_LABEL_LONG_DOT);
    lv_obj_set_width(l, w - 32);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 16, 0);
  }
}

// One carousel row. `artUrl` empty means "show art only if it is already decoded" — used by search
// results, whose flat index deliberately omits the art URL to keep all.tsv small.
static lv_obj_t *radioRow(size_t i, const String &title, const String &id, const String &artUrl,
                          lv_event_cb_t cb) {
  const lv_coord_t w = SCREEN_W - RAIL_W - PAD_X * 2, h = 96;
  lv_obj_t *row = lv_button_create(s_radioList);
  lv_obj_remove_style_all(row);
  lv_obj_set_size(row, w, h);
  lv_obj_set_pos(row, 0, (lv_coord_t)(i * (h + 10)));
  lv_obj_set_style_radius(row, JB_R_LG, 0);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(JB_SCREEN_ELEV), 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(JB_SCREEN_ELEV_2), LV_STATE_PRESSED);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

  lv_obj_t *tile = panel(row, 72, 72, JB_SCREEN_ELEV_2, JB_R_MD);
  lv_obj_align(tile, LV_ALIGN_LEFT_MID, 12, 0);
  lv_obj_t *img = lv_image_create(tile);
  lv_obj_set_size(img, 72, 72);
  lv_obj_center(img);
  lv_obj_add_flag(img, LV_OBJ_FLAG_IGNORE_LAYOUT);
  s_radioTiles.push_back(img);
  const lv_image_dsc_t *d = artcache::get(artcache::keyOf(id), artUrl);
  if (d) lv_image_set_src(img, d);

  lv_obj_t *t = label(row, title.c_str(), &lv_font_montserrat_22, JB_TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, w - 120);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 100, -12);
  lv_obj_t *sub = label(row, "Prime Station", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(sub, LV_ALIGN_LEFT_MID, 100, 14);
  return row;
}

// Play a search hit. Its parentID needs the genre's OBJECT id, which the flat index stores only as
// an index — so resolve it here rather than widening every row of all.tsv to carry it.
static void radioHitCb(lv_event_t *e) {
  const int i = (int)(intptr_t)lv_event_get_user_data(e);
  if (i < 0 || i >= (int)s_searchHits.size()) return;
  uiSoundPlay(UiSound::Confirm);
  String gname, gid;
  if (!radiocache::genre(s_searchHits[i].genreIdx, gname, gid)) return;
  amazon::Station st;
  st.title = s_searchHits[i].title;
  st.id    = s_searchHits[i].id;
  if (stateLock()) {
    g_pending.playUri  = amazon::playUri(st);
    g_pending.playMeta = amazon::playMeta(st, gid);
    stateUnlock();
  }
}

// --- A-Z jump strip -------------------------------------------------------------------------------
// Stations arrive already sorted by title (the crawler sorts, not the device), so the letter -> row
// mapping is a linear pass over data that is in RAM anyway. That is why there is no offsets file.
static void radioAzJump(lv_event_t *e) {
  const char c = (char)(intptr_t)lv_event_get_user_data(e);
  for (size_t i = 0; i < s_radioStations.size(); i++) {
    char f = s_radioStations[i].title.length() ? s_radioStations[i].title[0] : 0;
    if (f >= 'a' && f <= 'z') f -= 32;
    if (f == c) {
      lv_obj_scroll_to_y(s_radioList, (lv_coord_t)(i * 106), LV_ANIM_ON);
      uiSoundPlay(UiSound::Tick);
      return;
    }
  }
}

static void radioBuildAz() {
  lv_obj_clean(s_azStrip);
  bool have[26] = {false};
  for (const auto &st : s_radioStations) {
    char f = st.title.length() ? st.title[0] : 0;
    if (f >= 'a' && f <= 'z') f -= 32;
    if (f >= 'A' && f <= 'Z') have[f - 'A'] = true;
  }
  // 26 targets across the content width. Letters with no stations are dimmed and inert rather than
  // hidden, so the strip keeps a stable shape a thumb can learn.
  const lv_coord_t step = (SCREEN_W - RAIL_W - PAD_X * 2) / 26;
  for (int i = 0; i < 26; i++) {
    lv_obj_t *b = lv_button_create(s_azStrip);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, step, 44);
    lv_obj_set_pos(b, i * step, 0);
    char t[2] = {(char)('A' + i), 0};
    lv_obj_t *l = label(b, t, &lv_font_montserrat_16, have[i] ? JB_TEXT_MUTED : JB_SCREEN_LINE);
    lv_obj_center(l);
    if (have[i]) lv_obj_add_event_cb(b, radioAzJump, LV_EVENT_CLICKED, (void *)(intptr_t)('A' + i));
  }
}

static void radioShowStations(int genreIdx) {
  String gname, gid;
  if (!radiocache::genre(genreIdx, gname, gid)) return;
  radioClear();
  s_radioLevel = 1; s_radioGenre = genreIdx;
  s_radioGenreId = gid; s_radioGenreName = gname;
  radiocache::stations(genreIdx, s_radioStations);

  lv_obj_remove_flag(s_radioBack, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_radioTitle, gname.c_str());
  lv_obj_set_scroll_snap_y(s_radioList, LV_SCROLL_SNAP_CENTER);

  for (size_t i = 0; i < s_radioStations.size(); i++)
    radioRow(i, s_radioStations[i].title, s_radioStations[i].id, s_radioStations[i].artUrl,
             radioPlayCb);
  radioBuildAz();
  lv_obj_remove_flag(s_azStrip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_searchTa, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_flag(s_radioKb, LV_OBJ_FLAG_HIDDEN);
  radioLayout(false, true);
  radioPaintArt();
  lv_mem_monitor_t mon; lv_mem_monitor(&mon);
  LOG.printf("[ui    ] radio: %s, %u stations, lvgl_free=%uKB\n", gname.c_str(),
                (unsigned)s_radioStations.size(), (unsigned)(mon.free_size / 1024));
}

// Search is GLOBAL across all ~1,045 stations, not scoped to the current genre: someone hunting a
// specific station should not have to know which genre it was filed under.
static void radioRunSearch(const String &q) {
  radioClear();
  s_radioLevel = 2;
  lv_obj_add_flag(s_azStrip, LV_OBJ_FLAG_HIDDEN);
  if (q.length() < 2) {                       // one character matches most of the catalogue
    lv_label_set_text(s_radioStatus, "Type at least two letters.");
    lv_obj_remove_flag(s_radioStatus, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  // Capped: each row is ~1 KB of LVGL pool plus a tile, and nobody scrolls 500 results.
  const int n = radiocache::search(q, s_searchHits, 60);
  if (!n) {
    lv_label_set_text(s_radioStatus, "No stations match.");
    lv_obj_remove_flag(s_radioStatus, LV_OBJ_FLAG_HIDDEN);
    return;
  }
  lv_obj_add_flag(s_radioStatus, LV_OBJ_FLAG_HIDDEN);
  for (size_t i = 0; i < s_searchHits.size(); i++)
    radioRow(i, s_searchHits[i].title, s_searchHits[i].id, "", radioHitCb);
  // The status line sits where the first result now is, and the list needs the space the keyboard
  // was using — but the keyboard stays up, since refining the query is the common next action.
  radioLayout(true, false);
  LOG.printf("[ui    ] radio search \"%s\": %d hit(s)\n", q.c_str(), n);
}

static void radioSearchCb(lv_event_t *) {
  // Debounced in uiTick rather than acted on per keystroke: each run rebuilds up to 60 rows.
  s_searchPending = String(lv_textarea_get_text(s_searchTa));
  s_searchAt = millis();
}

static void radioShowSearch() {
  uiSoundPlay(UiSound::Tick);
  s_radioLevel = 2;
  radioClear();
  lv_obj_add_flag(s_azStrip, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_radioBack, LV_OBJ_FLAG_HIDDEN);
  lv_label_set_text(s_radioTitle, "Search");
  lv_obj_remove_flag(s_searchTa, LV_OBJ_FLAG_HIDDEN);
  lv_obj_remove_flag(s_radioKb, LV_OBJ_FLAG_HIDDEN);
  radioLayout(true, false);
  lv_keyboard_set_textarea(s_radioKb, s_searchTa);
  lv_textarea_set_text(s_searchTa, "");
  lv_label_set_text(s_radioStatus, "Type at least two letters.");
  lv_obj_remove_flag(s_radioStatus, LV_OBJ_FLAG_HIDDEN);
}
static void radioSearchBtnCb(lv_event_t *) { radioShowSearch(); }

static void buildRadio() {
  lv_obj_t *pg = s_page[PAGE_RADIO];

  s_radioBack = lv_button_create(pg);
  lv_obj_remove_style_all(s_radioBack);
  lv_obj_set_size(s_radioBack, 52, 52);
  lv_obj_align(s_radioBack, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 44);
  lv_obj_add_event_cb(s_radioBack, radioBackCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *bl = label(s_radioBack, LV_SYMBOL_LEFT, &lv_font_montserrat_24, JB_TEXT);
  lv_obj_center(bl);
  lv_obj_add_flag(s_radioBack, LV_OBJ_FLAG_HIDDEN);

  s_radioTitle = label(pg, "Radio", &lv_font_montserrat_28, JB_TEXT);
  lv_obj_align(s_radioTitle, LV_ALIGN_TOP_LEFT, 62, PAD_TOP + 52);

  s_radioStatus = label(pg, "", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_obj_align(s_radioStatus, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 176);

  s_radioList = lv_obj_create(pg);
  lv_obj_remove_style_all(s_radioList);
  lv_obj_set_size(s_radioList, SCREEN_W - RAIL_W - PAD_X * 2, SCREEN_H - (PAD_TOP + 110) - PAD_BOT);
  lv_obj_align(s_radioList, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 106);
  lv_obj_set_style_bg_opa(s_radioList, LV_OPA_TRANSP, 0);
  lv_obj_set_scroll_dir(s_radioList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_radioList, LV_SCROLLBAR_MODE_AUTO);
  lv_obj_add_event_cb(s_radioList, radioScrollCb, LV_EVENT_SCROLL, nullptr);
  lv_obj_add_flag(s_radioList, LV_OBJ_FLAG_HIDDEN);

  // Search entry point, top-right of the page.
  s_searchBtn = lv_button_create(pg);
  lv_obj_remove_style_all(s_searchBtn);
  lv_obj_set_size(s_searchBtn, 56, 52);
  lv_obj_align(s_searchBtn, LV_ALIGN_TOP_RIGHT, 0, PAD_TOP + 44);
  lv_obj_set_style_radius(s_searchBtn, JB_R_MD, 0);
  lv_obj_set_style_bg_opa(s_searchBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_searchBtn, lv_color_hex(JB_SCREEN_ELEV), 0);
  lv_obj_add_event_cb(s_searchBtn, radioSearchBtnCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sb = label(s_searchBtn, LV_SYMBOL_LIST, &lv_font_montserrat_20, JB_TEXT_MUTED);
  lv_obj_center(sb);

  s_searchTa = lv_textarea_create(pg);
  lv_textarea_set_one_line(s_searchTa, true);
  lv_textarea_set_placeholder_text(s_searchTa, "Station name");
  lv_obj_set_size(s_searchTa, SCREEN_W - RAIL_W - PAD_X * 2, 56);
  lv_obj_align(s_searchTa, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 106);
  lv_obj_set_style_bg_color(s_searchTa, lv_color_hex(JB_SCREEN_ELEV), 0);
  lv_obj_set_style_border_color(s_searchTa, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_set_style_text_color(s_searchTa, lv_color_hex(JB_TEXT), 0);
  lv_obj_set_style_text_font(s_searchTa, &lv_font_montserrat_22, 0);
  lv_obj_set_style_radius(s_searchTa, JB_R_MD, 0);
  lv_obj_add_event_cb(s_searchTa, radioSearchCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_flag(s_searchTa, LV_OBJ_FLAG_HIDDEN);

  // A-Z strip along the bottom. Horizontal, not the phone-style vertical rail: 26 letters down a
  // 450 px column is 17 px each, well under the design system's 44 px minimum touch target.
  s_azStrip = lv_obj_create(pg);
  lv_obj_remove_style_all(s_azStrip);
  lv_obj_set_size(s_azStrip, SCREEN_W - RAIL_W - PAD_X * 2, 44);
  lv_obj_align(s_azStrip, LV_ALIGN_BOTTOM_LEFT, 0, -PAD_BOT);
  lv_obj_clear_flag(s_azStrip, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(s_azStrip, LV_OBJ_FLAG_HIDDEN);

  // Keyboard last so it draws over the list.
  s_radioKb = lv_keyboard_create(pg);
  lv_obj_set_size(s_radioKb, SCREEN_W - RAIL_W - PAD_X * 2, 250);
  lv_obj_align(s_radioKb, LV_ALIGN_BOTTOM_LEFT, 0, 0);
  lv_obj_add_flag(s_radioKb, LV_OBJ_FLAG_HIDDEN);
}

// --- Screensaver controls (see the screensaver block further down for what they drive) ---------
// The two timers are PRESETS, not free numbers: "Never" has to be reachable without discovering
// that zero is special, and nobody wants to hold a +/- button up to thirty minutes. The same lists
// are in the web page, deliberately — one control that disagrees with the other about what the
// options are is how a setting ends up unreachable from one of them.
static const uint16_t kSaverDelays[] = {0, 30, 60, 120, 300, 600, 1800};
static const char kSaverDelayOpts[] = "Never\n30 seconds\n1 minute\n2 minutes\n5 minutes\n"
                                      "10 minutes\n30 minutes";
static const uint16_t kSaverBlanks[] = {0, 5, 15, 30, 60, 120, 480};
static const char kSaverBlankOpts[] = "Never\n5 minutes\n15 minutes\n30 minutes\n1 hour\n"
                                      "2 hours\n8 hours";
static lv_obj_t *s_ssDimVal = nullptr, *s_ssBrightVal = nullptr;

// Awake backlight. It has been in NVS and on the web page all along, but until the screensaver
// took ownership of the backlight nothing on this unit ever applied it — uiInit() set 100 % and
// left it there. It belongs beside the idle brightness: the two are read as a pair.
//
// Preview live, persist ONCE on release. Writing NVS per LV_EVENT_VALUE_CHANGED means a flash
// write for every pixel the finger moves — one drag across this slider is ~90 of them — and NVS
// is the same flash the Amazon tokens and Wi-Fi credentials live in. The preview still has to go
// through the screensaver rather than backlightSet(), or s_blPct goes stale and the next
// backlightTo() no-ops against a value the pin is not actually at.
static void ssBrightCb(lv_event_t *e) {
  const int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  lv_label_set_text_fmt(s_ssBrightVal, "%d", v);
  saverPreviewBrightness(v);
  if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;
  settingsSetBrightness((uint8_t)v);   // floors at 10, so this cannot blank the panel
  saverConfigChanged();
}

// Nearest stored value wins, so a value set from the web page that is not in this list (the API
// takes any number) still selects something sensible rather than silently showing the first entry.
static uint16_t nearestIdx(const uint16_t *opts, size_t n, uint16_t cur) {
  size_t best = 0;
  uint32_t bestD = 0xFFFFFFFF;
  for (size_t i = 0; i < n; i++) {
    const uint32_t d = (uint32_t)(opts[i] > cur ? opts[i] - cur : cur - opts[i]);
    if (d < bestD) { bestD = d; best = i; }
  }
  return (uint16_t)best;
}

static void ssModeCb(lv_event_t *e) {
  // Dropdown order is most-wanted-first, which is NOT the enum order — map explicitly rather than
  // letting the two drift apart.
  static const uint8_t kOrder[] = {SAVER_AUTO, SAVER_COVER, SAVER_CLOCK, SAVER_OFF};
  const uint32_t i = lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
  settingsSetSaverMode(kOrder[i < 4 ? i : 0]);
  saverConfigChanged();
  uiSoundPlay(UiSound::Tick);
}

static void ssDelayCb(lv_event_t *e) {
  const uint32_t i = lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
  settingsSetSaverDelaySec(kSaverDelays[i < 7 ? i : 3]);
  saverConfigChanged();
  uiSoundPlay(UiSound::Tick);
}

static void ssBlankCb(lv_event_t *e) {
  const uint32_t i = lv_dropdown_get_selected((lv_obj_t *)lv_event_get_target(e));
  settingsSetSaverBlankMin(kSaverBlanks[i < 7 ? i : 4]);
  saverConfigChanged();
  uiSoundPlay(UiSound::Tick);
}

static void ssDimCb(lv_event_t *e) {
  const int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  lv_label_set_text_fmt(s_ssDimVal, "%d", v);
  if (lv_event_get_code(e) != LV_EVENT_RELEASED) return;   // write NVS on release, not per pixel
  settingsSetSaverDimPct((uint8_t)v);
  saverConfigChanged();
}

// Shared styling. LVGL's default theme draws dropdowns in its own light palette, which would be
// the only thing on this device that is not the design system's dark one.
static lv_obj_t *ssDropdown(lv_obj_t *parent, const char *opts, uint16_t sel,
                            lv_event_cb_t cb, lv_coord_t x, lv_coord_t y, lv_coord_t w) {
  lv_obj_t *d = lv_dropdown_create(parent);
  lv_dropdown_set_options_static(d, opts);
  lv_dropdown_set_selected(d, sel);
  lv_obj_set_size(d, w, 52);
  lv_obj_align(d, LV_ALIGN_TOP_LEFT, x, y);
  lv_obj_set_style_radius(d, JB_R_MD, 0);
  lv_obj_set_style_bg_color(d, lv_color_hex(JB_SCREEN_ELEV), 0);
  lv_obj_set_style_border_color(d, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_set_style_text_color(d, lv_color_hex(JB_TEXT), 0);
  lv_obj_set_style_text_font(d, &lv_font_montserrat_16, 0);
  lv_obj_add_event_cb(d, cb, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *list = lv_dropdown_get_list(d);
  lv_obj_set_style_bg_color(list, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  lv_obj_set_style_border_color(list, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_set_style_text_color(list, lv_color_hex(JB_TEXT), 0);
  lv_obj_set_style_text_font(list, &lv_font_montserrat_16, 0);
  lv_obj_set_style_bg_color(list, lv_color_hex(JB_ACCENT), LV_PART_SELECTED | LV_STATE_CHECKED);
  return d;
}

static void buildSettings() {
  lv_obj_t *pg = s_page[PAGE_SETTINGS];
  // SCROLLABLE. panel() clears the flag, and this page was already ~570 px of content on a 600 px
  // panel — adding the Favourites block below Amazon would simply have been unreachable. Vertical
  // only, so a horizontal drag still can't move it off-axis.
  lv_obj_add_flag(pg, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(pg, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(pg, LV_SCROLLBAR_MODE_AUTO);

  lv_obj_t *h = label(pg, "Settings", &lv_font_montserrat_28, JB_TEXT);
  lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 56);

  // --- Device name ---
  lv_obj_t *nl = label(pg, "Device name", &lv_font_montserrat_16, JB_TEXT_MUTED);
  lv_obj_align(nl, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 112);

  s_nameTa = lv_textarea_create(pg);
  lv_textarea_set_one_line(s_nameTa, true);
  lv_textarea_set_max_length(s_nameTa, 31);   // DHCP hostnames are not unbounded
  lv_obj_set_size(s_nameTa, 420, 58);
  lv_obj_align(s_nameTa, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 140);
  lv_obj_set_style_bg_color(s_nameTa, lv_color_hex(JB_SCREEN_ELEV), 0);
  lv_obj_set_style_border_color(s_nameTa, lv_color_hex(JB_SCREEN_LINE), 0);
  lv_obj_set_style_text_color(s_nameTa, lv_color_hex(JB_TEXT), 0);
  lv_obj_set_style_text_font(s_nameTa, &lv_font_montserrat_22, 0);
  lv_obj_set_style_radius(s_nameTa, JB_R_MD, 0);
  lv_obj_add_event_cb(s_nameTa, kbShowCb, LV_EVENT_FOCUSED, nullptr);

  lv_obj_t *save = lv_button_create(pg);
  lv_obj_remove_style_all(save);
  lv_obj_set_size(save, 140, 58);
  lv_obj_align(save, LV_ALIGN_TOP_LEFT, 440, PAD_TOP + 140);
  lv_obj_set_style_radius(save, JB_R_MD, 0);
  lv_obj_set_style_bg_opa(save, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(save, lv_color_hex(JB_ACCENT), 0);
  lv_obj_add_event_cb(save, saveNameCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *sl = label(save, "Save", &lv_font_montserrat_22, JB_ACCENT_INK);
  lv_obj_center(sl);

  s_saveHint = label(pg, "", &lv_font_montserrat_16, JB_TEXT_DIM);
  lv_obj_align(s_saveHint, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 204);

  // --- On-device sound level ---
  lv_obj_t *vl = label(pg, "Sound feedback (this device's speakers)", &lv_font_montserrat_16,
                       JB_TEXT_MUTED);
  lv_obj_align(vl, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 250);

  s_soundSlider = lv_slider_create(pg);
  lv_obj_set_size(s_soundSlider, 480, 14);
  lv_obj_align(s_soundSlider, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 292);
  lv_slider_set_range(s_soundSlider, 0, 100);
  lv_slider_set_value(s_soundSlider, settingsUiSound(), LV_ANIM_OFF);
  lv_obj_set_style_bg_color(s_soundSlider, lv_color_hex(JB_SCREEN_ELEV_2), LV_PART_MAIN);
  lv_obj_set_style_bg_color(s_soundSlider, lv_color_hex(JB_ACCENT), LV_PART_INDICATOR);
  lv_obj_set_style_bg_color(s_soundSlider, lv_color_hex(JB_ACCENT), LV_PART_KNOB);
  lv_obj_add_event_cb(s_soundSlider, soundCb, LV_EVENT_VALUE_CHANGED, nullptr);
  lv_obj_add_event_cb(s_soundSlider, soundCb, LV_EVENT_RELEASED, nullptr);

  s_soundVal = label(pg, "", &lv_font_montserrat_22, JB_TEXT);
  lv_obj_align(s_soundVal, LV_ALIGN_TOP_LEFT, 500, PAD_TOP + 284);
  lv_label_set_text_fmt(s_soundVal, "%d", settingsUiSound());

  lv_obj_t *hint = label(pg, "0 turns feedback off.", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(hint, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 318);

  // --- Scroll feedback (separate from the master level above) ---
  lv_obj_t *ssl = label(pg, "Clicks while scrolling", &lv_font_montserrat_16, JB_TEXT_MUTED);
  lv_obj_align(ssl, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 352);
  lv_obj_t *ssw = lv_switch_create(pg);
  lv_obj_set_size(ssw, 72, 38);
  lv_obj_align(ssw, LV_ALIGN_TOP_LEFT, 300, PAD_TOP + 346);
  lv_obj_set_style_bg_color(ssw, lv_color_hex(JB_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (settingsScrollSound()) lv_obj_add_state(ssw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(ssw, scrollSoundCb, LV_EVENT_VALUE_CHANGED, nullptr);

  // --- Radio catalogue refresh ---
  lv_obj_t *rl = label(pg, "Refresh radio stations daily at", &lv_font_montserrat_16, JB_TEXT_MUTED);
  lv_obj_align(rl, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 410);

  lv_obj_t *rsw = lv_switch_create(pg);
  lv_obj_set_size(rsw, 72, 38);
  lv_obj_align(rsw, LV_ALIGN_TOP_LEFT, 300, PAD_TOP + 404);
  lv_obj_set_style_bg_color(rsw, lv_color_hex(JB_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
  if (settingsRadioAutoRefresh()) lv_obj_add_state(rsw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(rsw, autoRefreshCb, LV_EVENT_VALUE_CHANGED, nullptr);

  lv_obj_t *dn = transportBtn(pg, LV_SYMBOL_MINUS, 48, false, hourDownCb);
  lv_obj_align(dn, LV_ALIGN_TOP_LEFT, 400, PAD_TOP + 400);
  s_hourLbl = label(pg, "", &lv_font_montserrat_24, JB_TEXT);
  lv_obj_align(s_hourLbl, LV_ALIGN_TOP_LEFT, 462, PAD_TOP + 408);
  lv_label_set_text_fmt(s_hourLbl, "%02d:00", settingsRadioRefreshHour());
  lv_obj_t *up = transportBtn(pg, LV_SYMBOL_PLUS, 48, false, hourUpCb);
  lv_obj_align(up, LV_ALIGN_TOP_LEFT, 556, PAD_TOP + 400);

  // Local time, so the label means what it says wherever the device lives.
  lv_obj_t *rh = label(pg, "Device local time. ~500 KB once a day.", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(rh, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 438);

  lv_obj_t *rn = lv_button_create(pg);
  lv_obj_remove_style_all(rn);
  lv_obj_set_size(rn, 180, 52);
  lv_obj_align(rn, LV_ALIGN_TOP_LEFT, 624, PAD_TOP + 398);
  lv_obj_set_style_radius(rn, JB_R_MD, 0);
  lv_obj_set_style_bg_opa(rn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(rn, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  lv_obj_add_event_cb(rn, refreshNowCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *rnl = label(rn, "Refresh now", &lv_font_montserrat_16, JB_TEXT);
  lv_obj_center(rnl);

  s_radioMeta = label(pg, "", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_radioMeta, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 462);

  // --- Amazon Music account ---
  lv_obj_t *al = label(pg, "Amazon Music (for Radio stations)", &lv_font_montserrat_16, JB_TEXT_MUTED);
  lv_obj_align(al, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 500);
  s_amzStatus = label(pg, "", &lv_font_montserrat_16, JB_TEXT_DIM);
  lv_obj_align(s_amzStatus, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 528);

  // --- Favourites refresh (its own schedule; see settings.h for why it is not the radio one) ---
  {
    const lv_coord_t Y = PAD_TOP + 596;
    lv_obj_t *fl = label(pg, "Refresh favourites daily at", &lv_font_montserrat_16, JB_TEXT_MUTED);
    lv_obj_align(fl, LV_ALIGN_TOP_LEFT, 0, Y + 10);

    lv_obj_t *fsw = lv_switch_create(pg);
    lv_obj_set_size(fsw, 72, 38);
    lv_obj_align(fsw, LV_ALIGN_TOP_LEFT, 300, Y + 4);
    lv_obj_set_style_bg_color(fsw, lv_color_hex(JB_ACCENT), LV_PART_INDICATOR | LV_STATE_CHECKED);
    if (settingsFavAutoRefresh()) lv_obj_add_state(fsw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(fsw, favAutoCb, LV_EVENT_VALUE_CHANGED, nullptr);

    lv_obj_t *fdn = transportBtn(pg, LV_SYMBOL_MINUS, 48, false, favHourDownCb);
    lv_obj_align(fdn, LV_ALIGN_TOP_LEFT, 400, Y);
    s_favHourLbl = label(pg, "", &lv_font_montserrat_24, JB_TEXT);
    lv_obj_align(s_favHourLbl, LV_ALIGN_TOP_LEFT, 462, Y + 8);
    lv_label_set_text_fmt(s_favHourLbl, "%02d:00", settingsFavRefreshHour());
    lv_obj_t *fup = transportBtn(pg, LV_SYMBOL_PLUS, 48, false, favHourUpCb);
    lv_obj_align(fup, LV_ALIGN_TOP_LEFT, 556, Y);

    lv_obj_t *fn = lv_button_create(pg);
    lv_obj_remove_style_all(fn);
    lv_obj_set_size(fn, 180, 52);
    lv_obj_align(fn, LV_ALIGN_TOP_LEFT, 624, Y - 2);
    lv_obj_set_style_radius(fn, JB_R_MD, 0);
    lv_obj_set_style_bg_opa(fn, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(fn, lv_color_hex(JB_SCREEN_ELEV_2), 0);
    lv_obj_set_style_bg_color(fn, lv_color_hex(JB_ACCENT), LV_STATE_PRESSED);
    lv_obj_add_event_cb(fn, favRefreshNowCb, LV_EVENT_CLICKED, nullptr);
    lv_obj_t *fnl = label(fn, "Refresh now", &lv_font_montserrat_16, JB_TEXT);
    lv_obj_center(fnl);

    // Pick a different hour from the stations — running both at once is more internal SRAM than
    // this board has spare (see kMinHeap in fav_cache.cpp).
    lv_obj_t *fh = label(pg, "Keep this on a different hour from the stations.",
                         &lv_font_montserrat_12, JB_TEXT_DIM);
    lv_obj_align(fh, LV_ALIGN_TOP_LEFT, 0, Y + 38);

    s_favMeta = label(pg, "", &lv_font_montserrat_12, JB_TEXT_DIM);
    lv_obj_align(s_favMeta, LV_ALIGN_TOP_LEFT, 0, Y + 62);
  }

  // --- Screensaver ---
  // Last on the page on purpose: it is set once and then forgotten, unlike the room or the sound
  // level. The dropdowns are created before the keyboard below so the keyboard still draws over
  // them; an open dropdown list would otherwise sit under it.
  {
    const lv_coord_t Y = PAD_TOP + 700;
    lv_obj_t *sh = label(pg, "Screensaver", &lv_font_montserrat_22, JB_TEXT);
    lv_obj_align(sh, LV_ALIGN_TOP_LEFT, 0, Y);

    lv_obj_t *brl = label(pg, "Screen brightness", &lv_font_montserrat_16, JB_TEXT_MUTED);
    lv_obj_align(brl, LV_ALIGN_TOP_LEFT, 0, Y + 44);

    lv_obj_t *brs = lv_slider_create(pg);
    lv_obj_set_size(brs, 380, 14);
    lv_obj_align(brs, LV_ALIGN_TOP_LEFT, 300, Y + 54);
    lv_slider_set_range(brs, 10, 100);   // settingsSetBrightness floors at 10; match it here
    lv_slider_set_value(brs, settingsBrightness(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(brs, lv_color_hex(JB_SCREEN_ELEV_2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(brs, lv_color_hex(JB_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(brs, lv_color_hex(JB_ACCENT), LV_PART_KNOB);
    lv_obj_add_event_cb(brs, ssBrightCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(brs, ssBrightCb, LV_EVENT_RELEASED, nullptr);   // the one that writes NVS

    s_ssBrightVal = label(pg, "", &lv_font_montserrat_22, JB_TEXT);
    lv_obj_align(s_ssBrightVal, LV_ALIGN_TOP_LEFT, 700, Y + 46);
    lv_label_set_text_fmt(s_ssBrightVal, "%d", settingsBrightness());

    lv_obj_t *ml = label(pg, "Show when idle", &lv_font_montserrat_16, JB_TEXT_MUTED);
    lv_obj_align(ml, LV_ALIGN_TOP_LEFT, 0, Y + 108);
    // Same order as kOrder in ssModeCb — change both or neither.
    static const uint8_t kOrder[] = {SAVER_AUTO, SAVER_COVER, SAVER_CLOCK, SAVER_OFF};
    uint16_t modeSel = 0;
    for (uint16_t i = 0; i < 4; i++) if (kOrder[i] == settingsSaverMode()) modeSel = i;
    ssDropdown(pg, "Art when playing, else clock\nAlbum art\nClock\nNothing", modeSel,
               ssModeCb, 300, Y + 100, 420);

    lv_obj_t *dl = label(pg, "Appears after", &lv_font_montserrat_16, JB_TEXT_MUTED);
    lv_obj_align(dl, LV_ALIGN_TOP_LEFT, 0, Y + 172);
    ssDropdown(pg, kSaverDelayOpts,
               nearestIdx(kSaverDelays, 7, settingsSaverDelaySec()), ssDelayCb, 300, Y + 164, 200);

    lv_obj_t *bl = label(pg, "Screen off after", &lv_font_montserrat_16, JB_TEXT_MUTED);
    lv_obj_align(bl, LV_ALIGN_TOP_LEFT, 0, Y + 236);
    ssDropdown(pg, kSaverBlankOpts,
               nearestIdx(kSaverBlanks, 7, settingsSaverBlankMin()), ssBlankCb, 300, Y + 228, 200);

    lv_obj_t *il = label(pg, "Brightness while it is up", &lv_font_montserrat_16, JB_TEXT_MUTED);
    lv_obj_align(il, LV_ALIGN_TOP_LEFT, 0, Y + 300);

    lv_obj_t *sl = lv_slider_create(pg);
    lv_obj_set_size(sl, 380, 14);
    lv_obj_align(sl, LV_ALIGN_TOP_LEFT, 300, Y + 310);
    lv_slider_set_range(sl, 5, 100);      // 5, not 0 — 0 is what the screen-off timer is for
    lv_slider_set_value(sl, settingsSaverDimPct(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(sl, lv_color_hex(JB_SCREEN_ELEV_2), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, lv_color_hex(JB_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, lv_color_hex(JB_ACCENT), LV_PART_KNOB);
    lv_obj_add_event_cb(sl, ssDimCb, LV_EVENT_VALUE_CHANGED, nullptr);
    lv_obj_add_event_cb(sl, ssDimCb, LV_EVENT_RELEASED, nullptr);

    s_ssDimVal = label(pg, "", &lv_font_montserrat_22, JB_TEXT);
    lv_obj_align(s_ssDimVal, LV_ALIGN_TOP_LEFT, 700, Y + 302);
    lv_label_set_text_fmt(s_ssDimVal, "%d", settingsSaverDimPct());

    lv_obj_t *sn = label(pg, "A touch or a turn of the dial wakes it, even with the screen off.",
                         &lv_font_montserrat_12, JB_TEXT_DIM);
    lv_obj_align(sn, LV_ALIGN_TOP_LEFT, 0, Y + 350);
  }

  s_amzBtn = lv_button_create(pg);
  lv_obj_remove_style_all(s_amzBtn);
  lv_obj_set_size(s_amzBtn, 200, 52);
  lv_obj_align(s_amzBtn, LV_ALIGN_TOP_LEFT, 400, PAD_TOP + 496);
  lv_obj_set_style_radius(s_amzBtn, JB_R_MD, 0);
  lv_obj_set_style_bg_opa(s_amzBtn, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(s_amzBtn, lv_color_hex(JB_ACCENT), 0);
  lv_obj_add_event_cb(s_amzBtn, amzBtnCb, LV_EVENT_CLICKED, nullptr);
  s_amzBtnLbl = label(s_amzBtn, "Link account", &lv_font_montserrat_16, JB_ACCENT_INK);
  lv_obj_center(s_amzBtnLbl);

  // Link overlay, covering the content area so the QR is the only thing to look at.
  s_linkPanel = panel(pg, SCREEN_W - RAIL_W, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_align(s_linkPanel, LV_ALIGN_TOP_LEFT, -PAD_X, -PAD_TOP);
  lv_obj_add_flag(s_linkPanel, LV_OBJ_FLAG_HIDDEN);

  lv_obj_t *lt = label(s_linkPanel, "Link Amazon Music", &lv_font_montserrat_28, JB_TEXT);
  lv_obj_align(lt, LV_ALIGN_TOP_MID, 0, 40);

  s_linkQr = lv_qrcode_create(s_linkPanel);
  lv_qrcode_set_size(s_linkQr, 300);
  lv_qrcode_set_dark_color(s_linkQr, lv_color_hex(JB_SCREEN_BG));
  lv_qrcode_set_light_color(s_linkQr, lv_color_hex(JB_TEXT));
  lv_obj_align(s_linkQr, LV_ALIGN_CENTER, 0, -10);
  lv_obj_add_flag(s_linkQr, LV_OBJ_FLAG_HIDDEN);

  s_linkMsg = label(s_linkPanel, "", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_obj_align(s_linkMsg, LV_ALIGN_BOTTOM_MID, 0, -110);
  lv_obj_set_style_text_align(s_linkMsg, LV_TEXT_ALIGN_CENTER, 0);

  lv_obj_t *lc = lv_button_create(s_linkPanel);
  lv_obj_remove_style_all(lc);
  lv_obj_set_size(lc, 160, 52);
  lv_obj_align(lc, LV_ALIGN_BOTTOM_MID, 0, -40);
  lv_obj_set_style_radius(lc, JB_R_MD, 0);
  lv_obj_set_style_bg_opa(lc, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(lc, lv_color_hex(JB_SCREEN_ELEV_2), 0);
  lv_obj_add_event_cb(lc, linkCloseCb, LV_EVENT_CLICKED, nullptr);
  lv_obj_t *lcl = label(lc, "Close", &lv_font_montserrat_16, JB_TEXT);
  lv_obj_center(lcl);

  // Keyboard last so it draws above everything, hidden until the field is focused.
  s_kb = lv_keyboard_create(pg);
  lv_obj_set_size(s_kb, SCREEN_W - RAIL_W - PAD_X * 2, 240);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_kb, kbDoneCb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_kb, kbDoneCb, LV_EVENT_CANCEL, nullptr);
}

// --- Screensaver (plans/10) -------------------------------------------------------------------
// Three jobs, and only the first is about looks:
//   1. Show something worth looking at on a wall panel nobody is touching.
//   2. Stop any one high-contrast layout sitting under the backlight for hours. This is an IPS
//      LCD, so what it suffers is reversible image RETENTION, not OLED emitter wear — which is why
//      the two BACKLIGHT settings below matter more than the picture does.
//   3. Own the backlight. Before this, settingsBrightness() was written by the web page and never
//      applied on this unit; uiInit() just called backlightSet(100) and that was that.
//
// It lives entirely in the unit. Boards must not read settings (CLAUDE.md), and it must not go in
// core/ — `sonos-button` sweeps every core file into a build with no LVGL at all.
//
// ONE FULL-SCREEN OVERLAY on lv_layer_top(), created once and shown/hidden. It is clickable, so
// the tap that wakes the device is SWALLOWED rather than delivered to whatever button happened to
// be under the finger of someone reaching for a dark panel. That is also why the blank state shows
// it even with the screensaver set to Off.
namespace {

// The drifting group. Everything that could burn in lives inside this container and nothing else
// moves, so a drift step invalidates one rectangle rather than the whole screen — which matters,
// because repainting behind it means re-running the background image transform for that area.
const lv_coord_t SAVER_W = 760, SAVER_H = 440;
const lv_coord_t SAVER_DRIFT_X = 110, SAVER_DRIFT_Y = 70;
const lv_coord_t SAVER_TILE = 200;           // sharp cover tile inside the group

lv_obj_t *s_saver = nullptr, *s_saverBg = nullptr, *s_saverScrim = nullptr, *s_saverGrp = nullptr,
         *s_saverCard = nullptr, *s_saverCover = nullptr, *s_saverClock = nullptr,
         *s_saverAmPm = nullptr, *s_saverDate = nullptr, *s_saverTitle = nullptr,
         *s_saverMeta = nullptr;

enum class SaverState : uint8_t { Awake, Showing, Blank };
SaverState s_saverState = SaverState::Awake;
SaverState s_prevLogged = SaverState::Awake;   // separate from s_saverState so the first real
                                               // transition still logs (both start Awake)

// Cached config. Re-read only when the web page bumps webConfigGen() or the on-screen Settings
// page calls saverConfigChanged() — NVS reads on every 5 ms tick would be absurd.
uint8_t  s_cfgMode = SAVER_AUTO, s_cfgDim = 40, s_cfgBright = 100;
uint32_t s_cfgDelayMs = 120000, s_cfgBlankMs = 3600000;
uint32_t s_cfgGen = 0;
bool     s_cfgLoaded = false;

int      s_blPct     = -1;     // last value handed to backlightSet(); -1 = never set
int      s_saverLayout = -1;   // layout the tree is currently painted as: -1 none, 0 clock, 1 cover
int      s_shownMin   = -1;    // last minute rendered, so the clock repaints once a minute
uint16_t s_driftStep  = 0;
String   s_shownSaverTitle, s_shownSaverMeta;

void backlightTo(int pct) {
  if (pct == s_blPct) return;
  s_blPct = pct;
  backlightSet(pct);
}

void saverReadCfg() {
  s_cfgLoaded  = true;
  s_cfgGen     = webConfigGen();
  s_cfgMode    = settingsSaverMode();
  s_cfgDim     = settingsSaverDimPct();
  s_cfgBright  = settingsBrightness();
  s_cfgDelayMs = (uint32_t)settingsSaverDelaySec() * 1000u;
  s_cfgBlankMs = (uint32_t)settingsSaverBlankMin() * 60000u;
}

// Move the group. Called only when the minute changes, so the reposition and the clock repaint are
// ONE visible event rather than two — a screensaver that shuffles itself at some unrelated moment
// reads as a glitch. x and y run at incommensurate rates so the path does not retrace for hours.
void saverDrift() {
  if (!s_saverGrp) return;
  s_driftStep++;
  const float t = (float)s_driftStep * 0.10471976f;            // 2*pi / 60
  const lv_coord_t dx = (lv_coord_t)(sinf(t)          * (float)SAVER_DRIFT_X);
  const lv_coord_t dy = (lv_coord_t)(sinf(t * 1.618f) * (float)SAVER_DRIFT_Y);
  lv_obj_align(s_saverGrp, LV_ALIGN_CENTER, dx, dy);
}

// Cover layout vs clock layout. Only the vertical rhythm and three hidden flags differ, so this is
// one function rather than two trees — two trees would double the LVGL pool cost of a screen that
// exists to be idle.
void saverApplyLayout(bool cover) {
  if (s_saverLayout == (int)cover) return;
  s_saverLayout = (int)cover;

  auto show = [](lv_obj_t *o, bool on) {
    if (on) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
    else    lv_obj_add_flag(o, LV_OBJ_FLAG_HIDDEN);
  };
  show(s_saverBg,    cover);
  show(s_saverScrim, cover);
  show(s_saverCard,  cover);
  show(s_saverTitle, cover);
  show(s_saverMeta,  cover);

  // Clock-only centres the time/date pair in the group; the cover layout hangs everything off the
  // tile at the top.
  lv_obj_align(s_saverClock, LV_ALIGN_TOP_MID, 0, cover ? 232 : 150);
  lv_obj_align(s_saverDate,  LV_ALIGN_TOP_MID, 0, cover ? 330 : 248);

  // Force a clock repaint. AM/PM is hung off the TIME's right edge with lv_obj_align_to(), which
  // resolves to an absolute position ONCE — so moving the clock between layouts leaves the
  // meridiem behind at the old height. Repainting re-runs that align. (It also costs a drift step,
  // which is harmless: a layout change is already a visible event.)
  s_shownMin = -1;

  // This unit is on a wall with a power-only port, so the log mirror is the only way to see why it
  // chose what it chose. "Album art selected but a clock on the glass" has three different causes —
  // the mode did not persist, there is no decoded cover to show, or the images are drawing at the
  // wrong scale — and they are indistinguishable from across the room. One line per layout change,
  // which is at most a few an hour.
  LOG.printf("[saver ] layout=%s mode=%u art=%s %ldx%ld bgScale=%ld tileScale=%ld\n",
             cover ? "cover" : "clock", (unsigned)s_cfgMode, s_artDsc ? "yes" : "none",
             (long)(s_artDsc ? s_artDsc->header.w : 0), (long)(s_artDsc ? s_artDsc->header.h : 0),
             (long)lv_image_get_scale_x(s_saverBg), (long)lv_image_get_scale_x(s_saverCover));
}

void saverPaintClock() {
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);

  // Before NTP lands the epoch is 1970 and a confidently-wrong "4:00" is worse than an obvious
  // placeholder — which is the only reason '-' is in the 120 px font's range.
  if (lt.tm_year + 1900 < 2021) {
    lv_label_set_text(s_saverClock, "--:--");
    lv_label_set_text(s_saverDate,  "waiting for the network");
    lv_obj_add_flag(s_saverAmPm, LV_OBJ_FLAG_HIDDEN);
    return;
  }

  int h12 = lt.tm_hour % 12;
  if (h12 == 0) h12 = 12;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d:%02d", h12, lt.tm_min);
  lv_label_set_text(s_saverClock, buf);
  lv_label_set_text(s_saverAmPm, lt.tm_hour < 12 ? "AM" : "PM");
  lv_obj_remove_flag(s_saverAmPm, LV_OBJ_FLAG_HIDDEN);

  // Spelled out rather than strftime'd: "%-d" is a GNU extension newlib does not carry, and
  // zero-padding a day of the month ("August 03") reads as a filename, not a date.
  static const char *kDay[] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday",
                               "Saturday"};
  static const char *kMon[] = {"January", "February", "March", "April", "May", "June", "July",
                               "August", "September", "October", "November", "December"};
  snprintf(buf, sizeof(buf), "%d", lt.tm_mday);
  lv_label_set_text_fmt(s_saverDate, "%s, %s %s", kDay[lt.tm_wday % 7], kMon[lt.tm_mon % 12], buf);

  // The time label is centred, so its width changes between "9:05" and "12:05" and the meridiem
  // has to be re-hung off its right edge every minute.
  lv_obj_update_layout(s_saverGrp);
  lv_obj_align_to(s_saverAmPm, s_saverClock, LV_ALIGN_OUT_RIGHT_BOTTOM, 12, -18);
}

// Point both the wallpaper and the tile at the current cover.
//
// The scaling is LVGL's, not ours: LV_IMAGE_ALIGN_COVER fills the widget keeping aspect (the
// wallpaper) and LV_IMAGE_ALIGN_CONTAIN fits inside it (the tile), and BOTH recompute inside
// lv_image_set_src(). Setting the factor by hand with lv_image_set_scale() is the same arithmetic
// but has to be redone in the right order after every source change, which is one more thing to
// get wrong for no gain.
//
// From a <=280 px source the wallpaper is a ~3.7x upscale. That softness is deliberate — it is
// what makes it read as a wash rather than a stretched thumbnail, and it costs nothing, where a
// real blur would cost a second full-screen buffer.
void saverApplyArt() {
  if (!s_saverBg || !s_artDsc || !s_artDsc->header.w || !s_artDsc->header.h) return;
  lv_image_set_src(s_saverBg, s_artDsc);
  lv_image_set_src(s_saverCover, s_artDsc);
}

void saverBuild() {
  // On the top layer so it covers the rail and every page at once, and created AFTER the volume
  // toast so it draws over it. Clickable with no handler: the click is consumed here, and the
  // activity it registers with LVGL is what actually wakes the device.
  s_saver = panel(lv_layer_top(), SCREEN_W, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_align(s_saver, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_add_flag(s_saver, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(s_saver, LV_OBJ_FLAG_HIDDEN);

  s_saverBg = lv_image_create(s_saver);
  lv_obj_set_size(s_saverBg, SCREEN_W, SCREEN_H);
  lv_obj_align(s_saverBg, LV_ALIGN_CENTER, 0, 0);
  lv_image_set_inner_align(s_saverBg, LV_IMAGE_ALIGN_COVER);     // LVGL does the fill maths
  lv_obj_add_flag(s_saverBg, LV_OBJ_FLAG_HIDDEN);

  // Scrim. Does two jobs: it makes white text legible over any cover, and it drops the average
  // luminance of a screen that is about to sit lit for hours.
  //
  // 50 %, down from the 70 % this shipped with. At 70 %, on top of a screensaver backlight that
  // defaults to 40 %, the wallpaper was so dark it read as a plain clock on black — the feature
  // looked like it was not working at all. 120 px white type stays perfectly legible at 50 %.
  s_saverScrim = panel(s_saver, SCREEN_W, SCREEN_H, 0x000000, 0);
  lv_obj_align(s_saverScrim, LV_ALIGN_TOP_LEFT, 0, 0);
  lv_obj_set_style_bg_opa(s_saverScrim, LV_OPA_50, 0);
  lv_obj_add_flag(s_saverScrim, LV_OBJ_FLAG_HIDDEN);

  s_saverGrp = lv_obj_create(s_saver);
  lv_obj_remove_style_all(s_saverGrp);
  lv_obj_set_size(s_saverGrp, SAVER_W, SAVER_H);
  lv_obj_align(s_saverGrp, LV_ALIGN_CENTER, 0, 0);
  lv_obj_clear_flag(s_saverGrp, LV_OBJ_FLAG_SCROLLABLE);

  s_saverCard = panel(s_saverGrp, SAVER_TILE, SAVER_TILE, JB_SCREEN_ELEV, JB_R_LG);
  lv_obj_align(s_saverCard, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_border_width(s_saverCard, 1, 0);
  lv_obj_set_style_border_color(s_saverCard, lv_color_hex(JB_SCREEN_LINE), 0);
  s_saverCover = lv_image_create(s_saverCard);
  lv_obj_set_size(s_saverCover, SAVER_TILE, SAVER_TILE);
  lv_obj_center(s_saverCover);
  lv_image_set_inner_align(s_saverCover, LV_IMAGE_ALIGN_CONTAIN);  // ...and the fit maths

  s_saverClock = label(s_saverGrp, "--:--", &lv_font_clock_120, JB_TEXT);
  lv_obj_align(s_saverClock, LV_ALIGN_TOP_MID, 0, 232);

  s_saverAmPm = label(s_saverGrp, "", &lv_font_montserrat_28, JB_TEXT_MUTED);

  s_saverDate = label(s_saverGrp, "", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_obj_align(s_saverDate, LV_ALIGN_TOP_MID, 0, 330);

  // Both ellipsise on one line, for the reason the Now Playing labels do: a height-less LONG_DOT
  // label grows downwards instead of truncating, and here it would grow off the group.
  s_saverTitle = label(s_saverGrp, "", &lv_font_montserrat_28, JB_TEXT);
  lv_label_set_long_mode(s_saverTitle, LV_LABEL_LONG_DOT);
  lv_obj_set_size(s_saverTitle, SAVER_W, 34);
  lv_obj_set_style_text_align(s_saverTitle, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_saverTitle, LV_ALIGN_TOP_MID, 0, 372);

  s_saverMeta = label(s_saverGrp, "", &lv_font_montserrat_16, JB_TEXT_DIM);
  lv_label_set_long_mode(s_saverMeta, LV_LABEL_LONG_DOT);
  lv_obj_set_size(s_saverMeta, SAVER_W, 18);
  lv_obj_set_style_text_align(s_saverMeta, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_align(s_saverMeta, LV_ALIGN_TOP_MID, 0, 410);
}

}  // namespace

// Called from uiTick when albumArtTake() reports a change, and from the Settings page when the
// mode changes: both invalidate what the (possibly hidden) screensaver is pointing at.
static void saverArtChanged() {
  s_saverLayout = -1;          // force saverApplyLayout to re-evaluate cover-vs-clock
  if (s_artDsc) saverApplyArt();
}

// Tell the screensaver its settings moved. The on-screen Settings controls call this directly;
// the web page's changes arrive through webConfigGen().
static void saverConfigChanged() { s_cfgLoaded = false; }

// Light the panel at boot, at the persisted brightness.
//
// displayInit() leaves the backlight at 0 deliberately ("nothing to show yet"), and the first
// saverTick() does not run until appStartTasks() — on the far side of appBoot()'s Wi-Fi connect and
// SSDP discovery, which is many seconds. So SOMETHING has to turn the panel on in uiInit(), or the
// boot screen and uiProvisioning()'s "join <AP>" overlay are both invisible.
//
// It goes through the screensaver rather than calling backlightSet() directly so there is exactly
// one owner of that pin and s_blPct never goes stale. settingsInit() runs at main.cpp:76, BEFORE
// uiInit(), so the stored value really is readable here — this is not the 100 % placeholder it
// replaces. s_cfgLoaded is deliberately left false: the first saverTick() still does the full
// read, so this cannot pin a value that later config work would have changed.
static void saverBootBacklight() { backlightTo(settingsBrightness()); }

// Live preview while the awake-brightness slider is dragged, WITHOUT touching NVS — the write
// happens once on release (ssBrightCb). Same reason it is not a bare backlightSet(): the
// screensaver owns the pin, so a preview has to be routed through it too.
static void saverPreviewBrightness(int pct) { backlightTo(pct); }

// The state machine. Called once per uiTick with the frame's PlayerState snapshot.
//
// Idle is LVGL's own inactivity timer, which the touch indev already feeds. handleDial() calls
// lv_display_trigger_activity() so the dial feeds the SAME timer — one source of truth, rather
// than a second timestamp that could disagree with it.
static void saverTick(const PlayerState &p) {
  if (!s_saver) return;
  if (!s_cfgLoaded || s_cfgGen != webConfigGen()) saverReadCfg();

  const uint32_t idle = lv_display_get_inactive_time(nullptr);
  const bool wantBlank = s_cfgBlankMs && idle >= s_cfgBlankMs;
  const bool wantShow  = s_cfgMode != SAVER_OFF && s_cfgDelayMs && idle >= s_cfgDelayMs;

  const SaverState next = wantBlank ? SaverState::Blank
                        : wantShow  ? SaverState::Showing
                                    : SaverState::Awake;

  if (next != s_saverState) {
    s_saverState = next;
    if (next == SaverState::Awake) {
      lv_obj_add_flag(s_saver, LV_OBJ_FLAG_HIDDEN);
    } else {
      // Entering the overlay: repaint from scratch. s_shownMin is reset so the clock is right on
      // the first frame rather than a minute later.
      s_shownMin = -1;
      s_saverLayout = -1;
      lv_obj_remove_flag(s_saver, LV_OBJ_FLAG_HIDDEN);
    }
    // Blank still shows the overlay — see the note at the top: with the backlight at zero it is
    // invisible, but it is what stops a wake-up tap from pressing a button nobody can see. Strip it
    // to a bare dark rectangle, though: leaving the wallpaper in the tree means LVGL re-runs a
    // full-screen image transform to paint something with the backlight already at zero.
    if (next == SaverState::Blank) {
      lv_obj_add_flag(s_saverGrp,   LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_saverBg,    LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_saverScrim, LV_OBJ_FLAG_HIDDEN);
      s_saverLayout = -1;
    } else if (next == SaverState::Showing) {
      lv_obj_remove_flag(s_saverGrp, LV_OBJ_FLAG_HIDDEN);
    }
  }

  backlightTo(next == SaverState::Blank   ? 0
            : next == SaverState::Showing ? s_cfgDim
                                          : s_cfgBright);
  if (next != s_prevLogged) {
    s_prevLogged = next;
    LOG.printf("[saver ] state=%s idle=%lus backlight=%d%%\n",
               next == SaverState::Blank ? "blank" : next == SaverState::Showing ? "showing"
                                                                                 : "awake",
               (unsigned long)(idle / 1000), s_blPct);
  }

  if (next != SaverState::Showing) return;

  // AUTO falls back to the clock whenever there is nothing worth showing a cover for — paused
  // counts, because a paused track's art sitting on the wall all night is exactly the static image
  // this is meant to avoid.
  const bool playing = (p.transport == TransportState::Playing);
  const bool canCover = (s_artDsc != nullptr);
  const bool cover = canCover && (s_cfgMode == SAVER_COVER ||
                                  (s_cfgMode == SAVER_AUTO && playing));
  if (cover && s_saverLayout != 1) saverApplyArt();
  saverApplyLayout(cover);

  // One repaint a minute, and the drift rides along with it (see saverDrift).
  time_t t = time(nullptr);
  struct tm lt;
  localtime_r(&t, &lt);
  if (lt.tm_min != s_shownMin) {
    s_shownMin = lt.tm_min;
    saverPaintClock();
    saverDrift();
  }

  if (cover) {
    setTextIfChanged(s_saverTitle, s_shownSaverTitle,
                     p.title.length() ? p.title : String("Nothing playing"));
    String meta = p.artist;
    if (p.album.length()) { if (meta.length()) meta += JB_SEP; meta += p.album; }
    setTextIfChanged(s_saverMeta, s_shownSaverMeta, meta);
  }
}

void uiInit() {
  // Serial log -> TCP :2323, so the health heartbeat below is readable on a
  // wall-mounted unit without a cable. Starts a task that waits for WiFi itself.
  logMirrorBegin();

  lv_obj_t *scr = lv_screen_active();
  lv_obj_set_style_bg_color(scr, lv_color_hex(JB_SCREEN_BG), 0);
  lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  buildRail(scr);

  // Content area sits right of the rail, inset by the design's gutters.
  s_content = panel(scr, SCREEN_W - RAIL_W - PAD_X * 2, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_align(s_content, LV_ALIGN_TOP_LEFT, RAIL_W + PAD_X, 0);

  // Pages FIRST, status bar SECOND. LVGL paints siblings in creation order, so the shared chrome
  // has to be created after the pages or the pages' opaque backgrounds cover it — which is
  // exactly what hid the room name and left room switching with no visible feedback.
  const lv_coord_t pw = SCREEN_W - RAIL_W - PAD_X * 2;
  for (int i = 0; i < PAGE_COUNT; i++) {
    s_page[i] = panel(s_content, pw, SCREEN_H, JB_SCREEN_BG, 0);
    lv_obj_align(s_page[i], LV_ALIGN_TOP_LEFT, 0, 0);
  }

  buildStatusBar();     // shared chrome: on top of every page

  buildNowPlaying();
  buildTransport();
  buildFavourites();
  buildRadio();
  buildRooms();
  buildSettings();
  buildVolToast();   // top layer, hidden until the dial is turned off the Now Playing page
  saverBuild();      // top layer too, and AFTER the toast so it covers it
  // Background crawler: waits for card + Wi-Fi + a linked account, then keeps the cache fresh.
  // Started here rather than in appStartTasks() so the S3 units never spawn it.
  // 12 slots x 72x72 RGB565 = ~124 KB of PSRAM. Bounded on purpose: fifty decoded rows would be
  // ~506 KB, as much as the whole LVGL pool, which is what makes an unbounded cache unbuildable.
  artcache::init(72, 12);
  favcache::start();
  radiocache::start();

  showPage(PAGE_NOW);
  saverBootBacklight();   // NOT backlightSet() — the screensaver owns that pin. See its comment.
}

static void fmtTime(char *out, size_t n, uint32_t sec, bool negative) {
  snprintf(out, n, "%s%lu:%02lu", negative ? "-" : "", (unsigned long)(sec / 60),
           (unsigned long)(sec % 60));
}

// Drains the dial. Called from uiTick BEFORE anything renders, and it mutates the caller's
// PlayerState snapshot so a turn paints on the same frame it arrived on rather than a tick later.
// encoderDelta()/knobEvent() are cheap non-blocking reads of state the board's poll task keeps —
// no I2C happens on this task.
static void handleDial(PlayerState &p) {
  const int32_t   d  = encoderDelta();
  const KnobEvent ev = knobEvent();
  if (d == 0 && ev == KnobEvent::None) return;

  // The dial is not an LVGL input device — it is polled off I2C — so LVGL's inactivity timer would
  // never see it, and the screensaver would drop over a panel someone was actively turning. Feed
  // the same timer the touch indev feeds rather than keeping a second timestamp beside it.
  // Deliberately does NOT swallow the turn: reaching for the volume of a sleeping wall panel and
  // having the first turn do nothing but wake it is worse than having it do both.
  lv_display_trigger_activity(nullptr);

  if (d != 0) {
    // Acceleration: a slow hunt trims 1% per click, a fast spin crosses the range without needing
    // a dozen revolutions. A full 0-100 sweep is ~100 detents slow, ~25 at speed.
    //
    // *** DELIBERATELY GENTLER THAN THE NEST'S CURVE, AND IT MUST NOT BE "UNIFIED" WITH IT. ***
    // The nest reads an EC11 through hardware PCNT on the UI task's ~5 ms tick, so it essentially
    // always sees d == 1 and the multiplier IS the acceleration. This board reads a Modulino over
    // I2C at 50 Hz (kPollMs = 20) and encoderDelta() drains an ACCUMULATOR, so any spin faster than
    // 50 detents/s hands us d = 2, 3, 4... in one call — which the multiplier then compounds. The
    // nest's 6/3/2/1 curve therefore behaved completely differently here: at d=3 and dt<35 a single
    // tick moved 18%, which is what made the dial feel twitchy. Same numbers, different hardware,
    // different result.
    static uint32_t s_lastTurn = 0;
    const uint32_t now = lv_tick_get();
    const uint32_t dt  = now - s_lastTurn;
    s_lastTurn = now;
    const int mult = (dt < 35) ? 4 : (dt < 100) ? 2 : 1;

    // Cap the per-tick move. This is the half of the fix that addresses BATCHING rather than
    // speed: it bounds what one accumulated read can do without touching the feel of a slow,
    // precise turn, which never comes close to the limit.
    static const int kMaxStep = 8;
    int step = (int)d * mult;
    if (step >  kMaxStep) step =  kMaxStep;
    if (step < -kMaxStep) step = -kMaxStep;

    int v = (int)p.volume + step;
    v = v < 0 ? 0 : (v > 100 ? 100 : v);

    if (stateLock()) {
      // Optimistic. netTask confirms the real level on its ~1 Hz poll; waiting for that would make
      // the dial feel like it was dropping most of the turns.
      g_player.volume        = (uint8_t)v;
      g_player.volumeSetAtMs = millis();   // hold off the poll AND incoming GENA volume events
      g_pending.targetVolume = v;
      stateUnlock();
    }
    p.volume = (uint8_t)v;

    // Now Playing already carries a live volume bar — a toast over the top of it is just noise.
    if (s_cur != PAGE_NOW) showVolToast(v);

    // A detent is a scroll, so it obeys the scroll-sound toggle rather than the general UI level.
    // Rate-limited: at 6x acceleration the clicks would otherwise smear into one tone.
    static uint32_t s_lastClick = 0;
    if (settingsScrollSound() && (now - s_lastClick) >= 50) {
      s_lastClick = now;
      uiSoundPlay(UiSound::Tick);
    }
  }

  if (ev == KnobEvent::Short) {
    // The same decision playCb() makes, from the same source of truth, so the dial and the
    // on-screen button can never disagree about what a press means.
    uiSoundPlay(UiSound::Tick);
    bool wasPlaying = false;
    if (stateLock()) {
      wasPlaying = (g_player.transport == TransportState::Playing);
      g_pending.setPlay  = wasPlaying ? 0 : 1;
      g_player.transport = wasPlaying ? TransportState::Paused : TransportState::Playing;
      stateUnlock();
    }
    p.transport = wasPlaying ? TransportState::Paused : TransportState::Playing;
  }

  // KnobEvent::Long is reported by the board but deliberately left unbound. There is no obvious
  // second action for a press here, and picking one before the dial can be held in the hand would
  // be guesswork — see plans/07 for the list-scrolling idea that wants real hardware to judge.
}

void uiTick() {
  if (s_provisioning) {
    lv_obj_del(s_provisioning);
    s_provisioning = nullptr;
  }

  // Fill the name field from NVS once, on the first tick — uiInit() runs before appBoot(), so the
  // stored value is not necessarily readable yet.
  static bool nameLoaded = false;
  if (!nameLoaded && s_nameTa) {
    nameLoaded = true;
    String n = settingsDeviceName();
    lv_textarea_set_text(s_nameTa, n.length() ? n.c_str() : DEVICE_HOSTNAME);
  }

  // Snapshot under the mutex, render from the copy — never hold the lock across LVGL work.
  PlayerState p;
  if (stateLock()) {
    p = g_player;
    g_player.dirty = false;
    stateUnlock();
  }

  // The physical dial, before any rendering: it edits `p` so a turn lands on this frame.
  handleDial(p);
  if (s_volToast && !lv_obj_has_flag(s_volToast, LV_OBJ_FLAG_HIDDEN) &&
      (int32_t)(lv_tick_get() - s_volToastUntil) >= 0) {
    lv_obj_add_flag(s_volToast, LV_OBJ_FLAG_HIDDEN);
  }

  setTextIfChanged(s_room, s_shown.room, p.zoneName.length() ? p.zoneName : String("no room"));
  const String wantTitle = p.title.length() ? p.title : String("Nothing playing");
  if (s_shown.title != wantTitle) {
    setTextIfChanged(s_title, s_shown.title, wantTitle);
    placeTitle();
  }

  String meta = p.artist;
  if (p.album.length()) { if (meta.length()) meta += JB_SEP; meta += p.album; }
  setTextIfChanged(s_meta, s_shown.meta, meta);

  const bool playing = (p.transport == TransportState::Playing);
  s_wasPlaying = playing;
  if (s_shown.playing != (int)playing) {
    s_shown.playing = (int)playing;
    lv_label_set_text(s_playLbl, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);
    lv_obj_set_style_bg_color(s_dot, lv_color_hex(playing ? JB_ACCENT : JB_TEXT_DIM), 0);
    setTextIfChanged(s_badgeSrc, s_shown.badge, playing ? String("PLAYING") : String(""));
  }

  // Scrubber — only the fill width and the two timecodes, and only when they move.
  //
  // playerPositionNow(), NOT p.positionSec: position is the one now-playing field Sonos never
  // events, so with GENA carrying the state netTask only samples it every 15 s. Reading the raw
  // sample would step the bar and the timecode in 15 s jumps. This advances it locally between
  // samples and the poll reconciles it. Identical behaviour when eventing is off — the sample is
  // then a second old at most.
  // DURATION IS OFTEN 0, and not only for radio. Sonos reports TrackDuration 0:00:00 for anything
  // it treats as an open-ended stream — live radio, but ALSO a direct Spotify track, which has a
  // perfectly finite length Sonos simply does not tell us (no <res>, no duration attribute
  // anywhere in its metadata; checked). The old code computed pct=0 in that case, so the bar sat
  // empty at 0% while the elapsed timecode counted up beside it — visibly broken — and the
  // remaining timecode read a nonsense "-0:00".
  //
  // With no duration there is no honest percentage to draw, so don't draw one: the bar goes to a
  // dim full width (a track with no end, rather than a track that never starts) and the remaining
  // side shows an em dash instead of a fabricated number. The design system asks for "LIVE · on
  // air" here, which is right for radio but would be a lie on a Spotify track — and the two are
  // indistinguishable from this field alone.
  const uint32_t posSec = playerPositionNow(p);
  const bool unknownDur = (p.durationSec == 0);
  const lv_coord_t trackW = lv_obj_get_width(s_track);
  int pct = unknownDur ? 100 : (int)((uint64_t)posSec * 100 / p.durationSec);
  if (pct > 100) pct = 100;
  if (pct != s_shown.pct) {
    s_shown.pct = pct;
    lv_obj_set_width(s_fill, trackW * pct / 100);
  }
  const uint8_t fillOpa = unknownDur ? LV_OPA_40 : LV_OPA_COVER;
  if (fillOpa != s_shown.fillOpa) {
    s_shown.fillOpa = fillOpa;
    lv_obj_set_style_bg_opa(s_fill, fillOpa, 0);
  }

  char buf[16];
  if (posSec != s_shown.elapsed) {
    s_shown.elapsed = posSec;
    fmtTime(buf, sizeof(buf), posSec, false);
    lv_label_set_text(s_elapsed, buf);
  }
  const uint32_t remain = unknownDur ? UINT32_MAX
                                     : ((p.durationSec > posSec) ? p.durationSec - posSec : 0);
  if (remain != s_shown.remain) {
    s_shown.remain = remain;
    if (unknownDur) lv_label_set_text(s_remain, JB_DASH);
    else { fmtTime(buf, sizeof(buf), remain, true); lv_label_set_text(s_remain, buf); }
  }

  // Volume.
  const uint8_t vol = p.volume > 100 ? 100 : p.volume;
  if ((int)vol != s_shown.volPct) {
    s_shown.volPct = vol;
    lv_obj_set_width(s_volFill, 260 * vol / 100);
    snprintf(buf, sizeof(buf), "%u", (unsigned)vol);
    lv_label_set_text(s_volPct, buf);
  }
  const uint8_t icon = (p.muted || vol == 0) ? 0 : (vol < 50 ? 1 : 2);
  if (icon != s_shown.volIcon) {
    s_shown.volIcon = icon;
    lv_label_set_text(s_volIcon, icon == 0 ? LV_SYMBOL_MUTE
                                : (icon == 1 ? LV_SYMBOL_VOLUME_MID : LV_SYMBOL_VOLUME_MAX));
  }

  // Album art. albumArtTake() reports only on a CHANGE and hands over the decoded descriptor, so
  // this costs nothing on the vast majority of ticks. The art task owns fetching and decoding —
  // never do either on the UI task.
  const lv_image_dsc_t *dsc = nullptr;
  if (albumArtTake(&dsc)) {
    s_artDsc = dsc;      // remember it: the screensaver is a second consumer, and take() is once-only
    saverArtChanged();
    if (dsc) {
      lv_image_set_src(s_artImg, dsc);
      lv_obj_center(s_artImg);
      lv_obj_remove_flag(s_artImg, LV_OBJ_FLAG_HIDDEN);
      lv_obj_add_flag(s_artPh, LV_OBJ_FLAG_HIDDEN);
    } else {
      lv_obj_add_flag(s_artImg, LV_OBJ_FLAG_HIDDEN);
      lv_obj_remove_flag(s_artPh, LV_OBJ_FLAG_HIDDEN);
    }
  }

  // Rooms. Only while the page is visible — a hidden tree is pure churn on the LVGL pool, and the
  // per-room SOAP poll must not run for a page nobody is looking at.
  if (s_cur == PAGE_ROOMS) {
    // Tells netTask to keep polling per-room volume/transport. Lapses ~2 s after we stop asking,
    // so every exit path (rail tap, screensaver, OTA overlay) stops the traffic without needing
    // its own teardown call. See room_status.h.
    roomstatus::keepAlive();

    // FULL rebuild on a topology change or a zone switch (neither of which the status generation
    // covers): both change the row set or which row is the anchor.
    if (s_roomsGen != g_zonesGen || s_roomsActive != p.zoneName) {
      s_roomsGen = g_zonesGen;
      s_roomsActive = p.zoneName;
      s_roomsStatusGen = roomstatus::gen();
      rebuildRooms();
      // The active room's volume and transport are ALREADY known — netTask polls both every
      // second for Now Playing — so the summary bar and the MAIN row can be right on the FIRST
      // frame instead of reading "--" until their turn comes round.
      //
      // Seeded into s_roomsData directly, not via roomstatus::noteVolume(): on the first open the
      // poller has not run a tick yet, so its table is empty and a note would land nowhere. The
      // merge in refreshRooms() keeps a local reading that the poller does not have yet.
      const int a = activeRoomIdx();
      if (a >= 0) {
        s_roomsData[(size_t)a].vol = p.volume;
        s_roomsData[(size_t)a].volOk = true;
        s_roomsData[(size_t)a].transport = p.transport;
        s_roomsData[(size_t)a].transportOk = true;
        roomstatus::prioritise(s_roomsData[(size_t)a].ip);   // and read that room first
        refreshRooms();
      }
    } else if (s_roomsStatusGen != roomstatus::gen()) {
      // CHEAP refresh: new readings only. Text, colours and bar widths — no object churn.
      s_roomsStatusGen = roomstatus::gen();
      refreshRooms();
    }
  }

  if (s_cur == PAGE_SETTINGS && s_amzStatus) {
    static amazon::LinkState shownState = (amazon::LinkState)0xFF;
    static bool shownLinked = false;
    const bool nowLinked = amazon::linked();
    if (nowLinked != shownLinked) {
      shownLinked = nowLinked;
      lv_label_set_text(s_amzStatus, nowLinked ? "Account linked." : "Not linked.");
      lv_label_set_text(s_amzBtnLbl, nowLinked ? "Unlink" : "Link account");
    }
    const amazon::LinkState st = amazon::linkState();
    if (st != shownState) {
      shownState = st;
      switch (st) {
        case amazon::LinkState::Starting:
          lv_label_set_text(s_linkMsg, "Requesting a code from Amazon...");
          lv_obj_add_flag(s_linkQr, LV_OBJ_FLAG_HIDDEN);
          break;
        case amazon::LinkState::Waiting: {
          const String u = amazon::linkUrl();
          lv_qrcode_update(s_linkQr, u.c_str(), u.length());
          lv_obj_remove_flag(s_linkQr, LV_OBJ_FLAG_HIDDEN);
          lv_label_set_text(s_linkMsg, "Scan with a phone, then approve in Amazon.");
          break;
        }
        case amazon::LinkState::Linked:
          lv_obj_add_flag(s_linkQr, LV_OBJ_FLAG_HIDDEN);
          lv_label_set_text(s_linkMsg, "Linked. Building the station list...");
          radiocache::requestRefresh();
          break;
        case amazon::LinkState::Failed:
          lv_obj_add_flag(s_linkQr, LV_OBJ_FLAG_HIDDEN);
          lv_label_set_text(s_linkMsg, "Linking failed or timed out.\nClose and try again.");
          break;
        default: break;
      }
    }
    // The countdown is the only thing that changes second to second while waiting.
    if (st == amazon::LinkState::Waiting) {
      static uint16_t shownLeft = 0;
      const uint16_t left = amazon::linkSecondsLeft();
      if (left / 10 != shownLeft / 10) {
        shownLeft = left;
        lv_label_set_text_fmt(s_linkMsg, "Scan with a phone, then approve in Amazon.\n%u:%02u left",
                              left / 60, left % 60);
      }
    }
  }

  if (s_cur == PAGE_SETTINGS && s_radioMeta) {
    static String shownMeta;
    String m;
    if (radiocache::busy() || favcache::busy()) m = "Refreshing now...";
    else if (radiocache::ready())  m = String(radiocache::genreCount()) + " genres, " +
                                       String(favcache::count()) + " favorites cached.";
    else                           m = "No station cache yet.";
    if (m != shownMeta) { shownMeta = m; lv_label_set_text(s_radioMeta, m.c_str()); }
  }

  // Radio: until the carousel lands, report exactly which precondition is unmet. Each of these is
  // a different user action (insert a card / join Wi-Fi / link an account / wait), so a single
  // "unavailable" would be useless.
  if (s_cur == PAGE_RADIO && s_radioStatus) {
    static String shownRadio;
    String msg;
    if (!localStorageRoot())            msg = "No SD card.\nInsert one to cache the station list.";
    else if (!amazon::linked())         msg = "Amazon Music not linked.\nLink it in Settings to browse stations.";
    else if (radiocache::busy())        msg = "Building the station cache...";
    else if (!radiocache::ready())      msg = "Station cache not built yet.\nIt builds automatically once linked.";
    else msg = "";
    if (msg != shownRadio) { shownRadio = msg; lv_label_set_text(s_radioStatus, msg.c_str()); }
    if (msg.isEmpty()) lv_obj_add_flag(s_radioStatus, LV_OBJ_FLAG_HIDDEN);
    else               lv_obj_remove_flag(s_radioStatus, LV_OBJ_FLAG_HIDDEN);

    // Debounced search: rebuilding up to 60 rows per keystroke would stutter badly.
    if (s_searchAt && millis() - s_searchAt > 220) {
      s_searchAt = 0;
      radioRunSearch(s_searchPending);
    }

    if (s_radioLevel != 0 && artcache::generation() != s_artGen) {
      s_artGen = artcache::generation();
      radioPaintArt();
    }

    // Populate once the cache exists, and repopulate after a refresh replaces it.
    const uint32_t gen = radiocache::ready() ? (uint32_t)radiocache::fetchedAt() : 0;
    if (gen && gen != s_radioShownGen && !radiocache::busy()) {
      s_radioShownGen = gen;
      radioShowGenres();
    }
  }

  if (s_cur == PAGE_FAVORITES) {
    // Refresh on entry when the cache has gone stale. This is the one behaviour that differs from
    // Radio: favourites are edited by the owner in the Sonos app and are expected to show up here
    // without waiting for the nightly slot or pressing anything.
    static bool askedThisVisit = false;
    if (!s_favEntered) { s_favEntered = true; askedThisVisit = false; }
    if (!askedThisVisit && favcache::stale() && !favcache::busy()) {
      askedThisVisit = true;
      favcache::requestRefresh();
    }
    // Repaint when the cache is replaced, or when artwork finishes decoding.
    const uint32_t gen = favcache::ready() ? (uint32_t)favcache::fetchedAt() : 0;
    if (gen && gen != s_favShownGen && !favcache::busy() && !s_favSearching) {
      s_favShownGen = gen;
      favShowAll();
    }
    if (!s_favs.empty() && artcache::generation() != s_favArtGen) {
      s_favArtGen = artcache::generation();
      favPaintArt();
    }
    if (s_favSearchAt && millis() - s_favSearchAt > 220) {
      s_favSearchAt = 0;
      favRunSearch(s_favPending);
    }
    if (!favcache::ready()) {
      lv_obj_remove_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
      lv_label_set_text(s_favStatus, favcache::busy() ? "Loading favourites..."
                                                      : "No favourites cached yet.");
    }
  } else {
    s_favEntered = false;
  }

  // Health heartbeat. This unit lost the network after a few minutes and the failure looked like
  // "connection refused" from Sonos — which on the S3 units means internal SRAM exhaustion, but
  // here could equally be the ESP-Hosted/C6 link degrading (a known, unresolved upstream issue on
  // P4 boards). Logging heap AND link state together is what distinguishes the two.
  {
    static uint32_t lastHealth = 0;
    if (millis() - lastHealth >= 10000) {
      lastHealth = millis();
      lv_mem_monitor_t mon;
      lv_mem_monitor(&mon);   // LVGL pool: exhaustion here freezes the UI, not the network
      // Publish the same sample to the health JSON, so the pool is readable over HTTP and not only
      // off a serial cable. mon.max_used is LVGL's own running peak, so this 10 s cadence loses
      // nothing that matters — it is the number to size LV_MEM_SIZE against, and the one that would
      // have named the 70-row Favourites freeze immediately. Deliberately reusing this monitor call
      // rather than adding a 2 s sampler like the nest: lv_mem_monitor walks the whole free list,
      // and this pool is 512 KB against the nest's 96 KB.
      webConfigReportLvMem((uint32_t)(mon.total_size - mon.free_size), (uint32_t)mon.max_used,
                           mon.frag_pct);
      // Every link field is netTask's published snapshot (g_link*, core/app.h) — reading them
      // here is a plain memory load. Calling WiFi.RSSI()/localIP()/sonos::zones() directly would
      // be a blocking co-processor RPC and an unlocked read of a vector netTask rewrites, i.e.
      // this log would stall or crash the UI task in exactly the fault it exists to report.
      const uint32_t ip = g_linkIp;
      LOG.printf("[health] up=%lus heap=%luKB min=%luKB psram=%luKB wifi=%d rssi=%d "
                    "ip=%u.%u.%u.%u zones=%u lvgl_free=%uKB log=%d/%lu\n",
                    (unsigned long)(millis() / 1000),
                    (unsigned long)(ESP.getFreeHeap() / 1024),
                    (unsigned long)(ESP.getMinFreeHeap() / 1024),
                    (unsigned long)(ESP.getFreePsram() / 1024),
                    g_linkStatus, g_linkRssi,
                    (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                    (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF),
                    (unsigned)g_linkZones, (unsigned)(mon.free_size / 1024),
                    logMirrorClients(), (unsigned long)logMirrorDropped());
    }
  }

  // Screensaver LAST, after every page has updated: it owns the backlight and the top-layer
  // overlay, and running it before the pages would let a page repaint under a screen that is
  // already meant to be dark.
  saverTick(p);

  bringupConsoleTick();   // bring-up only; compiles to nothing without the flag

  lv_timer_handler();
}

void uiProvisioning(const char *apSsid) {
  s_provisioning = panel(lv_screen_active(), SCREEN_W, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_t *l = label(s_provisioning, "", &lv_font_montserrat_28, JB_TEXT);
  lv_label_set_text_fmt(l, "Join \"%s\"\non your phone to set up Wi-Fi", apSsid);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);

  // Full brightness — you have to read an AP name off it — but through the screensaver, not
  // backlightSet(). A bare call here leaves s_blPct saying "70 %" while the pin is at 100 %, and
  // the first saverTick() would then no-op against its own stale bookkeeping and leave the panel
  // stuck bright. Provisioning runs inside appBoot(), before saverTick() has ever run.
  saverPreviewBrightness(100);
  lv_timer_handler();   // the UI task is not running yet — flush synchronously
}
