// SPDX-License-Identifier: GPL-3.0-or-later
// The PS5 browser's layout, patched in ShellUI's memory (browser_patch.c).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Makes the browser full screen (restore = 0) or puts back its normal window
// (restore = 1). Takes effect the next time the browser opens. url is opened
// once if the browser has to be started to find its code. Returns 0 on
// success, 1 on failure, 2 when the patch worked but its cache was not saved.
// Failures show a system notification.
int BrowserPatch(int restore, const char* url);

#ifdef __cplusplus
}
#endif
