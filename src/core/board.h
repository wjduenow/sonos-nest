// Board HAL — the contract every board (src/boards/<board>/) implements. Declarations
// only: no LVGL, no board pins, no driver headers. The core app + a unit's UI talk to
// the hardware exclusively through these free functions, so a unit can run on any board.
//
// A board brings up its own display (+ LVGL), touch (registered as an LVGL pointer
// indev, so the UI never polls it), and any physical input devices. Touch is push-based
// via LVGL; the rotary encoder + knob button are pull-based (a unit polls them in uiTick).
// Boards without an encoder/knob return neutral values (0 / None / false).
#pragma once

#include <stdint.h>

// Bring up the board: I2C bus, display + LVGL, touch indev, input devices.
// Returns false if the display failed to initialize.
bool boardInit();

// Backlight 0..100%. No-op on boards without a controllable backlight.
void backlightSet(uint8_t pct);

// Button ring 0..100% — the illuminated bezel on the sonos-button family. No-op elsewhere.
//
// Split out from backlightSet() when button-v3 arrived. On the two screenless buttons the ring IS
// "the backlight" — the only light on the box — so they map it onto backlightSet() and that reuse
// cost nothing. button-v3 has a real LCD backlight AND a ring, so the two had to become separate
// calls. The screenless boards keep backlightSet() delegating here, which is what lets main.cpp's
// boot-time backlightSet(settingsBrightness()) go on meaning the right thing on them.
//
// The two levels come from different settings, deliberately: settingsRing() floors at 0 (a ring
// may be fully off) while settingsBrightness() floors at 10, so nobody can blank an LCD and lose
// the UI needed to un-blank it.
void ringSet(uint8_t pct);

// --- Rotary input (optional; neutral values on boards without an encoder/knob) ---
int32_t encoderDelta();            // signed detents since last call; 0 if no encoder

// Press classification: Short fires on release of a quick press; Long fires as soon as
// the button has been held past the long-press threshold (no need to release first).
//
// Double/Triple are emitted only by boards that classify MULTI-presses (today just the
// sonos-button's FLM12 — see boards/esp32s3cam/button.cpp). Everywhere else they never occur, so
// existing units need no new branches. Note what enabling multi-press costs on such a board:
// Short can no longer fire on release, because a release is only a *single* press once the
// multi-press window has expired without another press. A unit wanting instant press feedback
// should drive it off knobDown() (which is still edge-immediate), not off Short.
enum class KnobEvent { None, Short, Long, Double, Triple };
KnobEvent knobEvent();             // next queued press event; None if no knob
bool      knobPressed();           // true once per Short press; false if no knob
bool      knobDown();              // true while the knob is held; false if no knob

// One-shot: has the board's motion sensor seen a TAP? False on boards without an IMU.
// The board samples inside this call, so it must be polled STEADILY (every uiTick) rather than
// only when a caller happens to care — a gap in the polling is a gap in the detection.
// button-v3 wakes its info screen on this — that board has no touch panel, and knocking the case
// is the only gesture that reaches a sealed box.
bool      tapDetected();

// --- Network link recovery (optional) ---
// Boards whose Wi-Fi is a separate co-processor can have the HOST believe it is associated while
// the radio is actually gone. Recovery there means resetting the co-processor, not reconnecting
// Wi-Fi — the normal reconnect path cannot fix a dead transport. Return true if the board did
// something and the caller should re-associate; false (the default) means "nothing I can do",
// which is correct for boards with an on-die radio.
bool netLinkRecover(const char *note);   // note: what to record as the reboot reason (nullptr = "netlink")

// --- Info screen (optional; no-op on boards without a small status panel) ---
// A tiny, mostly-dark signpost display: one QR code, a caption, and a few lines of text. This is
// NOT the LVGL screen a full unit draws. It is deliberately dumb, because the only board with one
// (button-v3) is otherwise HEADLESS and must not link LVGL — see lib/qrcodegen/VENDORING.md for
// what that would have cost. The board owns rendering INCLUDING encoding the QR, which is what
// keeps the encoder and the graphics library inside that one env's lib_deps instead of reaching
// the other two buttons through +<core/>.
//
// The layering is the usual one: the unit decides WHAT to show and WHEN (it is the layer allowed
// to read settings/g_player), the board decides how it looks.
//
// infoScreenShow() is synchronous and self-flushing — uiProvisioning() calls it before any UI task
// exists, so it cannot depend on one. lines/nLines may be nullptr/0 (QR and caption only); all
// strings are consumed before it returns, so callers may pass temporaries.
//
// show() LIGHTS the panel and off() darkens it — the caller must not reach for backlightSet() to
// do that, because on the two screenless buttons running this same unit backlightSet() is the
// BUTTON RING and the screen brightness would come out of the ring. The level is read from
// settingsBrightness() by the board, the same way uiSoundPlay() reads settingsUiSound().
bool infoScreenPresent();
void infoScreenShow(const char *qrText, const char *caption,
                    const char *const *lines, uint8_t nLines);
void infoScreenOff();

// --- UI feedback tones (optional; no-op on boards without a speaker) ---
// Short non-musical confirmations for touch/press, NOT media playback — deliberately separate
// from localAudio*, which means "play a file off local storage". A board with speakers but no
// storage (the jukebox) implements this and stubs those; the reverse is equally valid.
// Volume is not a parameter: the level is a user setting (settingsUiSound()), read by the board.
enum class UiSound {
  Tick,      // a control was pressed
  Confirm,   // an action was accepted (room changed, favourite started)
  Error,     // an action failed
};
void uiSoundPlay(UiSound s);

// --- Local audio (optional; boards without an onboard codec/speaker are no-ops) ---
// Play a local audio file (e.g. off the SD card) through an onboard speaker. Async: playback
// runs on a board-owned task, so this returns as soon as it has started. Returns false if the
// board has no audio output or playback failed to start.
bool localAudioPlay(const char *path);
void localAudioStop();
bool localAudioActive();           // true while a local file is playing
void localAudioSetVolume(uint8_t pct);   // 0..100; no-op on boards without audio

// --- Wake word (optional; boards without a mic are no-ops) ---
// On-device wake-word detection. The board owns the microphone, the feature frontend and the
// models, and runs them on its own task — it reports *which phrase was heard* and nothing more.
// Deciding what a phrase DOES is the unit's job (boards must not reach into g_pending/settings),
// so this HAL is deliberately just "did you hear something".
//
// wakeWordInit()  — start the engine. false if the board has no mic or bring-up failed.
// wakeWordPoll()  — non-blocking: index of a phrase detected since the last call, else -1.
//                   Only the most recent detection is kept; call it from a loop (uiTick).
// wakeWordPhrase()— human-readable phrase for an index (nullptr out of range), for logs/UI.
// The index order is the board's, and matches wakeWordPhrase() — a unit should map by asking,
// not by hard-coding numbers.
bool        wakeWordInit();
int         wakeWordPoll();
const char *wakeWordPhrase(int i);
int         wakeWordCount();

// Serve a local file (e.g. off the SD card) over HTTP so a network player (Sonos) can stream
// it, and return the URL to hand the player. Starts a small HTTP server on first use. Returns
// nullptr if the board has no local storage/server or it couldn't start. The returned pointer
// is owned by the board; copy it before the next call.
const char *localFileUrl(const char *path);

// Base URL of the board's file-management web UI (add/remove tracks on its local storage from a
// browser), e.g. "http://192.168.1.20:8080". nullptr on boards without one, or when there's no
// network yet. The board owns the port; the UI just displays what it returns. Pointer is owned
// by the board — copy it before the next call.
const char *localManagerUrl();

// Base URL of the board's web CONFIG page — what a user clicks in the sonos-portal dashboard to
// configure this device from a browser. Distinct from localManagerUrl(): that means a *file*
// manager (only boards with local storage), whereas some boards (the button) serve a config page
// but have no files to manage. nullptr on boards with no web UI at all (the nest), or before the
// network is up. Pointer is owned by the board — copy it before the next call.
const char *boardConfigUrl();

// Root of a writable filesystem the firmware may use for its own data (caches, indexes), e.g.
// "/sdcard". nullptr on boards with no storage, or when the card is missing or unreadable — callers
// MUST treat nullptr as "this feature is unavailable", not as an error to retry.
// Files underneath are accessed with plain stdio; the board only owns mounting it.
//
// *** Writers: 4 KB per write() call, maximum, and keep any single file under ~256 KB. ***
// Larger chunks fail immediately on this hardware and sustained writes die past ~300 KB. Measured,
// not folklore — see plans/08-music-service-integration.md.
const char *localStorageRoot();

// Local track library — playable files (e.g. .mp3) on the board's SD card. Empty on boards
// without local storage. Call localTracksRefresh() to (re)scan before listing.
void        localTracksRefresh();
int         localTrackCount();
const char *localTrackName(int i);   // display basename (nullptr if out of range)
const char *localTrackPath(int i);   // full path for localAudioPlay / localFileUrl
