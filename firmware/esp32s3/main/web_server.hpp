// web_server.hpp — Wi-Fi AP + REST API + embedded editor UI.
#pragma once

#include <functional>
#include <string>

#include "macro_engine.hpp"

struct WebHooks {
  std::function<bool()> sc2_connected;      // Steam Controller link
  std::function<bool()> console_connected;  // Switch 2 link
  std::function<bool()> console_streaming;  // console subscribed to input
  std::function<std::string()> device_info; // free-form JSON fragment (optional)
  // Fire a test rumble on the Steam Controller for `ms` milliseconds.
  // Speeds are 0..65535; ms == 0 means "leave it running" (the pad stops
  // itself shortly after the last write anyway).
  std::function<void(uint16_t left, uint16_t right, uint32_t ms)> rumble;
};

// Bring up the AP and the HTTP server. Safe to call once from app_main.
bool web_server_begin(MacroEngine &engine, const WebHooks &hooks);

// Current client IP / AP state, for logging.
std::string web_server_ip();
