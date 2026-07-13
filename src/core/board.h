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

// --- Rotary input (optional; neutral values on boards without an encoder/knob) ---
int32_t encoderDelta();            // signed detents since last call; 0 if no encoder

// Press classification: Short fires on release of a quick press; Long fires as soon as
// the button has been held past the long-press threshold (no need to release first).
enum class KnobEvent { None, Short, Long };
KnobEvent knobEvent();             // next queued press event; None if no knob
bool      knobPressed();           // true once per Short press; false if no knob
bool      knobDown();              // true while the knob is held; false if no knob

// --- Local audio (optional; boards without an onboard codec/speaker are no-ops) ---
// Play a local audio file (e.g. off the SD card) through an onboard speaker. Async: playback
// runs on a board-owned task, so this returns as soon as it has started. Returns false if the
// board has no audio output or playback failed to start.
bool localAudioPlay(const char *path);
void localAudioStop();
bool localAudioActive();           // true while a local file is playing
void localAudioSetVolume(uint8_t pct);   // 0..100; no-op on boards without audio

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

// Local track library — playable files (e.g. .mp3) on the board's SD card. Empty on boards
// without local storage. Call localTracksRefresh() to (re)scan before listing.
void        localTracksRefresh();
int         localTrackCount();
const char *localTrackName(int i);   // display basename (nullptr if out of range)
const char *localTrackPath(int i);   // full path for localAudioPlay / localFileUrl
