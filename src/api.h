// SPDX-License-Identifier: GPL-3.0-or-later
// The JSON API and static files behind the web UI.
//
//   GET  /api/storages            storages with free/total
//   POST /api/scan?path=P         start scanning P (replaces the last scan)
//   POST /api/cancel              cancel the running scan
//   GET  /api/status              phase, progress, last delete result
//   GET  /api/node?id=N           folder N: children (all) + rings for the chart
//   GET  /api/biggest             biggest files of the scan
//   POST /api/delete?ids=1,2,3    delete these nodes (protected ones are refused)
//   POST /api/quit                stop the payload
#pragma once

#include <string>
#include <vector>

#include "http.h"

// web_dir: serve the UI from disk (development), "" = built-in copy.
// extra: folders offered on the storage list besides the defaults.
void ApiInit(const std::string& web_dir, const std::vector<std::string>& extra);
void ApiHandle(const Request& req, Response& res);
