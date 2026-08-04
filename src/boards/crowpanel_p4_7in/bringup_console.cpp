// Serial bring-up console — BUILD-FLAG GATED, NOT SHIPPING CODE.
//
// Compiled only under -DJUKEBOX_BRINGUP_CONSOLE. It exists so the Amazon crawler can be exercised
// on hardware before the Settings DeviceLink ceremony (plans/08 step 7) is built: the credentials
// are otherwise only obtainable through a browser, and there is no other way to get them into NVS.
//
// *** REMOVE THE FLAG once Settings can link an account. *** A serial command that writes account
// credentials into NVS is bring-up scaffolding, not a feature, and it should not outlive its reason.
//
//   amz hh <householdId>     amz tok <token>      amz status
//   amz sn <n>               amz key <key>        radio crawl | radio status
#ifdef JUKEBOX_BRINGUP_CONSOLE

#include <Arduino.h>

#include "core/amazon.h"
#include "core/radio_cache.h"
#include "core/settings.h"
#include "bringup_console.h"

static String s_line;

static void run(const String &cmd) {
  const int sp1 = cmd.indexOf(' ');
  const String verb = (sp1 < 0) ? cmd : cmd.substring(0, sp1);
  const String rest = (sp1 < 0) ? "" : cmd.substring(sp1 + 1);
  const int sp2 = rest.indexOf(' ');
  const String noun = (sp2 < 0) ? rest : rest.substring(0, sp2);
  const String arg  = (sp2 < 0) ? "" : rest.substring(sp2 + 1);

  if (verb == "amz") {
    if (noun == "hh")       { settingsSetHouseholdId(arg);  Serial.printf("[bringup] household=%s\n", arg.c_str()); }
    else if (noun == "sn")  { settingsSetAmazonSerial((uint8_t)arg.toInt()); Serial.printf("[bringup] sn=%d\n", arg.toInt()); }
    else if (noun == "tok") { settingsSetAmazonAuth(arg, settingsAmazonKey()); Serial.printf("[bringup] token set (%u chars)\n", (unsigned)arg.length()); }
    else if (noun == "key") { settingsSetAmazonAuth(settingsAmazonToken(), arg); Serial.printf("[bringup] key set (%u chars)\n", (unsigned)arg.length()); }
    else if (noun == "status") {
      Serial.printf("[bringup] household=%s sn=%u token=%u key=%u linked=%d\n",
                    settingsHouseholdId().c_str(), settingsAmazonSerial(),
                    (unsigned)settingsAmazonToken().length(),
                    (unsigned)settingsAmazonKey().length(), (int)amazon::linked());
    } else Serial.println("[bringup] amz hh|sn|tok|key|status");
    return;
  }
  if (verb == "radio") {
    if (noun == "crawl") { Serial.println("[bringup] crawl requested"); radiocache::requestRefresh(); }
    else Serial.printf("[bringup] ready=%d busy=%d genres=%d fetchedAt=%lu\n",
                       (int)radiocache::ready(), (int)radiocache::busy(),
                       radiocache::genreCount(), (unsigned long)radiocache::fetchedAt());
    return;
  }
  Serial.println("[bringup] commands: amz hh|sn|tok|key|status | radio crawl|status");
}

void bringupConsoleTick() {
  while (Serial.available()) {
    const char c = (char)Serial.read();
    if (c == '\r') continue;
    if (c == '\n') {
      String l = s_line; s_line = "";
      l.trim();
      if (l.length()) run(l);
    } else if (s_line.length() < 2048) {
      s_line += c;                 // tokens are ~650 chars, keys ~525
    }
  }
}

#endif  // JUKEBOX_BRINGUP_CONSOLE
