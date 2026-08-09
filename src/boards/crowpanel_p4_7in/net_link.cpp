// ESP-Hosted link recovery for the CrowPanel Advance 7" (ESP32-P4 + ESP32-C6 over SDIO).
// Implements netLinkRecover() from core/board.h.
//
// THE FAILURE THIS EXISTS FOR. Under sustained traffic the SDIO transport to the C6 degrades and
// stops answering. The give-away is `WiFi.status() == WL_CONNECTED` while `WiFi.RSSI() == 0` with
// the IP still set: RSSI can only come from the co-processor over an RPC, so zero while
// "connected" means the RPC is dead and only the host's cached association state remains. Sockets
// then fail "connection refused", discovery returns nothing, and the room list empties.
//
// Matches open, unresolved upstream reports (espressif/esp-hosted-mcu #167, #121: random mid-run
// transport failures under load). Treat it as a platform limitation to recover from.
//
// *** WHY THIS REBOOTS INSTEAD OF RE-INITIALISING THE TRANSPORT. ***
// The first version of this file did the apparently-correct thing: esp_hosted_deinit() ->
// C6 reset -> esp_hosted_init(). On hardware that HARD-FROZE THE WHOLE DEVICE — no heartbeat, no
// panic, no watchdog, serial silent, physical reset required. Tearing the transport down out from
// under live lwIP users (artTask mid album-art download, the portal registrar, the OTA updater,
// all of which hold sockets) wedges every task. Making that safe would mean quiescing every
// network consumer first and proving it, and a hard freeze on a wall-mounted panel is far worse
// than the fault being recovered from.
//
// A reboot is safe, deterministic, and — since the board variant fixed the ESP-Hosted reset pin
// (variants/crowpanel_p4_7in/pins_arduino.h, GPIO32 = C6_EN) — it genuinely resets the C6 on the
// way back up, which is the thing that actually needs to happen. That was NOT true before the
// variant fix, when a reboot left the C6 untouched and the boot loop could never recover.
//
// Cost: a few seconds of blank screen. Acceptable against a device that is otherwise dead until
// somebody unplugs it.
#include <Arduino.h>
#include "core/net/logmirror.h"

#include "core/board.h"
#include "pins.h"

bool netLinkRecover() {
  LOG.println("[netlink] link is dead and cannot be repaired in place — restarting");
  LOG.println("[netlink] (reboot resets the C6 via GPIO32 during esp_hosted init)");
  LOG.flush();
  delay(50);        // let the log actually reach the wire
  ESP.restart();
  return false;     // not reached
}
