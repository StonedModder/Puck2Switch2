// macro_store.cpp
#include "macro_store.hpp"

#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

#include "cJSON.h"
#include "esp_log.h"
#include "esp_spiffs.h"

#include "macro_core.h"
#include "macro_json.hpp"

static const char *TAG = "macro_store";
static constexpr const char *kMount = "/macros";
static constexpr size_t kMaxMacroBytes = 96 * 1024;
static constexpr int kMaxMacros = 64;

namespace {

bool valid_id(const std::string &id) {
  if (id.empty() || id.size() > 64) return false;
  for (char c : id) {
    if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_')) return false;
  }
  return true;
}

std::string path_for(const std::string &id) { return std::string(kMount) + "/" + id + ".json"; }

bool read_file(const std::string &path, std::string &out) {
  FILE *f = std::fopen(path.c_str(), "rb");
  if (!f) return false;
  out.clear();
  char buf[512];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) {
    out.append(buf, n);
    if (out.size() > kMaxMacroBytes) {
      std::fclose(f);
      return false;
    }
  }
  std::fclose(f);
  return true;
}

bool write_file(const std::string &path, const std::string &data) {
  FILE *f = std::fopen(path.c_str(), "wb");
  if (!f) return false;
  const size_t n = std::fwrite(data.data(), 1, data.size(), f);
  std::fclose(f);
  return n == data.size();
}

int count_macros() {
  DIR *d = opendir(kMount);
  if (!d) return 0;
  int n = 0;
  struct dirent *e;
  while ((e = readdir(d)) != nullptr) {
    const size_t len = std::strlen(e->d_name);
    if (len > 5 && std::strcmp(e->d_name + len - 5, ".json") == 0) ++n;
  }
  closedir(d);
  return n;
}

} // namespace

namespace macro_store {

bool init() {
  esp_vfs_spiffs_conf_t conf = {};
  conf.base_path = kMount;
  conf.partition_label = "storage";
  conf.max_files = 8;
  conf.format_if_mount_failed = true;

  esp_err_t err = esp_vfs_spiffs_register(&conf);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "SPIFFS mount failed: %s", esp_err_to_name(err));
    return false;
  }
  size_t total = 0, used = 0;
  esp_spiffs_info("storage", &total, &used);
  ESP_LOGI(TAG, "macro storage ready: %u/%u bytes used, %d macro(s)", (unsigned)used,
           (unsigned)total, count_macros());
  return true;
}

bool list(std::string &json_out) {
  DIR *d = opendir(kMount);
  if (!d) {
    json_out = "[]";
    return false;
  }
  cJSON *arr = cJSON_CreateArray();
  struct dirent *e;
  while ((e = readdir(d)) != nullptr) {
    const size_t len = std::strlen(e->d_name);
    if (len <= 5 || std::strcmp(e->d_name + len - 5, ".json") != 0) continue;

    std::string id(e->d_name, len - 5);
    std::string body;
    if (!read_file(path_for(id), body)) continue;

    macro_json::Meta meta;
    std::vector<s2s::MacroStep> steps;
    std::string err;
    if (!macro_json::parse(body, meta, steps, err)) {
      ESP_LOGW(TAG, "skipping unreadable macro %s: %s", id.c_str(), err.c_str());
      continue;
    }
    cJSON *o = cJSON_CreateObject();
    cJSON_AddStringToObject(o, "id", id.c_str());
    cJSON_AddStringToObject(o, "name", meta.name.c_str());
    cJSON_AddStringToObject(o, "game", meta.game.c_str());
    cJSON_AddNumberToObject(o, "steps", (double)steps.size());
    cJSON_AddNumberToObject(o, "ms", (double)s2s::macro_total_ms(steps));
    cJSON_AddItemToArray(arr, o);
  }
  closedir(d);

  char *out = cJSON_PrintUnformatted(arr);
  json_out = out ? out : "[]";
  if (out) cJSON_free(out);
  cJSON_Delete(arr);
  return true;
}

bool load(const std::string &id, std::string &out) {
  if (!valid_id(id)) return false;
  return read_file(path_for(id), out);
}

bool save(const std::string &id, const std::string &body, std::string &err_out,
          std::string &saved_id) {
  macro_json::Meta meta;
  std::vector<s2s::MacroStep> steps;
  std::string err;
  if (!macro_json::parse(body, meta, steps, err)) {
    err_out = err;
    return false;
  }

  std::string target = id.empty() ? meta.id : id;
  if (target.empty()) target = macro_json::slugify(meta.name);
  if (!valid_id(target)) {
    err_out = "invalid macro id";
    return false;
  }

  // Creating a new macro has to respect the cap; overwriting does not.
  const bool exists = (access(path_for(target).c_str(), F_OK) == 0);
  if (!exists && count_macros() >= kMaxMacros) {
    err_out = "macro library full (max 64)";
    return false;
  }

  meta.id = target;
  const std::string out = macro_json::write(meta, steps);
  if (!write_file(path_for(target), out)) {
    err_out = "write failed (storage full?)";
    return false;
  }
  saved_id = target;
  return true;
}

bool remove(const std::string &id) {
  if (!valid_id(id)) return false;
  return std::remove(path_for(id).c_str()) == 0;
}

} // namespace macro_store
