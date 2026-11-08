# Vendored cJSON (MIT, see LICENSE) — ESP-IDF 6.x no longer bundles a JSON
# component, and the macro editor needs one for the REST API and macro files.
idf_component_register(SRCS "cJSON.c" INCLUDE_DIRS ".")
