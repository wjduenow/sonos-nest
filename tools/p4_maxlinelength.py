# Work around a SCons 4.8.1 crash on the ESP32-P4 (pioarduino) builds.
#
# The IDF 5.5 include/lib list is long enough that compile commands exceed SCons'
# default MAXLINELENGTH (2048), which sends it down the TempFileMunge response-file
# path — and that path is broken in the bundled SCons 4.8.1:
#
#   File ".../SCons/Platform/__init__.py", line 220, in __call__
#     length += len(c)
#   AttributeError: 'CmdStringHolder' object has no attribute 'data'
#
# It shows up as a per-object-file failure (e.g. on lvgl's lv_flex.c.o), not as a
# compiler error, so it reads like a code problem when it isn't.
#
# Linux allows ~2 MB of argv (ARG_MAX), so simply raising the threshold keeps SCons on
# the normal direct-exec path and the response file is never built. The S3 envs never hit
# this — their include list is much shorter — so this is wired only into the jukebox envs.
Import("env")

env["MAXLINELENGTH"] = 2000000
