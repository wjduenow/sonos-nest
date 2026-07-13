// ES3C28P HTTP server — media streaming (for Sonos) + remote SD file management.
// Board-local, not part of the core HAL: the HAL surface is localFileUrl() in core/board.h.
#pragma once

// Start the HTTP server task. Safe to call before WiFi is up — the task waits for a
// connection before binding. Idempotent.
void mediaServerStart();
