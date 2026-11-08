// web_server.cpp
#include "web_server.hpp"

#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "cJSON.h"
#include "esp_event.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_timer.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include "macro_core.h"
#include "macro_json.hpp"
#include "macro_store.hpp"
#include "web_ui.h"

static const char *TAG = "web";

namespace {

MacroEngine *s_engine = nullptr;
WebHooks s_hooks{};

std::string json_escape(const std::string &s) {
  std::string o;
  o.reserve(s.size() + 8);
  for (char c : s) {
    switch (c) {
    case '"': o += "\\\""; break;
    case '\\': o += "\\\\"; break;
    case '\n': o += "\\n"; break;
    case '\r': o += "\\r"; break;
    case '\t': o += "\\t"; break;
    default:
      if (static_cast<unsigned char>(c) < 0x20) o += ' ';
      else o += c;
    }
  }
  return o;
}

bool query_param(httpd_req_t *req, const char *key, std::string &out) {
  char buf[128];
  if (httpd_req_get_url_query_str(req, buf, sizeof buf) != ESP_OK) return false;
  char val[96];
  if (httpd_query_key_value(buf, key, val, sizeof val) != ESP_OK) return false;
  out = val;
  return true;
}

std::string read_body(httpd_req_t *req) {
  std::string body;
  body.reserve(req->content_len);
  char buf[1024];
  int remaining = req->content_len;
  while (remaining > 0) {
    const int want = remaining < (int)sizeof buf ? remaining : (int)sizeof buf;
    const int n = httpd_req_recv(req, buf, want);
    if (n <= 0) {
      if (n == HTTPD_SOCK_ERR_TIMEOUT) continue;
      break;
    }
    body.append(buf, n);
    remaining -= n;
    if (body.size() > 256 * 1024) break;
  }
  return body;
}

esp_err_t send_json(httpd_req_t *req, const std::string &body, const char *status = nullptr) {
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  if (status) httpd_resp_set_status(req, status);
  return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t send_error(httpd_req_t *req, const char *status, const std::string &msg) {
  const std::string body = std::string("{\"error\":\"") + json_escape(msg) + "\"}";
  return send_json(req, body, status);
}

// ---- handlers -------------------------------------------------------------

esp_err_t h_index(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html; charset=utf-8");
  httpd_resp_set_hdr(req, "Cache-Control", "no-store");
  return httpd_resp_send(req, WEB_UI_HTML, (ssize_t)strlen(WEB_UI_HTML));
}

esp_err_t h_status(httpd_req_t *req) {
  std::string out = "{";
  out += "\"sc2\":" + std::string(s_hooks.sc2_connected && s_hooks.sc2_connected() ? "true" : "false");
  out += ",\"console\":" +
         std::string(s_hooks.console_connected && s_hooks.console_connected() ? "true" : "false");
  out += ",\"streaming\":" +
         std::string(s_hooks.console_streaming && s_hooks.console_streaming() ? "true" : "false");
  out += ",\"playing\":" + std::string(s_engine->playing() ? "true" : "false");
  out += ",\"recording\":" + std::string(s_engine->recording() ? "true" : "false");
  out += ",\"pos\":" + std::to_string(s_engine->position_ms());
  out += ",\"dur\":" + std::to_string(s_engine->duration_ms());
  out += ",\"rec_ms\":" + std::to_string(s_engine->record_ms());
  out += ",\"ip\":\"" + json_escape(web_server_ip()) + "\"";
  if (s_hooks.device_info) {
    const std::string extra = s_hooks.device_info();
    if (!extra.empty() && extra != "{}") {
      out += ",";
      out += extra; // `extra` is a JSON fragment without braces
    }
  }
  out += "}";
  return send_json(req, out);
}

esp_err_t h_list(httpd_req_t *req) {
  std::string body;
  macro_store::list(body);
  return send_json(req, body);
}

esp_err_t h_macro_get(httpd_req_t *req) {
  std::string id;
  if (!query_param(req, "id", id)) return send_error(req, "400 Bad Request", "missing id");
  std::string body;
  if (!macro_store::load(id, body)) return send_error(req, "404 Not Found", "no such macro");
  return send_json(req, body);
}

esp_err_t h_macro_save(httpd_req_t *req) {
  const std::string body = read_body(req);
  if (body.empty()) return send_error(req, "400 Bad Request", "empty body");

  std::string id;
  query_param(req, "id", id); // optional: overwrite an existing id

  std::string err, saved;
  if (!macro_store::save(id, body, err, saved)) return send_error(req, "400 Bad Request", err);
  return send_json(req, "{\"ok\":true,\"id\":\"" + json_escape(saved) + "\"}");
}

esp_err_t h_macro_delete(httpd_req_t *req) {
  std::string id;
  if (!query_param(req, "id", id)) return send_error(req, "400 Bad Request", "missing id");
  if (!macro_store::remove(id)) return send_error(req, "404 Not Found", "no such macro");
  return send_json(req, "{\"ok\":true}");
}

esp_err_t h_download(httpd_req_t *req) {
  std::string id;
  if (!query_param(req, "id", id)) return send_error(req, "400 Bad Request", "missing id");
  std::string body;
  if (!macro_store::load(id, body)) return send_error(req, "404 Not Found", "no such macro");
  httpd_resp_set_type(req, "application/json");
  const std::string cd = "attachment; filename=\"" + id + ".json\"";
  httpd_resp_set_hdr(req, "Content-Disposition", cd.c_str());
  return httpd_resp_send(req, body.c_str(), body.size());
}

esp_err_t h_play(httpd_req_t *req) {
  const std::string body = read_body(req);
  if (body.empty()) return send_error(req, "400 Bad Request", "empty body");

  std::vector<s2s::MacroStep> steps;
  macro_json::Meta meta;
  std::string err;

  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  const bool is_ref = root && cJSON_IsObject(root) &&
                      cJSON_GetObjectItemCaseSensitive(root, "id") != nullptr &&
                      cJSON_GetObjectItemCaseSensitive(root, "steps") == nullptr;
  std::string id;
  if (is_ref) {
    cJSON *idit = cJSON_GetObjectItemCaseSensitive(root, "id");
    if (cJSON_IsString(idit) && idit->valuestring) id = idit->valuestring;
  }
  if (root) cJSON_Delete(root);

  if (!id.empty()) {
    std::string doc;
    if (!macro_store::load(id, doc)) return send_error(req, "404 Not Found", "no such macro");
    if (!macro_json::parse(doc, meta, steps, err))
      return send_error(req, "400 Bad Request", err);
  } else {
    if (!macro_json::parse(body, meta, steps, err)) return send_error(req, "400 Bad Request", err);
  }

  if (steps.empty()) return send_error(req, "400 Bad Request", "macro has no steps");
  if (!s_engine->start(steps)) return send_error(req, "409 Conflict", "could not start");

  const std::string out = "{\"ok\":true,\"ms\":" + std::to_string(s2s::macro_total_ms(steps)) + "}";
  return send_json(req, out);
}

esp_err_t h_stop(httpd_req_t *req) {
  s_engine->stop();
  return send_json(req, "{\"ok\":true}");
}

esp_err_t h_record(httpd_req_t *req) {
  const std::string body = read_body(req);
  std::string action = "start";
  cJSON *root = cJSON_ParseWithLength(body.c_str(), body.size());
  if (root) {
    cJSON *a = cJSON_GetObjectItemCaseSensitive(root, "action");
    if (cJSON_IsString(a) && a->valuestring) action = a->valuestring;
    cJSON_Delete(root);
  }

  if (action == "start") {
    if (s_engine->playing()) s_engine->stop();
    s_engine->record_start();
    return send_json(req, "{\"ok\":true,\"recording\":true}");
  }
  if (action == "stop") {
    auto steps = s_engine->record_stop();
    if (steps.empty()) return send_error(req, "409 Conflict", "nothing was recorded");
    macro_json::Meta meta;
    meta.name = "Recorded macro";
    meta.id = "";
    meta.created = (uint32_t)(esp_timer_get_time() / 1000000);
    return send_json(req, macro_json::write(meta, steps));
  }
  return send_error(req, "400 Bad Request", "action must be start or stop");
}

// GET /api/rumble?l=<0..65535>&r=<0..65535>&ms=<0..5000>
// Fires a test rumble on the Steam Controller. Missing params use sensible
// defaults; out-of-range values are clamped so the UI never has to be careful.
esp_err_t h_rumble(httpd_req_t *req) {
  // Parse one optional unsigned param, clamping to `hi`. Returns false (and the
  // caller answers 400) when the text is not a plain non-negative integer.
  auto parse_u32 = [req](const char *key, uint32_t def, uint32_t hi, uint32_t &out) -> bool {
    std::string s;
    if (!query_param(req, key, s) || s.empty()) {
      out = def;
      return true;
    }
    if (s[0] == '-' || s[0] == '+') return false; // strtoul would silently wrap
    errno = 0;
    char *end = nullptr;
    const unsigned long v = strtoul(s.c_str(), &end, 10);
    if (end == s.c_str() || *end != '\0' || errno == ERANGE) return false;
    out = v > hi ? hi : (uint32_t)v;
    return true;
  };

  uint32_t l = 0, r = 0, ms = 0;
  if (!parse_u32("l", 60000, 65535, l)) return send_error(req, "400 Bad Request", "l must be 0..65535");
  if (!parse_u32("r", 60000, 65535, r)) return send_error(req, "400 Bad Request", "r must be 0..65535");
  if (!parse_u32("ms", 400, 5000, ms)) return send_error(req, "400 Bad Request", "ms must be 0..5000");

  if (s_hooks.rumble) s_hooks.rumble((uint16_t)l, (uint16_t)r, ms);

  const std::string out = "{\"ok\":true,\"left\":" + std::to_string(l) +
                          ",\"right\":" + std::to_string(r) + ",\"ms\":" + std::to_string(ms) + "}";
  return send_json(req, out);
}

} // namespace

std::string web_server_ip() {
  esp_netif_t *ap = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
  if (!ap) return "";
  esp_netif_ip_info_t ip{};
  if (esp_netif_get_ip_info(ap, &ip) != ESP_OK) return "";
  char buf[32];
  snprintf(buf, sizeof buf, IPSTR, IP2STR(&ip.ip));
  return buf;
}

bool web_server_begin(MacroEngine &engine, const WebHooks &hooks) {
  s_engine = &engine;
  s_hooks = hooks;

  // --- Wi-Fi access point ---------------------------------------------------
  ESP_ERROR_CHECK(esp_netif_init());
  ESP_ERROR_CHECK(esp_event_loop_create_default());
  esp_netif_create_default_wifi_ap();

  wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
  ESP_ERROR_CHECK(esp_wifi_init(&cfg));

  wifi_config_t wc = {};
  strncpy((char *)wc.ap.ssid, CONFIG_S2S_WIFI_SSID, sizeof wc.ap.ssid - 1);
  wc.ap.ssid_len = strlen(CONFIG_S2S_WIFI_SSID);
  wc.ap.channel = 6;
  wc.ap.max_connection = 2;
  wc.ap.beacon_interval = 200; // ms; fewer beacons = less airtime stolen from BLE
  wc.ap.authmode = WIFI_AUTH_OPEN;
  if (strlen(CONFIG_S2S_WIFI_PASSWORD) >= 8) {
    strncpy((char *)wc.ap.password, CONFIG_S2S_WIFI_PASSWORD, sizeof wc.ap.password - 1);
    wc.ap.authmode = WIFI_AUTH_WPA2_PSK;
  }
  wc.ap.pairwise_cipher = WIFI_CIPHER_TYPE_CCMP;

  ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
  ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &wc));
  ESP_ERROR_CHECK(esp_wifi_set_bandwidth(WIFI_IF_AP, WIFI_BW20));
  ESP_ERROR_CHECK(esp_wifi_start());

  ESP_LOGI(TAG, "Wi-Fi AP '%s' up at %s", CONFIG_S2S_WIFI_SSID, web_server_ip().c_str());

  // --- HTTP server ----------------------------------------------------------
  httpd_config_t hc = HTTPD_DEFAULT_CONFIG();
  hc.max_uri_handlers = 16;
  hc.stack_size = 8192;
  hc.lru_purge_enable = true;
  hc.uri_match_fn = httpd_uri_match_wildcard;

  httpd_handle_t server = nullptr;
  if (httpd_start(&server, &hc) != ESP_OK) {
    ESP_LOGE(TAG, "httpd_start failed");
    return false;
  }

  static const httpd_uri_t routes[] = {
      {.uri = "/", .method = HTTP_GET, .handler = h_index, .user_ctx = nullptr},
      {.uri = "/api/status", .method = HTTP_GET, .handler = h_status, .user_ctx = nullptr},
      {.uri = "/api/macros", .method = HTTP_GET, .handler = h_list, .user_ctx = nullptr},
      {.uri = "/api/macro", .method = HTTP_GET, .handler = h_macro_get, .user_ctx = nullptr},
      {.uri = "/api/macro", .method = HTTP_POST, .handler = h_macro_save, .user_ctx = nullptr},
      {.uri = "/api/macro", .method = HTTP_DELETE, .handler = h_macro_delete, .user_ctx = nullptr},
      {.uri = "/api/download", .method = HTTP_GET, .handler = h_download, .user_ctx = nullptr},
      {.uri = "/api/play", .method = HTTP_POST, .handler = h_play, .user_ctx = nullptr},
      {.uri = "/api/stop", .method = HTTP_POST, .handler = h_stop, .user_ctx = nullptr},
      {.uri = "/api/record", .method = HTTP_POST, .handler = h_record, .user_ctx = nullptr},
      {.uri = "/api/rumble", .method = HTTP_GET, .handler = h_rumble, .user_ctx = nullptr},
  };
  for (const auto &r : routes) httpd_register_uri_handler(server, &r);

  ESP_LOGI(TAG, "web UI ready at http://%s/", web_server_ip().c_str());
  return true;
}
