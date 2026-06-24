#include "player_state.h"

SemaphoreHandle_t g_stateMutex = nullptr;
PlayerState       g_player;

void playerStateInit() {
  g_stateMutex = xSemaphoreCreateMutex();
}
