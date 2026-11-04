#include "options.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>

#define CELLAR_PATH "/Cellar/autojankyborders/"
#define OPT_PATH "/opt/autojankyborders"

static const char* const g_prefixes[MENUBAR_OPTION_COUNT] = {
  [MENUBAR_OPTION_COLOR] = "color=",
  [MENUBAR_OPTION_STYLE] = "style=",
  [MENUBAR_OPTION_WIDTH] = "width=",
  [MENUBAR_OPTION_COLOR_REFRESH] = "color_refresh="
};

static int option_slot(const char* argument) {
  for (int i = 0; i < MENUBAR_OPTION_COUNT; ++i) {
    if (strncmp(argument, g_prefixes[i], strlen(g_prefixes[i])) == 0) return i;
  }
  return -1;
}

static const char* option_value(int slot, const char* argument) {
  return argument + strlen(g_prefixes[slot]);
}

static bool color_valid(const char* value) {
  if (strcmp(value, "auto") == 0 || strcmp(value, "inverse") == 0
      || strcmp(value, "auto-gradient") == 0
      || strcmp(value, "inverse-gradient") == 0) return true;
  if (strncmp(value, "0x", 2) != 0 || strlen(value) != 10) return false;
  for (const char* cursor = value + 2; *cursor; ++cursor) {
    if (!isxdigit((unsigned char)*cursor)) return false;
  }
  return true;
}

static bool style_valid(const char* value) {
  return strcmp(value, "round") == 0 || strcmp(value, "square") == 0
         || strcmp(value, "uniform") == 0
         || strcmp(value, "r") == 0 || strcmp(value, "s") == 0
         || strcmp(value, "u") == 0;
}

static bool width_valid(const char* value) {
  char* end;
  double width = strtod(value, &end);
  return end != value && !*end && width > 0.0;
}

// The daemon rejects refresh intervals between 0 and 250 ms.
static bool refresh_valid(const char* value) {
  if (!isdigit((unsigned char)*value)) return false;
  char* end;
  unsigned long refresh = strtoul(value, &end, 10);
  return !*end && (refresh == 0 || refresh >= 250);
}

static bool value_valid(int slot, const char* value) {
  switch (slot) {
    case MENUBAR_OPTION_COLOR: return color_valid(value);
    case MENUBAR_OPTION_STYLE: return style_valid(value);
    case MENUBAR_OPTION_WIDTH: return width_valid(value);
    case MENUBAR_OPTION_COLOR_REFRESH: return refresh_valid(value);
    default: return false;
  }
}

void menubar_options_clear(struct menubar_options* options) {
  memset(options, 0, sizeof(struct menubar_options));
}

bool menubar_options_is_empty(const struct menubar_options* options) {
  for (int i = 0; i < MENUBAR_OPTION_COUNT; ++i) {
    if (options->values[i][0]) return false;
  }
  return true;
}

bool menubar_options_set(struct menubar_options* options, const char* argument) {
  int slot = option_slot(argument);
  if (slot < 0 || strlen(argument) >= MENUBAR_OPTION_SIZE
      || !value_valid(slot, option_value(slot, argument))) return false;
  strcpy(options->values[slot], argument);
  return true;
}

bool menubar_options_matches(const struct menubar_options* options,
                             const char* argument) {
  int slot = option_slot(argument);
  if (slot < 0 || !options->values[slot][0]) return false;
  const char* current = option_value(slot, options->values[slot]);
  const char* expected = option_value(slot, argument);
  switch (slot) {
    case MENUBAR_OPTION_STYLE: return current[0] == expected[0];
    case MENUBAR_OPTION_WIDTH:
    case MENUBAR_OPTION_COLOR_REFRESH:
      return strtod(current, NULL) == strtod(expected, NULL);
    default: return strcasecmp(current, expected) == 0;
  }
}

void menubar_options_merge(struct menubar_options* destination,
                           const struct menubar_options* source) {
  for (int i = 0; i < MENUBAR_OPTION_COUNT; ++i) {
    if (source->values[i][0]) strcpy(destination->values[i], source->values[i]);
  }
}

void menubar_options_load(struct menubar_options* options, const char* path) {
  menubar_options_clear(options);
  FILE* file = fopen(path, "r");
  if (!file) return;
  char line[256];
  while (fgets(line, sizeof(line), file)) {
    line[strcspn(line, "\r\n")] = '\0';
    menubar_options_set(options, line);
  }
  fclose(file);
}

bool menubar_options_save(const struct menubar_options* options, const char* path) {
  if (menubar_options_is_empty(options)) {
    unlink(path);
    return true;
  }

  char temporary[1024];
  if (snprintf(temporary, sizeof(temporary), "%s.tmp", path)
      >= (int)sizeof(temporary)) return false;
  FILE* file = fopen(temporary, "w");
  if (!file) return false;
  bool success = true;
  for (int i = 0; i < MENUBAR_OPTION_COUNT; ++i) {
    if (options->values[i][0] && fprintf(file, "%s\n", options->values[i]) < 0) {
      success = false;
    }
  }
  if (fclose(file) != 0) success = false;
  if (success && rename(temporary, path) != 0) success = false;
  if (!success) unlink(temporary);
  return success;
}

void menubar_options_scan_script(struct menubar_options* options, const char* path) {
  FILE* file = fopen(path, "r");
  if (!file) return;
  char line[1024];
  while (fgets(line, sizeof(line), file)) {
    char* cursor = line;
    char* token;
    while ((token = strsep(&cursor, " \t\r\n"))) {
      if (token[0] == '#') break;
      token += strspn(token, "\"'(");
      token[strcspn(token, "\"')")] = '\0';
      menubar_options_set(options, token);
    }
  }
  fclose(file);
}

size_t menubar_options_message(const struct menubar_options* options,
                               char message[MENUBAR_MESSAGE_SIZE]) {
  size_t length = 0;
  for (int i = 0; i < MENUBAR_OPTION_COUNT; ++i) {
    size_t size = strlen(options->values[i]);
    if (!size) continue;
    memcpy(message + length, options->values[i], size + 1);
    length += size + 1;
  }
  if (!length) return 0;
  message[length++] = '\0';
  return length;
}

bool menubar_stable_path(const char* path, char* stable, size_t size) {
  const char* cellar = strstr(path, CELLAR_PATH);
  const char* bin = cellar ? strstr(cellar + strlen(CELLAR_PATH), "/bin/") : NULL;
  int written;
  if (bin) {
    written = snprintf(stable, size, "%.*s" OPT_PATH "%s",
                       (int)(cellar - path), path, bin);
  } else {
    written = snprintf(stable, size, "%s", path);
  }
  return written >= 0 && (size_t)written < size;
}
