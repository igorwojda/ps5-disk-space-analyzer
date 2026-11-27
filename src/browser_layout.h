// SPDX-License-Identifier: GPL-3.0-or-later
// Switches the PS5 browser between full screen and its normal window.
#pragma once

// Starts the switch on its own thread: leaves the browser for the home screen,
// patches the browser and opens it again on this app. auto_once: skip it if an
// automatic switch already ran since the payload started, so a failed patch
// does not reopen the browser over and over. Returns false when it did not
// start (not on a PS5, a switch is running, or auto_once skipped it).
bool StartBrowserLayout(bool fullscreen, bool auto_once, unsigned short port);
