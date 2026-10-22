add_executable(pvnc_viewer
  src/main.cpp
)
target_link_libraries(pvnc_viewer PRIVATE pvnc_protocol)
set_target_properties(pvnc_viewer PROPERTIES OUTPUT_NAME "pvnc-viewer")
