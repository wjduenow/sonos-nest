// Serial bring-up console. Compiled only under -DJUKEBOX_BRINGUP_CONSOLE; see the .cpp for why it
// exists and when to delete it.
#pragma once

#ifdef JUKEBOX_BRINGUP_CONSOLE
void bringupConsoleTick();
#else
inline void bringupConsoleTick() {}
#endif
