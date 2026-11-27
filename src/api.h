// SPDX-License-Identifier: GPL-3.0-or-later
// The JSON API and static files behind the web UI.
//
//   GET  /api/info                app version, LAN address and port
//   GET  /api/storages            storages with free/total
//   POST /api/scan?path=P         start scanning P (replaces the last scan)
//   POST /api/cancel              cancel the running scan
//   GET  /api/status              phase, progress, last delete or move result
//   GET  /api/node?id=N           folder N: children (all) + rings for the chart
//   GET  /api/biggest             biggest files of the scan
//   GET  /api/games               every game's folders on every volume, added up
//   POST /api/delete?ids=1,2,3    delete these nodes (protected ones are refused);
//                                 paths=a\nb deletes game folders found by /api/games
//   POST /api/move?dest=D&ids=..  move the same kind of selection into folder D
//                                 (a storage or an unprotected scanned folder);
//                                 srcs=a\nb&sizes=1,2 adds cut items by path,
//                                 from any listed storage
//   POST /api/browser-layout?full=1|0[&auto=1]
//                                 switch the PS5 browser to full screen or back
//                                 to its window, then reopen it on this app
//   POST /api/quit                stop the payload
#pragma once

#include <string>
#include <vector>

#include "http.h"

// web_dir: serve the UI from disk (development), "" = built-in copy.
// extra: folders offered on the storage list besides the defaults.
// port: the port the server listens on (shown in the UI).
void ApiInit(const std::string& web_dir, const std::vector<std::string>& extra, unsigned short port);
void ApiHandle(const Request& req, Response& res);
