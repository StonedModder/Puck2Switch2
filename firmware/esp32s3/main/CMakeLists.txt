idf_component_register(
  SRCS "app_main.cpp" "sc2_central.cpp" "macro_engine.cpp" "macro_json.cpp"
       "macro_store.cpp" "web_server.cpp"
  INCLUDE_DIRS "."
  REQUIRES switch2_pro ble_gatt_server esp-nimble-cpp nvs base_component core
  PRIV_REQUIRES esp_timer esp_driver_gpio esp_wifi esp_netif esp_event
                esp_http_server spiffs cjson nvs_flash)
