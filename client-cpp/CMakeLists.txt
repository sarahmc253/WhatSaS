cmake_minimum_required(VERSION 3.16)
project(sas-secure-messaging VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED) #SQLite3 for local cache for user. I chose it over MySql for fast lookups, offline capability, etc.

# libsodium does not ship a CMake config package; locate it via find_library/find_path
find_library(SODIUM_LIB NAMES sodium libsodium REQUIRED)
find_path(SODIUM_INCLUDE NAMES sodium.h PATH_SUFFIXES sodium REQUIRED)

# Windows socket libraries (Winsock2, CryptoAPI for system cert store)
if(WIN32)
  set(PLATFORM_LIBS ws2_32 crypt32)
endif()

include_directories(include ${SODIUM_INCLUDE})

add_executable(sas-client
  src/main.cpp
  src/User.cpp
  src/Message.cpp
  src/MessageStore.cpp
  src/Conversation.cpp
  src/Client.cpp
  src/HttpClient.cpp
  src/tls_connect.cpp
  src/http_response.cpp
)

target_link_libraries(sas-client PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  ${PLATFORM_LIBS}
)

# Tests executable for TCP socket testing
enable_testing()
add_executable(test_tcp
  tests/test_tcp.cpp
  src/HttpClient.cpp
  src/tls_connect.cpp
  src/http_response.cpp
)
target_link_libraries(test_tcp PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  ${PLATFORM_LIBS}
)
add_test(NAME TCP_Socket_Test COMMAND test_tcp)

add_executable(test_http
  tests/test_http.cpp
  src/HttpClient.cpp
  src/tls_connect.cpp
  src/http_response.cpp
)
target_link_libraries(test_http PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  ${PLATFORM_LIBS}
)
# Network tests are opt-in — run manually with: ctest -L network
# or: ./build/test_http.exe --network
add_test(NAME HTTP_GET_Test COMMAND test_http)
set_tests_properties(HTTP_GET_Test PROPERTIES LABELS "network")

add_executable(test_client
  tests/test_client.cpp
  src/Client.cpp
  src/HttpClient.cpp
  src/tls_connect.cpp
  src/http_response.cpp
  src/User.cpp
  src/Message.cpp
  src/MessageStore.cpp
  src/Conversation.cpp
)
target_link_libraries(test_client PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  ${PLATFORM_LIBS}
)
add_test(NAME Client_Crypto_Test COMMAND test_client)
