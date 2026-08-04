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
#include <lvgl.h>

#include <vector>

#include "core/album_art.h"
#include "core/board.h"
#include "core/library.h"
#include "core/settings.h"
#include "core/player_state.h"
#include "core/app.h"          // g_link* — netTask's link snapshot. NEVER call WiFi.* from here:
                              // on this board the radio is a co-processor and those are blocking
                              // RPCs, so a UI-task caller freezes rendering when the link dies.
#include "core/sonos/ssdp.h"
#include "core/unit.h"
#include "ui_scale.h"

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

// Radio (Sonos Favourites, FV:2)
static lv_obj_t *s_favList = nullptr, *s_favStatus = nullptr;
static bool s_favRequested = false;    // a browse has been asked for since entering the page
static bool s_favRendered  = false;    // the rows on screen are current (cleared on page exit)

// Favourites cache. Browsing FV:2 is not cheap — library::collectRows() paginates PAGE=16 up to
// MAX_ROWS=200, i.e. up to 13 SOAP round-trips — and without this, every single tap of the Radio
// rail icon re-ran the whole browse, because leaving the page resets s_favRequested. Radio ->
// Now Playing -> Radio three times was ~40 requests in a couple of seconds, over the same
// ESP-Hosted SDIO bridge that is documented as failing under sustained load. Same reasoning as
// kArtSettleMs in album_art.cpp: on this board, traffic we can avoid is traffic we should avoid.
//
// The labels live on the heap, NOT in the LVGL pool — caching them costs a few KB and is unrelated
// to the row-count budget below. Favourites change rarely; a minute of staleness is invisible.
static std::vector<String> s_favLabels;
static uint32_t s_favFetchedMs = 0;
static const uint32_t kFavCacheMs = 60000;

// Settings
static lv_obj_t *s_nameTa = nullptr, *s_kb = nullptr, *s_soundSlider = nullptr,
                *s_soundVal = nullptr, *s_saveHint = nullptr;

// Rooms
static lv_obj_t *s_roomsWrap = nullptr;
static std::vector<String> s_roomIps;      // parallel to the chips
static uint32_t s_roomsGen = UINT32_MAX;   // last-rendered g_zonesGen
static String   s_roomsActive;             // last-rendered active zone (the amber outline)

// Status bar
static lv_obj_t *s_dot = nullptr, *s_room = nullptr, *s_group = nullptr, *s_clock = nullptr;
// Now playing
static lv_obj_t *s_art = nullptr, *s_artImg = nullptr, *s_artPh = nullptr, *s_badgeSrc = nullptr, *s_title = nullptr, *s_meta = nullptr;
static lv_obj_t *s_elapsed = nullptr, *s_remain = nullptr, *s_track = nullptr, *s_fill = nullptr;
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
  String   room, title, meta, badge;
  int      pct = -1, volPct = -1;
  uint32_t elapsed = UINT32_MAX, remain = UINT32_MAX;
  int      playing = -1;      // tri-state so the first tick always paints
  uint8_t  volIcon = 0xFF;
};
static Shown s_shown;

static inline void setTextIfChanged(lv_obj_t *l, String &cache, const String &next) {
  if (cache == next) return;
  cache = next;
  lv_label_set_text(l, next.c_str());
}

// --- Commands ---------------------------------------------------------------------------------
static void prevCb(lv_event_t *) {
  uiSoundPlay(UiSound::Tick);
  if (stateLock()) { g_pending.prev = true; stateUnlock(); }
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

  // Now / Favorites / Radio / Rooms / Settings.
  //
  // TODO(icons): these are stand-ins. The design system specifies Lucide, and LVGL's built-in
  // symbol font has NEITHER a heart (Favorites) NOR a radio glyph — the two this rail most needs.
  // The fix is converting a two-glyph Lucide subset into an LVGL font, scoped to UNIT_JUKEBOX the
  // same way the Montserrat sizes already are. Until then: PLAY for Now Playing, LIST for
  // Favorites, AUDIO for Radio — distinct from each other, but none of them right.
  const char *icons[PAGE_COUNT] = {LV_SYMBOL_PLAY, LV_SYMBOL_LIST, LV_SYMBOL_AUDIO,
                                   LV_SYMBOL_VOLUME_MAX, LV_SYMBOL_SETTINGS};
  for (int i = 0; i < PAGE_COUNT; i++) {
    lv_obj_t *b = lv_button_create(scr);
    lv_obj_remove_style_all(b);
    lv_obj_set_size(b, RAIL_BTN, RAIL_BTN);
    lv_obj_set_style_radius(b, JB_R_LG, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(JB_SCREEN_BG), 0);
    lv_obj_align(b, LV_ALIGN_TOP_LEFT, (RAIL_W - RAIL_BTN) / 2, PAD_TOP + i * RAIL_STEP);
    lv_obj_add_event_cb(b, railCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);
    lv_obj_t *l = label(b, icons[i], &lv_font_montserrat_28, JB_TEXT_DIM);
    lv_obj_center(l);
    s_railBtn[i] = b;
    s_railIcon[i] = l;
  }
}

static void buildStatusBar() {
  s_dot = panel(s_content, 9, 9, JB_ACCENT, LV_RADIUS_CIRCLE);
  lv_obj_align(s_dot, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 6);

  s_room = label(s_content, "—", &lv_font_montserrat_16, JB_TEXT);
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
  lv_obj_align(s_art, LV_ALIGN_LEFT_MID, 0, 0);
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

  s_badgeSrc = label(s_page[PAGE_NOW], "", &lv_font_montserrat_12, JB_ACCENT);
  lv_obj_align(s_badgeSrc, LV_ALIGN_LEFT_MID, textX, -118);

  s_title = label(s_page[PAGE_NOW], "Sonos Jukebox", &lv_font_montserrat_48, JB_TEXT);
  lv_label_set_long_mode(s_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_title, textW);
  lv_obj_align(s_title, LV_ALIGN_LEFT_MID, textX, -64);

  s_meta = label(s_page[PAGE_NOW], "starting up", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_label_set_long_mode(s_meta, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_meta, textW);
  lv_obj_align(s_meta, LV_ALIGN_LEFT_MID, textX, -8);

  // Scrubber: 6px track, accent fill, mono-ish timecodes beneath.
  s_track = panel(s_page[PAGE_NOW], textW, 6, JB_SCREEN_ELEV_2, 3);
  lv_obj_align(s_track, LV_ALIGN_LEFT_MID, textX, 48);
  s_fill = panel(s_track, 0, 6, JB_ACCENT, 3);
  lv_obj_align(s_fill, LV_ALIGN_LEFT_MID, 0, 0);

  s_elapsed = label(s_page[PAGE_NOW], "0:00", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_elapsed, LV_ALIGN_LEFT_MID, textX, 68);
  s_remain = label(s_page[PAGE_NOW], "-0:00", &lv_font_montserrat_12, JB_TEXT_DIM);
  lv_obj_align(s_remain, LV_ALIGN_LEFT_MID, textX + textW - 44, 68);
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

  // Leaving Radio: free the rows AND the cached browse results. CLAUDE.md is explicit that a long
  // browse list can fill LV_MEM_SIZE and freeze the UI on a layer-alloc retry loop, so this is not
  // optional tidiness. s_favLabels is deliberately NOT cleared — it is plain heap, and keeping
  // it is what stops the next visit from re-issuing the browse (see kFavCacheMs).
  if (s_cur == PAGE_FAVORITES && page != PAGE_FAVORITES) {
    lv_obj_clean(s_favList);
    library::clearResults();
    s_favRequested = false;
    s_favRendered  = false;
    lv_obj_remove_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_favStatus, "Loading favourites" LV_SYMBOL_REFRESH);
    // Restore the alignment too: a truncated list moves this label to the bottom of the page
    // (see buildFavRows), and restoring only the text left the next visit's "Loading…" pinned
    // down there instead of under the header.
    lv_obj_align(s_favStatus, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 90);
  }

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

// Tapping a room chip switches the controlled zone. The net task resolves the coordinator and
// re-polls; the UI just states the intent.
static void roomCb(lv_event_t *e) {
  int idx = (int)(intptr_t)lv_event_get_user_data(e);
  if (idx < 0 || idx >= (int)s_roomIps.size()) return;
  uiSoundPlay(UiSound::Confirm);
  Serial.printf("[ui    ] room chip %d -> %s\n", idx, s_roomIps[idx].c_str());
  if (stateLock()) { g_pending.requestZoneIp = s_roomIps[idx]; stateUnlock(); }
  showPage(PAGE_NOW);      // the design treats picking a room as "now show me that room"
}

// Room chips, per the design's RoomChip: pill, status dot, accent tint + accent border when it is
// the controlled zone. Rebuilt only when discovery reports a change (g_zonesGen).
static void rebuildRooms() {
  lv_obj_clean(s_roomsWrap);
  s_roomIps.clear();

  String cur;
  if (stateLock()) { cur = g_player.zoneName; stateUnlock(); }

  s_roomsActive = cur;

  std::vector<sonos::Zone> zs;
  sonos::zonesSnapshot(zs);   // copy: netTask rewrites the live list during discovery
  if (zs.empty()) {
    lv_obj_t *l = label(s_roomsWrap, "Searching for speakers" LV_SYMBOL_REFRESH,
                        &lv_font_montserrat_22, JB_TEXT_MUTED);
    lv_obj_align(l, LV_ALIGN_TOP_LEFT, 0, 0);
    return;
  }

  const lv_coord_t chipW = 220, chipH = 56, gapX = 14, gapY = 12;
  const int cols = 3;
  for (size_t i = 0; i < zs.size(); ++i) {
    const bool active = (zs[i].name == cur);
    s_roomIps.push_back(zs[i].ip);

    lv_obj_t *chip = lv_button_create(s_roomsWrap);
    lv_obj_remove_style_all(chip);
    lv_obj_set_size(chip, chipW, chipH);
    lv_obj_set_style_radius(chip, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(chip, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(JB_SCREEN_ELEV), 0);
    lv_obj_set_style_border_width(chip, 1, 0);
    lv_obj_set_style_border_color(chip, lv_color_hex(active ? JB_ACCENT : JB_SCREEN_LINE), 0);
    lv_obj_set_style_bg_color(chip, lv_color_hex(JB_SCREEN_ELEV_2), LV_STATE_PRESSED);
    // set_pos, not align: aligned children are positioned relative to the parent but do not
    // extend its scrollable content area, so the rows past the fold would still be unreachable.
    lv_obj_set_pos(chip, (lv_coord_t)((i % cols) * (chipW + gapX)),
                   (lv_coord_t)((i / cols) * (chipH + gapY)));
    lv_obj_add_event_cb(chip, roomCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *dot = panel(chip, 8, 8, active ? JB_ACCENT : JB_TEXT_DIM, LV_RADIUS_CIRCLE);
    lv_obj_align(dot, LV_ALIGN_LEFT_MID, 16, 0);

    lv_obj_t *nm = label(chip, zs[i].name.c_str(), &lv_font_montserrat_16,
                         active ? JB_TEXT : JB_TEXT_MUTED);
    lv_label_set_long_mode(nm, LV_LABEL_LONG_DOT);
    lv_obj_set_width(nm, chipW - 44);
    lv_obj_align(nm, LV_ALIGN_LEFT_MID, 32, 0);
  }
}

static void buildRooms() {
  lv_obj_t *pg = s_page[PAGE_ROOMS];
  lv_obj_t *h = label(pg, "Rooms", &lv_font_montserrat_28, JB_TEXT);
  lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 56);   // clear of the status bar

  s_roomsWrap = panel(pg, SCREEN_W - RAIL_W - PAD_X * 2, SCREEN_H - 150, JB_SCREEN_BG, 0);
  lv_obj_align(s_roomsWrap, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 82);
  // Scrollable, like the Radio list. panel() clears SCROLLABLE, and with a fixed 3-column grid at
  // a 68 px row pitch this container shows exactly 6 rows = 18 chips. Zone 19 onward was drawn
  // outside the clip area with no scroll and no notice — a house with more than 18 rooms simply
  // could not reach the later ones. Silent, and worse than the Radio list's honest "showing N of M".
  lv_obj_add_flag(s_roomsWrap, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_roomsWrap, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_roomsWrap, LV_SCROLLBAR_MODE_AUTO);
}

// Play the tapped favourite. library:: was told PLAY_FAVORITE at browse time, so this becomes a
// SetAVTransportURI + Play on the coordinator — the net task does the SOAP, as always.
static void favPlayCb(lv_event_t *e) {
  uiSoundPlay(UiSound::Confirm);
  library::requestPlay((int)(intptr_t)lv_event_get_user_data(e));
  showPage(PAGE_NOW);
}

// One row per favourite, following the design's ListRow: art tile, title, subtitle, trailing
// badge. There is no per-item artwork from a Favourites browse (only labels), so the tile carries
// a glyph rather than a fake image.
// Backstop cap on rendered rows. Each row is 4 LVGL objects (button + art tile + glyph + label) at
// roughly 1 KB of the LVGL pool. Exhausting that pool is not a failure in LVGL — it is a layer-
// alloc RETRY LOOP, so the UI task spins forever, the screen freezes mid-build, and because the
// health heartbeat prints from uiTick the device goes silent on serial too. It looks exactly like
// a total system hang while netTask is in fact still running. CLAUDE.md warns about precisely this.
//
// This was 40 because the pool was the nest's 96 KB, which meant a real 70-favourite system had
// most of its list hidden behind a notice. The jukebox pool is now 512 KB in PSRAM (lv_conf.h), so
// the cap is a safety net rather than the binding constraint: 120 rows is ~120 KB, comfortably
// clear of the four full-screen pages and the keyboard. Anything beyond is still reported rather
// than silently dropped — a truncated list that claims to be complete is its own bug. The
// before/after pool numbers are logged on every build; watch them if rows get more expensive.
static const size_t kMaxFavRows = 120;

static void buildFavRows(const std::vector<String> &labels) {
  lv_obj_clean(s_favList);
  const lv_coord_t rowH = 72, rowW = lv_obj_get_width(s_favList) - 8;

  const size_t shown = labels.size() > kMaxFavRows ? kMaxFavRows : labels.size();
  lv_mem_monitor_t before;
  lv_mem_monitor(&before);
  Serial.printf("[ui    ] favorites: %u item(s), rendering %u, lvgl_free=%uKB\n",
                (unsigned)labels.size(), (unsigned)shown, (unsigned)(before.free_size / 1024));

  for (size_t i = 0; i < shown; ++i) {
    lv_obj_t *row = lv_button_create(s_favList);
    lv_obj_remove_style_all(row);
    lv_obj_set_size(row, rowW, rowH);
    lv_obj_set_style_radius(row, JB_R_MD, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
    lv_obj_set_style_bg_color(row, lv_color_hex(JB_SCREEN_ELEV), LV_STATE_PRESSED);
    lv_obj_align(row, LV_ALIGN_TOP_LEFT, 0, (lv_coord_t)(i * (rowH + 2)));
    lv_obj_add_event_cb(row, favPlayCb, LV_EVENT_CLICKED, (void *)(intptr_t)i);

    lv_obj_t *tile = panel(row, 52, 52, JB_SCREEN_ELEV_2, 10);
    lv_obj_align(tile, LV_ALIGN_LEFT_MID, 6, 0);
    lv_obj_t *g = label(tile, LV_SYMBOL_AUDIO, &lv_font_montserrat_20, JB_TEXT_DIM);
    lv_obj_center(g);

    lv_obj_t *t = label(row, labels[i].c_str(), &lv_font_montserrat_22, JB_TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, rowW - 80);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, 72, 0);
  }
  lv_mem_monitor_t after;
  lv_mem_monitor(&after);
  Serial.printf("[ui    ] favorites: rendered, lvgl_free=%uKB (used %uKB for the list)\n",
                (unsigned)(after.free_size / 1024),
                (unsigned)((before.free_size - after.free_size) / 1024));

  if (labels.size() > shown) {
    // Say so on screen. Silently showing 40 of 120 would be worse than the freeze it prevents.
    lv_obj_remove_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text_fmt(s_favStatus, "Showing first %u of %u favourites",
                          (unsigned)shown, (unsigned)labels.size());
    lv_obj_align(s_favStatus, LV_ALIGN_BOTTOM_LEFT, 0, -PAD_BOT);
  } else {
    lv_obj_add_flag(s_favStatus, LV_OBJ_FLAG_HIDDEN);
  }
}

static void buildFavourites() {
  lv_obj_t *pg = s_page[PAGE_FAVORITES];
  lv_obj_t *h = label(pg, "Favorites", &lv_font_montserrat_28, JB_TEXT);
  lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 56);   // clear of the status bar

  s_favStatus = label(pg, "Loading favourites" LV_SYMBOL_REFRESH, &lv_font_montserrat_22,
                        JB_TEXT_DIM);
  lv_obj_align(s_favStatus, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 90);

  s_favList = lv_obj_create(pg);
  lv_obj_remove_style_all(s_favList);
  lv_obj_set_size(s_favList, SCREEN_W - RAIL_W - PAD_X * 2, SCREEN_H - (PAD_TOP + 90) - PAD_BOT);
  lv_obj_align(s_favList, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 82);
  lv_obj_set_style_bg_opa(s_favList, LV_OPA_TRANSP, 0);
  // Scrollable: a Favourites list is arbitrarily long and this panel is finite.
  lv_obj_set_scroll_dir(s_favList, LV_DIR_VER);
  lv_obj_set_scrollbar_mode(s_favList, LV_SCROLLBAR_MODE_AUTO);
}

// --- Settings -----------------------------------------------------------------------------------
static void soundCb(lv_event_t *e) {
  const int v = lv_slider_get_value((lv_obj_t *)lv_event_get_target(e));
  settingsSetUiSound((uint8_t)v);
  lv_label_set_text_fmt(s_soundVal, "%d", v);
  // Preview at the new level on release, so the slider is judged by ear rather than by number.
  if (lv_event_get_code(e) == LV_EVENT_RELEASED) uiSoundPlay(UiSound::Tick);
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
  lv_label_set_text(s_saveHint, "Saved — restarting to apply…");
  if (stateLock()) { g_pending.reboot = true; stateUnlock(); }
}

static void kbShowCb(lv_event_t *e) {
  lv_obj_remove_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_keyboard_set_textarea(s_kb, (lv_obj_t *)lv_event_get_target(e));
}
static void kbDoneCb(lv_event_t *) { lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN); }

// --- Radio (Amazon Prime Stations) --------------------------------------------------------------
// Placeholder until the SD cache and the station carousel land (plans/08 steps 4-5). It reports the
// real precondition rather than pretending: with no card there is nothing to browse, and saying so
// beats an empty list that looks broken.
static lv_obj_t *s_radioStatus = nullptr;

static void buildRadio() {
  lv_obj_t *pg = s_page[PAGE_RADIO];
  lv_obj_t *h = label(pg, "Radio", &lv_font_montserrat_28, JB_TEXT);
  lv_obj_align(h, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 56);

  s_radioStatus = label(pg, "", &lv_font_montserrat_22, JB_TEXT_MUTED);
  lv_obj_align(s_radioStatus, LV_ALIGN_TOP_LEFT, 0, PAD_TOP + 100);
  lv_label_set_text(s_radioStatus, localStorageRoot()
      ? "Amazon Prime Stations\nStation cache not built yet."
      : "Amazon Prime Stations\nNo SD card — insert one to cache the station list.");
}

static void buildSettings() {
  lv_obj_t *pg = s_page[PAGE_SETTINGS];
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

  // Keyboard last so it draws above everything, hidden until the field is focused.
  s_kb = lv_keyboard_create(pg);
  lv_obj_set_size(s_kb, SCREEN_W - RAIL_W - PAD_X * 2, 240);
  lv_obj_align(s_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_obj_add_flag(s_kb, LV_OBJ_FLAG_HIDDEN);
  lv_obj_add_event_cb(s_kb, kbDoneCb, LV_EVENT_READY, nullptr);
  lv_obj_add_event_cb(s_kb, kbDoneCb, LV_EVENT_CANCEL, nullptr);
}

void uiInit() {
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

  showPage(PAGE_NOW);
  backlightSet(100);
}

static void fmtTime(char *out, size_t n, uint32_t sec, bool negative) {
  snprintf(out, n, "%s%lu:%02lu", negative ? "-" : "", (unsigned long)(sec / 60),
           (unsigned long)(sec % 60));
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

  setTextIfChanged(s_room, s_shown.room, p.zoneName.length() ? p.zoneName : String("no room"));
  setTextIfChanged(s_title, s_shown.title,
                   p.title.length() ? p.title : String("Nothing playing"));

  String meta = p.artist;
  if (p.album.length()) { if (meta.length()) meta += "  ·  "; meta += p.album; }
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
  const lv_coord_t trackW = lv_obj_get_width(s_track);
  int pct = (p.durationSec > 0) ? (int)((uint64_t)p.positionSec * 100 / p.durationSec) : 0;
  if (pct > 100) pct = 100;
  if (pct != s_shown.pct) {
    s_shown.pct = pct;
    lv_obj_set_width(s_fill, trackW * pct / 100);
  }

  char buf[16];
  if (p.positionSec != s_shown.elapsed) {
    s_shown.elapsed = p.positionSec;
    fmtTime(buf, sizeof(buf), p.positionSec, false);
    lv_label_set_text(s_elapsed, buf);
  }
  const uint32_t remain = (p.durationSec > p.positionSec) ? p.durationSec - p.positionSec : 0;
  if (remain != s_shown.remain) {
    s_shown.remain = remain;
    fmtTime(buf, sizeof(buf), remain, true);
    lv_label_set_text(s_remain, buf);
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

  // Rooms: rebuild only when discovery actually changed (g_zonesGen), and only while the page is
  // visible — rebuilding a hidden tree is pure churn on the LVGL pool.
  // g_zonesGen covers discovery changes; the active-room compare covers a zone SWITCH, which does
  // not bump that generation and would otherwise leave the amber outline on the previous room.
  if (s_cur == PAGE_ROOMS && (s_roomsGen != g_zonesGen || s_roomsActive != p.zoneName)) {
    s_roomsGen = g_zonesGen;
    rebuildRooms();
  }

  // Radio: render from the cache if it is fresh, otherwise ask once on arrival and poll for the
  // async result. The browse runs on the net task; takeResults() returns true exactly once when a
  // new set lands. An empty result is cached too, so a system with no favourites doesn't re-browse
  // on every visit either.
  if (s_cur == PAGE_FAVORITES) {
    if (!s_favRendered) {
      const bool fresh = s_favFetchedMs && (millis() - s_favFetchedMs) < kFavCacheMs;
      if (fresh) {
        if (s_favLabels.empty()) lv_label_set_text(s_favStatus, "No favourites on this system");
        else                       buildFavRows(s_favLabels);
        s_favRendered = true;
      } else if (!s_favRequested) {
        s_favRequested = true;
        library::requestBrowse("FV:2", library::PLAY_FAVORITE);
      }
    }
    std::vector<String> labels;
    if (library::takeResults(labels)) {
      s_favLabels    = labels;
      s_favFetchedMs = millis();
      s_favRendered  = true;
      if (labels.empty()) lv_label_set_text(s_favStatus, "No favourites on this system");
      else                buildFavRows(labels);
    }
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
      // Every link field is netTask's published snapshot (g_link*, core/app.h) — reading them
      // here is a plain memory load. Calling WiFi.RSSI()/localIP()/sonos::zones() directly would
      // be a blocking co-processor RPC and an unlocked read of a vector netTask rewrites, i.e.
      // this log would stall or crash the UI task in exactly the fault it exists to report.
      const uint32_t ip = g_linkIp;
      Serial.printf("[health] up=%lus heap=%luKB min=%luKB psram=%luKB wifi=%d rssi=%d "
                    "ip=%u.%u.%u.%u zones=%u lvgl_free=%uKB\n",
                    (unsigned long)(millis() / 1000),
                    (unsigned long)(ESP.getFreeHeap() / 1024),
                    (unsigned long)(ESP.getMinFreeHeap() / 1024),
                    (unsigned long)(ESP.getFreePsram() / 1024),
                    g_linkStatus, g_linkRssi,
                    (unsigned)(ip & 0xFF), (unsigned)((ip >> 8) & 0xFF),
                    (unsigned)((ip >> 16) & 0xFF), (unsigned)((ip >> 24) & 0xFF),
                    (unsigned)g_linkZones, (unsigned)(mon.free_size / 1024));
    }
  }

  lv_timer_handler();
}

void uiProvisioning(const char *apSsid) {
  s_provisioning = panel(lv_screen_active(), SCREEN_W, SCREEN_H, JB_SCREEN_BG, 0);
  lv_obj_t *l = label(s_provisioning, "", &lv_font_montserrat_28, JB_TEXT);
  lv_label_set_text_fmt(l, "Join \"%s\"\non your phone to set up Wi-Fi", apSsid);
  lv_obj_set_style_text_align(l, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_center(l);

  backlightSet(100);
  lv_timer_handler();   // the UI task is not running yet — flush synchronously
}
