// macro_json.cpp
#include "macro_json.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

#include "cJSON.h"

using namespace s2s;

namespace {
struct NamedBit {
  const char *name;
  uint32_t bit;
};

// Order matters only for display; parsing is order-independent.
constexpr NamedBit kButtons[] = {
    {"A", MB_A},       {"B", MB_B},         {"X", MB_X},        {"Y", MB_Y},
    {"L", MB_L},       {"R", MB_R},         {"ZL", MB_ZL},      {"ZR", MB_ZR},
    {"Plus", MB_PLUS}, {"Minus", MB_MINUS}, {"Home", MB_HOME},  {"Capture", MB_CAPTURE},
    {"LStick", MB_LSTICK}, {"RStick", MB_RSTICK}, {"Up", MB_DUP},  {"Down", MB_DDOWN},
    {"Left", MB_DLEFT},    {"Right", MB_DRIGHT},  {"GL", MB_GL},   {"GR", MB_GR},
};

inline uint16_t clamp_u12(double v) {
  if (v < 0) v = 0;
  if (v > MACRO_STICK_MAX) v = MACRO_STICK_MAX;
  return static_cast<uint16_t>(v + 0.5);
}
} // namespace

namespace macro_json {

const char *button_name(uint32_t bit) {
  for (const auto &b : kButtons)
    if (b.bit == bit) return b.name;
  return nullptr;
}

bool button_from_name(const std::string &name, uint32_t &bit) {
  // Accept a few shorthands the editor may send.
  if (name == "+") return bit = MB_PLUS, true;
  if (name == "-") return bit = MB_MINUS, true;
  for (const auto &b : kButtons) {
    if (name == b.name) {
      bit = b.bit;
      return true;
    }
  }
  // case-insensitive fallback
  for (const auto &b : kButtons) {
    if (name.size() == std::strlen(b.name)) {
      bool eq = true;
      for (size_t i = 0; i < name.size(); ++i)
        if (std::tolower((unsigned char)name[i]) != std::tolower((unsigned char)b.name[i])) {
          eq = false;
          break;
        }
      if (eq) {
        bit = b.bit;
        return true;
      }
    }
  }
  return false;
}

std::string slugify(const std::string &name) {
  std::string out;
  bool dash = false;
  for (char c : name) {
    const unsigned char u = static_cast<unsigned char>(c);
    if (std::isalnum(u)) {
      out.push_back(static_cast<char>(std::tolower(u)));
      dash = false;
    } else if (!out.empty() && !dash) {
      out.push_back('-');
      dash = true;
    }
    if (out.size() >= 48) break;
  }
  while (!out.empty() && out.back() == '-') out.pop_back();
  if (out.empty()) out = "macro";
  return out;
}

bool parse(const std::string &json, Meta &meta, std::vector<MacroStep> &steps, std::string &err) {
  steps.clear();
  cJSON *root = cJSON_ParseWithLength(json.c_str(), json.size());
  if (!root) {
    err = "invalid JSON";
    return false;
  }

  auto get_str = [&](const char *k) -> std::string {
    cJSON *it = cJSON_GetObjectItemCaseSensitive(root, k);
    return (it && cJSON_IsString(it) && it->valuestring) ? it->valuestring : "";
  };
  const std::string v = get_str("v");
  if (!v.empty() && v != "1") {
    err = "unsupported version " + v;
    cJSON_Delete(root);
    return false;
  }

  meta.id = get_str("id");
  meta.game = get_str("game");
  meta.author = get_str("author");
  const std::string nm = get_str("name");
  if (!nm.empty()) meta.name = nm;
  cJSON *created = cJSON_GetObjectItemCaseSensitive(root, "created");
  if (created && cJSON_IsNumber(created)) meta.created = (uint32_t)created->valuedouble;

  cJSON *arr = cJSON_GetObjectItemCaseSensitive(root, "steps");
  if (!cJSON_IsArray(arr)) {
    err = "missing steps array";
    cJSON_Delete(root);
    return false;
  }
  if (cJSON_GetArraySize(arr) > 2000) {
    err = "too many steps (max 2000)";
    cJSON_Delete(root);
    return false;
  }

  int index = 0;
  cJSON *it = nullptr;
  cJSON_ArrayForEach(it, arr) {
    ++index;
    if (!cJSON_IsObject(it)) {
      err = "step " + std::to_string(index) + " is not an object";
      cJSON_Delete(root);
      return false;
    }
    MacroStep s;

    cJSON *d = cJSON_GetObjectItemCaseSensitive(it, "d");
    if (!cJSON_IsNumber(d)) {
      err = "step " + std::to_string(index) + " missing duration";
      cJSON_Delete(root);
      return false;
    }
    double dv = d->valuedouble;
    if (dv < MACRO_MIN_STEP_MS) dv = MACRO_MIN_STEP_MS;
    if (dv > MACRO_MAX_STEP_MS) dv = MACRO_MAX_STEP_MS;
    s.hold_ms = static_cast<uint16_t>(dv + 0.5);

    cJSON *b = cJSON_GetObjectItemCaseSensitive(it, "b");
    if (b) {
      if (cJSON_IsArray(b)) {
        cJSON *bn = nullptr;
        cJSON_ArrayForEach(bn, b) {
          if (!cJSON_IsString(bn) || !bn->valuestring) continue;
          uint32_t bit = 0;
          if (button_from_name(bn->valuestring, bit)) s.buttons |= bit;
          else {
            err = std::string("unknown button '") + bn->valuestring + "' in step " +
                  std::to_string(index);
            cJSON_Delete(root);
            return false;
          }
        }
      } else if (cJSON_IsNumber(b)) {
        s.buttons = static_cast<uint32_t>(b->valuedouble); // bitmask form (round-trips)
      }
    }

    auto axis = [&](const char *k, uint16_t def) -> uint16_t {
      cJSON *a = cJSON_GetObjectItemCaseSensitive(it, k);
      return cJSON_IsNumber(a) ? clamp_u12(a->valuedouble) : def;
    };
    s.lx = axis("lx", MACRO_STICK_CENTER);
    s.ly = axis("ly", MACRO_STICK_CENTER);
    s.rx = axis("rx", MACRO_STICK_CENTER);
    s.ry = axis("ry", MACRO_STICK_CENTER);

    steps.push_back(s);
  }

  cJSON_Delete(root);
  if (steps.empty()) {
    err = "macro has no steps";
    return false;
  }
  if (meta.id.empty()) meta.id = slugify(meta.name);
  return true;
}

std::string write(const Meta &meta, const std::vector<MacroStep> &steps) {
  cJSON *root = cJSON_CreateObject();
  cJSON_AddNumberToObject(root, "v", 1);
  cJSON_AddStringToObject(root, "id", meta.id.c_str());
  cJSON_AddStringToObject(root, "name", meta.name.c_str());
  if (!meta.game.empty()) cJSON_AddStringToObject(root, "game", meta.game.c_str());
  if (!meta.author.empty()) cJSON_AddStringToObject(root, "author", meta.author.c_str());
  cJSON_AddNumberToObject(root, "created", meta.created ? meta.created : 0);
  cJSON_AddNumberToObject(root, "ms", macro_total_ms(steps));

  cJSON *arr = cJSON_AddArrayToObject(root, "steps");
  for (const auto &s : steps) {
    cJSON *o = cJSON_CreateObject();
    cJSON_AddNumberToObject(o, "d", s.hold_ms);
    cJSON *b = cJSON_AddArrayToObject(o, "b");
    for (const auto &nb : kButtons) {
      if (s.buttons & nb.bit) cJSON_AddItemToArray(b, cJSON_CreateString(nb.name));
    }
    if (s.lx != MACRO_STICK_CENTER) cJSON_AddNumberToObject(o, "lx", s.lx);
    if (s.ly != MACRO_STICK_CENTER) cJSON_AddNumberToObject(o, "ly", s.ly);
    if (s.rx != MACRO_STICK_CENTER) cJSON_AddNumberToObject(o, "rx", s.rx);
    if (s.ry != MACRO_STICK_CENTER) cJSON_AddNumberToObject(o, "ry", s.ry);
    cJSON_AddItemToArray(arr, o);
  }

  char *out = cJSON_PrintUnformatted(root);
  std::string s = out ? out : "{}";
  if (out) cJSON_free(out);
  cJSON_Delete(root);
  return s;
}

} // namespace macro_json
