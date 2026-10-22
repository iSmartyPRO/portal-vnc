add_executable(pvnc_viewer
  src/main.cpp
  src/rfb_client.cpp
  src/window.cpp
)
target_include_directories(pvnc_viewer PRIVATE src)
target_link_libraries(pvnc_viewer PRIVATE pvnc_protocol ws2_32 gdi32 user32)
set_target_properties(pvnc_viewer PROPERTIES OUTPUT_NAME "pvnc-viewer")
