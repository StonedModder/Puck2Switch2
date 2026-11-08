idf_component_register(
  SRCS "app_main.cpp" "sc2_central.cpp"
  INCLUDE_DIRS "."
  REQUIRES switch2_pro ble_gatt_server esp-nimble-cpp nvs base_component core
  PRIV_REQUIRES esp_timer esp_driver_gpio)
