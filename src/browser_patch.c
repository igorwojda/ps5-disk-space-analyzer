// SPDX-License-Identifier: GPL-3.0-or-later
//
// ps5-web-browser-fullscreen (vendor/browser-fullscreen) compiled into the
// payload, as ps5-web-file-manager does it. The build copies its sources and
// applies patches/browser-fullscreen-embed.patch, which moves the big static
// tables to a workspace that only exists while a patch runs.
#include <stdio.h>
#include <sys/mman.h>

#include "browser_patch.h"

static int patch_restore;
static const char* patch_url;
#define patch_zh 0
static int browser_patch_main(void);
static const char* patch_failure_text(const char* failure) { return failure; }

// Quiet: the upstream payload prints its progress to stdout.
static int patch_quiet_log(const char* text, ...) {
  (void)text;
  return 0;
}
#define printf patch_quiet_log
#define puts patch_quiet_log
#define perror(text) ((void)(text))

#define RESTORE patch_restore
#define FULLSCREEN_SAFE_SCAN 0
#define FULLSCREEN_LIVE_PLAN 0
#define EXPORT_FIXED_PROFILE 0
#define DIAG_DEEP 1
#define HYBRID_BROWSER_URL patch_url
#define main browser_patch_main
#define setvbuf(stream, buffer, mode, size) ((void)0)
#include "main.c"
#undef setvbuf
#undef main
#undef printf
#undef puts
#undef perror

struct patch_workspace {
  __typeof__(diagnostic) report;
  __typeof__(hybrid_record) hybrid[2];
  __typeof__(hybrid_discovered_native) native;
  __typeof__(jsc_record) jsc;
  __typeof__(scan_native_probes) probes;
  __typeof__(anchor_last_graph) graph;
  __typeof__(anchor_open_graph) open_graph;
  uint8_t code[2][HA_CODE_BYTES];
};

static void patch_clear_state(void) {
  hybrid_pending = hybrid_discovered_valid = 0;
  hybrid_cache_status = "not attempted";
  jsc_pending = 0;
  jsc_cache_status = "not attempted";
  fixed = NULL;
  hermes_base = fixed_read_bytes = fixed_read_calls = 0;
  anchor_report_code(0, NULL, NULL);
}

int BrowserPatch(int restore, const char* url) {
  size_t page = (size_t)getpagesize();
  size_t size = (sizeof(struct patch_workspace) + page - 1) / page * page;
  struct patch_workspace* work =
      mmap(NULL, size, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
  if (work == MAP_FAILED) return 1;
  diagnostic_state = &work->report;
  hybrid_record_state = &work->hybrid[0];
  hybrid_disk_record_state = &work->hybrid[1];
  hybrid_discovered_native_state = &work->native;
  jsc_record_state = &work->jsc;
  scan_native_probes_state = &work->probes;
  anchor_last_graph_state = &work->graph;
  anchor_open_graph_state = &work->open_graph;
  anchor_code_state = work->code;
  patch_restore = restore;
  patch_url = url;
  patch_clear_state();
  int result = browser_patch_main();
  patch_clear_state();
  diagnostic_state = NULL;
  hybrid_record_state = hybrid_disk_record_state = NULL;
  hybrid_discovered_native_state = NULL;
  jsc_record_state = NULL;
  scan_native_probes_state = NULL;
  anchor_last_graph_state = NULL;
  anchor_open_graph_state = NULL;
  anchor_code_state = NULL;
  patch_url = NULL;
  patch_restore = 0;
  munmap(work, size);
  return result;
}
