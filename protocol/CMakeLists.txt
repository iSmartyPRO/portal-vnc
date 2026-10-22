find_package(MbedTLS CONFIG REQUIRED)

add_library(pvnc_protocol STATIC
  src/protocol.cpp
  src/tls.cpp
  src/handshake.cpp
  src/auth.cpp
)
target_include_directories(pvnc_protocol PUBLIC include)
target_link_libraries(pvnc_protocol
  PUBLIC  MbedTLS::mbedtls MbedTLS::mbedx509 MbedTLS::mbedcrypto
  PRIVATE ws2_32 secur32
)

# ---- tests ----
find_package(Catch2 3 CONFIG REQUIRED)
add_executable(pvnc_protocol_tests
  test/test_protocol.cpp
  test/test_tls.cpp
  test/test_handshake.cpp
  test/test_auth.cpp
)
target_link_libraries(pvnc_protocol_tests PRIVATE pvnc_protocol Catch2::Catch2WithMain ws2_32)
add_test(NAME protocol COMMAND pvnc_protocol_tests)
