// SPDX-License-Identifier: GPL-3.0-or-later
// Logging and on-screen notifications.
#pragma once

// Writes to stderr (websrv shows it with ?pipe=1) and to the log file
// (/data/disk-space-analyzer/disk-space-analyzer.log on the PS5).
void LogOpen();
void Log(const char* fmt, ...) __attribute__((format(printf, 1, 2)));

// A PS5 system toast; stderr elsewhere. Use for things the user must see.
void Notify(const char* fmt, ...) __attribute__((format(printf, 1, 2)));
