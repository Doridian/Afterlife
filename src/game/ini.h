/*
 * ALIFE.INI access. The original does not use GetPrivateProfileString: the
 * file has no [sections], and every reader calls ini_get_string (0x4ce2e3),
 * which rescans the whole file line by line for a key prefix. See
 * docs/startup.md for the keys.
 */
#ifndef AFTERLIFE_GAME_INI_H
#define AFTERLIFE_GAME_INI_H

#include <stddef.h>

/* Size of the shared result buffer used when ini_get_string gets no buffer. */
#define INI_VALUE_LEN 0x100

/* 0x4c6ad6: the INI file is the .INI named on the command line if there was
 * one, else the executable's base name + ".ini" ("ALIFE.ini"). In the port
 * the name is looked up in the data directory (which stands in for the
 * original's working directory, the executable's folder). */
void ini_set_path(const char *arg_ini_path);
const char *ini_path(void);

/* 0x4ce2e3: finds the first line starting with `key` (case-sensitive prefix
 * compare, so "Mixer Channels" does not match "Mixer channels") and returns
 * the text after '=' with leading spaces skipped and the line ending cut off.
 * Trailing spaces are kept.
 *
 * Lines are read into `buf` (at most size - 1 chars per read). With buf ==
 * NULL a shared static buffer is used, and size is clamped to INI_VALUE_LEN
 * when it is 0 or larger. Returns buf (or the static buffer), or NULL if the
 * file can't be opened, the key isn't found, or the line has no '='.
 * Quirk kept from the original: on a miss, buf holds the last line read. */
char *ini_get_string(char *buf, int size, const char *key);

#endif
