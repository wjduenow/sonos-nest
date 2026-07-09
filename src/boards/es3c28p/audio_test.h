// Local audio playback bring-up (ES3C28P board): play the ocean-sounds MP3 off the microSD
// through the onboard ES8311 codec + speaker. Built only by the `sleep-machine-audio` env.
// Never returns (call it as the whole program, like sdMscRun()).
#pragma once

void audioTestRun();
