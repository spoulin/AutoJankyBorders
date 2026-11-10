#pragma once
#include <stdbool.h>
#include <stddef.h>

enum menubar_option_slot {
  MENUBAR_OPTION_COLOR,
  MENUBAR_OPTION_STYLE,
  MENUBAR_OPTION_WIDTH,
  MENUBAR_OPTION_COLOR_REFRESH,
  MENUBAR_OPTION_COUNT
};

#define MENUBAR_OPTION_SIZE 32
#define MENUBAR_MESSAGE_SIZE (MENUBAR_OPTION_COUNT * MENUBAR_OPTION_SIZE + 1)

// Holds complete `key=value` arguments. An empty slot means the option is unset.
struct menubar_options {
  char values[MENUBAR_OPTION_COUNT][MENUBAR_OPTION_SIZE];
};

void menubar_options_clear(struct menubar_options* options);
bool menubar_options_is_empty(const struct menubar_options* options);

// Stores a supported argument in its slot. Anything else is rejected.
bool menubar_options_set(struct menubar_options* options, const char* argument);

// Compares by meaning: `width=10` matches `width=10.0`, `style=s` `style=square`.
bool menubar_options_matches(const struct menubar_options* options,
                             const char* argument);

// Copies the options that are set in `source` over those in `destination`.
void menubar_options_merge(struct menubar_options* destination,
                           const struct menubar_options* source);

// Reads one argument per line. A missing file yields empty options.
void menubar_options_load(struct menubar_options* options, const char* path);

// Replaces the file atomically. Empty options remove it.
bool menubar_options_save(const struct menubar_options* options, const char* path);

// Best-effort scan of a shell script for arguments, used only for display.
void menubar_options_scan_script(struct menubar_options* options, const char* path);

// Builds the daemon message: NUL-terminated arguments and a final NUL.
// Returns its length, or 0 when no option is set.
size_t menubar_options_message(const struct menubar_options* options,
                               char message[MENUBAR_MESSAGE_SIZE]);

// Maps a Homebrew Cellar path to the stable opt path that survives upgrades.
bool menubar_stable_path(const char* path, char* stable, size_t size);
