find_package(MbedTLS CONFIG REQUIRED)
find_package(nlohmann_json CONFIG REQUIRED)

# Admin logic as a static lib so tests can link it (GUI-free).
add_library(pvnc_admin_core STATIC
  src/ca.cpp
  src/keyvault.cpp
  src/configedit.cpp
  src/agentlocate.cpp
  src/picker.cpp
  src/fileio.cpp
)
target_include_directories(pvnc_admin_core PUBLIC src)
target_link_libraries(pvnc_admin_core
  PUBLIC  MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto
          pvnc_agent_core  # SvcState/queryService + SessionInfo (service.h, sessions.h)
  PRIVATE nlohmann_json::nlohmann_json advapi32 version ole32 oleaut32
)

# ---- GUI executable ----
add_executable(pvnc_admin
  src/main.cpp
  src/ui_common.cpp
  src/ui_ca.cpp
  src/ui_agent.cpp
  src/pvnc_admin.rc
)
target_link_libraries(pvnc_admin PRIVATE pvnc_admin_core
  user32 gdi32 advapi32 comctl32 comdlg32 shell32 ole32 oleaut32 dwmapi version ws2_32)
set_target_properties(pvnc_admin PROPERTIES OUTPUT_NAME "pvnc-admin" WIN32_EXECUTABLE TRUE)
target_compile_definitions(pvnc_admin PRIVATE
  "PVNC_RC_VER=${PVNC_VERSION_COMMA}"
  "PVNC_RC_STR=\"${PVNC_VERSION}\"")

# ---- tests ----
find_package(Catch2 3 CONFIG REQUIRED)
add_executable(pvnc_admin_tests
  test/test_ca.cpp
  test/test_keyvault.cpp
  test/test_configedit.cpp
  test/test_agentlocate.cpp
)
target_link_libraries(pvnc_admin_tests PRIVATE pvnc_admin_core Catch2::Catch2WithMain
        nlohmann_json::nlohmann_json)
add_test(NAME admin COMMAND pvnc_admin_tests)
