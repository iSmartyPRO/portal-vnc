cmake_minimum_required(VERSION 3.25)
project(PortalVNC CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
  add_compile_options(/W4 /permissive- /Zc:__cplusplus /EHsc)
  add_compile_definitions(_WIN32_WINNT=0x0A00 WIN32_LEAN_AND_MEAN NOMINMAX UNICODE _UNICODE)
endif()

enable_testing()

add_subdirectory(protocol)
add_subdirectory(agent)
add_subdirectory(viewer)
