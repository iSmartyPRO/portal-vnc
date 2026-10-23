find_package(nlohmann_json CONFIG REQUIRED)

add_executable(pvnc_viewer
  src/main.cpp
  src/rfb_client.cpp
  src/window.cpp
  src/connect_dialog.cpp
  src/sysinfo_dialog.cpp
  src/files_dialog.cpp
  src/pvnc_viewer.rc
)
target_include_directories(pvnc_viewer PRIVATE src)
target_link_libraries(pvnc_viewer PRIVATE pvnc_protocol nlohmann_json::nlohmann_json
  ws2_32 gdi32 user32 advapi32 shell32 comctl32 msimg32)
set_target_properties(pvnc_viewer PROPERTIES OUTPUT_NAME "pvnc-viewer" WIN32_EXECUTABLE TRUE)
