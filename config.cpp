#include "config.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static std::string getHomeDir() {
#ifdef _WIN32
  const char *home = getenv("USERPROFILE");
  if (home && *home) return home;
  const char *drive = getenv("HOMEDRIVE");
  const char *path = getenv("HOMEPATH");
  if (drive && path) return std::string(drive) + path;
  return "";
#else
  const char *home = getenv("HOME");
  return home ? home : "";
#endif
}

void Config::load() {
  std::string home = getHomeDir();
  if (home.empty()) return;  // no home dir found, skip silently

  char path[256];
  snprintf(path, sizeof(path), "%s/.kutrc", home.c_str());

  FILE *fp = fopen(path, "r");
  if (!fp) return;  // no config file, use defaults silently

  char line[256];
  while (fgets(line, sizeof(line), fp)) {
    int len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') line[len - 1] = '\0';

    if (line[0] == '\0' || line[0] == '#') continue;

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
    } else if (strcmp(key, "line_numbers") == 0) {
      line_numbers = (strcmp(value, "true") == 0);
    }
    // unknown keys are silently ignored; forward compatible
  }
  fclose(fp);
}