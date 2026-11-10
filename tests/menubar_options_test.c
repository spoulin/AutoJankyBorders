#include "../src/menubar/options.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static void write_file(const char* path, const char* contents) {
  FILE* file = fopen(path, "w");
  assert(file);
  fputs(contents, file);
  fclose(file);
}

int main(void) {
  struct menubar_options options;
  menubar_options_clear(&options);
  assert(menubar_options_is_empty(&options));

  // Only values the daemon accepts can be stored.
  assert(menubar_options_set(&options, "color=auto-gradient"));
  assert(menubar_options_set(&options, "color=0xff112233"));
  assert(!menubar_options_set(&options, "color=0xff1122"));
  assert(!menubar_options_set(&options, "color=red"));
  assert(menubar_options_set(&options, "style=square"));
  assert(!menubar_options_set(&options, "style=wavy"));
  assert(menubar_options_set(&options, "width=5.5"));
  assert(!menubar_options_set(&options, "width=0"));
  assert(!menubar_options_set(&options, "width=wide"));
  assert(menubar_options_set(&options, "color_refresh=0"));
  assert(menubar_options_set(&options, "color_refresh=250"));
  assert(!menubar_options_set(&options, "color_refresh=100"));
  assert(!menubar_options_set(&options, "color_refresh=-1"));
  assert(!menubar_options_set(&options, "hidpi=on"));
  assert(!menubar_options_set(&options, "active_color=0xff112233"));

  // Matching compares meaning rather than spelling.
  assert(menubar_options_matches(&options, "color=0xFF112233"));
  assert(!menubar_options_matches(&options, "color=auto"));
  assert(menubar_options_matches(&options, "style=s"));
  assert(!menubar_options_matches(&options, "style=round"));
  assert(menubar_options_matches(&options, "width=5.50"));
  assert(menubar_options_matches(&options, "color_refresh=250"));

  // Message format: arguments in slot order, then an extra NUL.
  char message[MENUBAR_MESSAGE_SIZE];
  struct menubar_options single;
  menubar_options_clear(&single);
  assert(menubar_options_message(&single, message) == 0);
  menubar_options_set(&single, "width=4.0");
  menubar_options_set(&single, "color=auto");
  size_t length = menubar_options_message(&single, message);
  assert(length == sizeof("color=auto\0width=4.0\0"));
  assert(memcmp(message, "color=auto\0width=4.0\0", length) == 0);

  // Save and load round trip; empty options remove the file.
  char directory[] = "/tmp/menubar_options_XXXXXX";
  assert(mkdtemp(directory));
  char path[256];
  snprintf(path, sizeof(path), "%s/options", directory);
  assert(menubar_options_save(&options, path));
  struct menubar_options loaded;
  menubar_options_load(&loaded, path);
  for (int i = 0; i < MENUBAR_OPTION_COUNT; ++i) {
    assert(strcmp(loaded.values[i], options.values[i]) == 0);
  }

  write_file(path, "color=auto\nnonsense\nhidpi=on\nwidth=-3\nstyle=round\n");
  menubar_options_load(&loaded, path);
  assert(strcmp(loaded.values[MENUBAR_OPTION_COLOR], "color=auto") == 0);
  assert(strcmp(loaded.values[MENUBAR_OPTION_STYLE], "style=round") == 0);
  assert(!loaded.values[MENUBAR_OPTION_WIDTH][0]);

  menubar_options_clear(&loaded);
  assert(menubar_options_save(&loaded, path));
  assert(access(path, F_OK) != 0);
  menubar_options_load(&loaded, path);
  assert(menubar_options_is_empty(&loaded));

  // bordersrc forms: array entries, inline arguments, quotes and comments.
  write_file(path,
             "#!/bin/bash\n"
             "# width=1.0\n"
             "options=(\n"
             "  style=square\n"
             "  width=10.0 # width=2.0\n"
             "  color_refresh=1000\n"
             "  default_color=0xff333333\n"
             ")\n"
             "/opt/bin/borders \"${options[@]}\" \"color=auto-gradient\"\n");
  menubar_options_clear(&loaded);
  menubar_options_scan_script(&loaded, path);
  assert(strcmp(loaded.values[MENUBAR_OPTION_COLOR], "color=auto-gradient") == 0);
  assert(strcmp(loaded.values[MENUBAR_OPTION_STYLE], "style=square") == 0);
  assert(strcmp(loaded.values[MENUBAR_OPTION_WIDTH], "width=10.0") == 0);
  assert(strcmp(loaded.values[MENUBAR_OPTION_COLOR_REFRESH],
                "color_refresh=1000") == 0);

  // Saved choices take precedence over the scanned script.
  menubar_options_clear(&single);
  menubar_options_set(&single, "width=4.0");
  menubar_options_merge(&loaded, &single);
  assert(strcmp(loaded.values[MENUBAR_OPTION_WIDTH], "width=4.0") == 0);
  assert(strcmp(loaded.values[MENUBAR_OPTION_STYLE], "style=square") == 0);

  unlink(path);
  rmdir(directory);

  char stable[256];
  assert(menubar_stable_path("/opt/homebrew/Cellar/autojankyborders/HEAD-23edb01/bin/bordersbar",
                             stable, sizeof(stable)));
  assert(strcmp(stable, "/opt/homebrew/opt/autojankyborders/bin/bordersbar") == 0);
  assert(menubar_stable_path("/Users/me/src/AutoJankyBorders/bin/bordersbar",
                             stable, sizeof(stable)));
  assert(strcmp(stable, "/Users/me/src/AutoJankyBorders/bin/bordersbar") == 0);
  assert(!menubar_stable_path("/a/long/path", stable, 4));

  printf("menubar_options_test: ok\n");
  return 0;
}
