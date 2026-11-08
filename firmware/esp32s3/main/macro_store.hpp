// macro_store.hpp — macros persisted as one JSON file each on SPIFFS.
//
// One file per macro keeps save/delete atomic and lets a macro be copied off the
// device with nothing more than a GET. The partition is separate from NVS, so
// writing macros can never disturb the BLE bonds.
#pragma once

#include <string>

namespace macro_store {

// Mount the "storage" partition. Returns false if it cannot be mounted or
// formatted.
bool init();

// JSON array of {id,name,game,steps,ms} for the library view.
bool list(std::string &json_out);

bool load(const std::string &id, std::string &out);
bool save(const std::string &id, const std::string &body, std::string &err_out,
          std::string &saved_id);
bool remove(const std::string &id);

} // namespace macro_store
