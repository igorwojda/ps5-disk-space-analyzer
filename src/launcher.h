// SPDX-License-Identifier: GPL-3.0-or-later
// Home screen tile in the Media tab that opens the UI in the PS5 browser.
#pragma once

// Writes /user/app/<TITLE>/sce_sys/{param.json,icon0.png} and registers the
// title when anything changed. Returns 0 on success. No-op off the PS5.
int InstallLauncher(unsigned short port);
