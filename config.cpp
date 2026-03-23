#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

void Config::load() {
  // build path to ~/.kutrc
  const char *home = getenv("HOME");
  if (!home) return;  // no HOME set, skip silently

  char path[256];
  snprintf(path, sizeof(path), "%s/.kutrc", home);

  FILE *fp = fopen(path, "r");
  if (!fp) return;  // no config file, use defaults silently

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    // strip newline
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

    // skip empty lines and comments
    if (line[0] == '\0' || line[0] == '#') continue;

    // split into key and value
    char key[64], value[128];
    if (sscanf(line, "%63s %127s", key, value) != 2) continue;

    // apply settings
    if (strcmp(key, "tab_stop") == 0) {
      int v = atoi(value);
      if (v > 0 && v <= 16) tab_stop = v;  // sanity check
    } else if (strcmp(key, "quit_times") == 0) {
      int v = atoi(value);
      if (v >= 1 && v <= 10) quit_times = v;
    } else if (strcmp(key, "theme") == 0) {
      theme = value;
    }
    // unknown keys are silently ignored; forward compatible
  }
  fclose(fp);
}