// SPDX-License-Identifier: GPL-3.0-or-later
// Minimal HTTP/1.1 server: one thread per connection, Connection: close.
// Enough for a single-page UI and a small JSON API.
#pragma once

#include <functional>
#include <map>
#include <string>

struct Request {
  std::string method;
  std::string path;                          // without the query string
  std::map<std::string, std::string> query;  // decoded
  std::string body;
  std::string local_ip;  // our address on the connection the request came in on
};

struct Response {
  int status = 200;
  std::string type = "application/json";
  std::string body;
  const void* data = nullptr;  // static data instead of body
  size_t size = 0;
};

using Handler = std::function<void(const Request&, Response&)>;

// First free port starting at `port`, or 0.
unsigned short FindFreePort(unsigned short port);

// IPv4 address of the LAN interface (the one with the default route), or "".
std::string LanAddress();

// True for 127.x.x.x and an empty string.
bool IsLoopback(const std::string& ip);

// Blocks serving requests until Stop() is called.
int Serve(unsigned short port, Handler handler);
void Stop();
