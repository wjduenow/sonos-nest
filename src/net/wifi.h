// WiFi connection + credential persistence (NVS). See plan §6.
#pragma once

bool wifiConnect();       // load NVS creds -> connect; else captive portal config
bool wifiIsConnected();
// TODO Phase 4: captive-portal first-boot setup with on-screen keyboard.
