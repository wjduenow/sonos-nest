// microSD mount for the CrowPanel Advance 7". Board-internal lifecycle; the storage itself is
// exposed to the rest of the firmware through localStorageRoot() in core/board.h, so nothing
// outside this board needs to know it is an SD card rather than flash or anything else.
//
// Everything about the pins, the 4 KB write rule and the traps lives in pins.h and
// plans/08-music-service-integration.md. Read the write rule before adding a writer.
#pragma once

bool sdCardInit();     // mount at SD_MOUNT_POINT. False if there is no card or no filesystem.
bool sdCardMounted();
uint64_t sdCardFreeBytes();
