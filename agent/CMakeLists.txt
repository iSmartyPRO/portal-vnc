find_package(nlohmann_json CONFIG REQUIRED)

# Core agent logic as a static lib so tests can link it.
add_library(pvnc_agent_core STATIC
  src/config.cpp
  src/paths.cpp
  src/capture_gdi.cpp
  src/capture_hub.cpp
  src/input.cpp
  src/rfb_server.cpp
  src/service.cpp
  src/tray.cpp
)
target_include_directories(pvnc_agent_core PUBLIC src)
target_link_libraries(pvnc_agent_core
  PUBLIC  pvnc_protocol
  PRIVATE nlohmann_json::nlohmann_json ws2_32 gdi32 user32 advapi32
          wtsapi32 userenv shell32
)

add_executable(pvnc_agent
  src/main.cpp
  src/pvnc_agent.rc
)
target_link_libraries(pvnc_agent PRIVATE pvnc_agent_core)
set_target_properties(pvnc_agent PROPERTIES OUTPUT_NAME "pvnc-agent")

# ---- tests ----
find_package(Catch2 3 CONFIG REQUIRED)
add_executable(pvnc_agent_tests
  test/test_config.cpp
  test/test_capture.cpp
  test/test_rfb_server.cpp
  test/test_integration.cpp
)
target_link_libraries(pvnc_agent_tests PRIVATE pvnc_agent_core Catch2::Catch2WithMain ws2_32)
add_test(NAME agent COMMAND pvnc_agent_tests)
