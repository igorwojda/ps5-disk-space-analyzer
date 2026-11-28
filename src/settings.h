// SPDX-License-Identifier: GPL-3.0-or-later
// Settings kept by the payload, shared by every device that opens the app.
#pragma once

#include <string>

// On the PS5: a flat JSON object of key/value pairs in
// /data/disk-space-analyzer/settings.json. It is read again on every call and
// rewritten on every change, so deleting it brings back the defaults at once.
// On a computer: kept for this run only.

// The whole object, as JSON text.
std::string SettingsJson();

// The value saved under `key`, or `def` when there is none or it is not a bool.
bool SettingBool(const std::string& key, bool def);
void SetSetting(const std::string& key, bool value);
// Saves `json` (already valid JSON: true, 1.2, "blue") under `key`.
void SetSettingJson(const std::string& key, const std::string& json);
