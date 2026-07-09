// SD card -> USB Mass Storage bridge (bring-up mode, ES3C28P board).
// Presents the inserted microSD to the host as a removable USB drive so files can be dropped
// onto it without pulling the card. Built only by the `sleep-machine-sdreader` env. Never
// returns (call it as the whole program, like the nest's bringupRun()).
#pragma once

void sdMscRun();
