// macro_json.hpp — the on-wire/on-disk macro format.
//
// Deliberately human-readable: buttons are names, not bitmasks, so a macro file
// can be edited by hand, diffed in git, and imported by other tools.
//
//   {
//     "v": 1,
//     "id": "sword-dupe",
//     "name": "Sword dupe",
//     "game": "Tears of the Kingdom",
//     "author": "you",
//     "created": 1794123646,
//     "steps": [
//       {"d": 80,  "b": ["A"]},
//       {"d": 40,  "b": []},
//       {"d": 120, "b": ["ZL"], "lx": 1024, "ly": 2048, "rx": 2048, "ry": 2048}
//     ]
//   }
#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "macro_core.h"

namespace macro_json {

struct Meta {
  std::string id;
  std::string name = "Untitled macro";
  std::string game;
  std::string author;
  uint32_t created = 0;
};

// Parse a macro document. On failure returns false and fills `err`.
bool parse(const std::string &json, Meta &meta, std::vector<s2s::MacroStep> &steps,
           std::string &err);

// Serialise a macro document.
std::string write(const Meta &meta, const std::vector<s2s::MacroStep> &steps);

// Button name <-> bit helpers (shared with the editor's expectations).
const char *button_name(uint32_t bit);
bool button_from_name(const std::string &name, uint32_t &bit);

// Turn a display name into a filesystem-safe id.
std::string slugify(const std::string &name);

} // namespace macro_json
