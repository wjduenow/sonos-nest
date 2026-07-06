#include "player_state.h"

SemaphoreHandle_t g_stateMutex = nullptr;
PlayerState       g_player;
PendingCmds       g_pending;
volatile uint32_t g_zonesGen = 0;

void playerStateInit() {
  g_stateMutex = xSemaphoreCreateMutex();
}
