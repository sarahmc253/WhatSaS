cmake_minimum_required(VERSION 3.16)
project(sas-secure-messaging VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(MSVC)
  add_compile_options(
    /W4
    $<$<CONFIG:Debug>:/fsanitize=address>
  )
  add_link_options($<$<CONFIG:Debug>:/fsanitize=address>)
else()
  add_compile_options(
    -Wall -Wextra
    $<$<CONFIG:Release>:-O2>
    $<$<CONFIG:Debug>:-g>
    $<$<CONFIG:Debug>:-fsanitize=address>
    $<$<CONFIG:Debug>:-fno-omit-frame-pointer>
  )
  add_link_options($<$<CONFIG:Debug>:-fsanitize=address>)
endif()
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(OpenSSL REQUIRED)
find_package(SQLite3 REQUIRED) #SQLite3 for local cache for user. I chose it over MySql for fast lookups, offline capability, etc.
include(FetchContent)
FetchContent_Declare(
  nlohmann_json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# libsodium does not ship a CMake config package; locate it via find_library/find_path
find_library(SODIUM_LIB NAMES sodium libsodium REQUIRED)
find_path(SODIUM_INCLUDE NAMES sodium.h
  PATHS /usr/include /usr/local/include /opt/homebrew/include
  REQUIRED
)

# Windows socket libraries (Winsock2, CryptoAPI for system cert store)
if(WIN32)
  set(PLATFORM_LIBS ws2_32 crypt32)
  # SODIUM_STATIC suppresses __declspec(dllimport) when linking the static .lib.
  # Global definition ensures no target misses it; per-target definitions below
  # provide an explicit belt-and-braces guarantee.
  add_compile_definitions(SODIUM_STATIC)
endif()

include_directories(include)

add_executable(sas-client
  src/main.cpp
  src/Auth.cpp
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
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(sas-client PRIVATE ${SODIUM_INCLUDE})

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
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(test_tcp PRIVATE ${SODIUM_INCLUDE})
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
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(test_http PRIVATE ${SODIUM_INCLUDE})
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
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(test_client PRIVATE ${SODIUM_INCLUDE})
add_test(NAME Client_Crypto_Test COMMAND test_client)

add_executable(test_conversation
  tests/test_conversation.cpp
  src/Message.cpp
  src/MessageStore.cpp
  src/Conversation.cpp
  src/Client.cpp
  src/HttpClient.cpp
  src/tls_connect.cpp
  src/http_response.cpp
  src/User.cpp
)
target_link_libraries(test_conversation PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(test_conversation PRIVATE ${SODIUM_INCLUDE})
add_test(NAME Conversation_Test COMMAND test_conversation)

add_executable(test_hpke
  tests/test_hpke.cpp
  src/User.cpp
  src/Message.cpp
  src/MessageStore.cpp
  src/Conversation.cpp
  src/Client.cpp
  src/HttpClient.cpp
  src/tls_connect.cpp
  src/http_response.cpp
)
target_link_libraries(test_hpke PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(test_hpke PRIVATE ${SODIUM_INCLUDE})
add_test(NAME HPKE_Test COMMAND test_hpke)

add_executable(test_tamper
  tests/test_tamper.cpp
)
target_link_libraries(test_tamper PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(test_tamper PRIVATE ${SODIUM_INCLUDE})
add_test(NAME Tamper_Test COMMAND test_tamper)

add_executable(test_e2e
  tests/test_e2e.cpp
  src/Message.cpp
  src/MessageStore.cpp
  src/Conversation.cpp
)
target_link_libraries(test_e2e PRIVATE
  OpenSSL::SSL
  OpenSSL::Crypto
  ${SODIUM_LIB}
  SQLite::SQLite3
  nlohmann_json::nlohmann_json
  ${PLATFORM_LIBS}
)
target_include_directories(test_e2e PRIVATE ${SODIUM_INCLUDE})
add_test(NAME E2E_Crypto_Test COMMAND test_e2e)

if(WIN32)
  foreach(T sas-client test_tcp test_http test_client test_conversation test_hpke test_tamper test_e2e)
    target_compile_definitions(${T} PRIVATE SODIUM_STATIC)
  endforeach()
endif()
