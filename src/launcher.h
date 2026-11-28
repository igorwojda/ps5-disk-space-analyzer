// SPDX-License-Identifier: GPL-3.0-or-later
// Home screen tile in the Media tab that opens the UI in the PS5 browser.
#pragma once

// This app's own title: its folders are never offered for deletion.
#define OWN_TITLE_ID "DSAN00002"

// Writes /user/app/<TITLE>/sce_sys/{param.json,icon0.png} and registers the
// title when anything changed. Returns 0 on success. No-op off the PS5.
int InstallLauncher(unsigned short port);
