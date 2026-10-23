find_package(MbedTLS CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Admin logic as a static lib so tests can link it (GUI-free).
add_library(pvnc_admin_core STATIC
  src/ca.cpp
  src/keyvault.cpp
  src/configedit.cpp
)
target_include_directories(pvnc_admin_core PUBLIC src)
target_link_libraries(pvnc_admin_core
  PUBLIC  MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto
  PRIVATE nlohmann_json::nlohmann_json advapi32
)

# ---- tests ----
find_package(Catch2 3 CONFIG REQUIRED)
add_executable(pvnc_admin_tests
  test/test_ca.cpp
  test/test_keyvault.cpp
  test/test_configedit.cpp
)
target_link_libraries(pvnc_admin_tests PRIVATE pvnc_admin_core Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json)
add_test(NAME admin COMMAND pvnc_admin_tests)
