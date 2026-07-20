// UX contract — the universal subset every unit (src/units/<unit>/) implements. The core
// app/boot layer calls only these; anything richer (screen enums, uiShow, etc.) stays
// private to the unit's own screens.h. Keep this header free of LVGL and unit internals.
#pragma once

void uiInit();    // build the unit's LVGL screens/widgets (call after boardInit())
void uiTick();    // per-frame: lv_timer_handler() + route this unit's input

// Called by appBoot() right before the WiFi captive portal blocks (no creds, or re-provision).
// Screened units draw a full-screen "join <apSsid> on your phone" message and flush it themselves
// (the UI task isn't running yet); headless units have no screen and no-op. The unit removes the
// message on its first uiTick(), by which point WiFi is provisioned and normal UI resumes.
void uiProvisioning(const char *apSsid);
