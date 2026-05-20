cmake_minimum_required(VERSION 3.16)
project(sas-secure-messaging VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wall -Wextra -O2")

find_package(CURL REQUIRED)
find_package(SQLite3 REQUIRED) #SQLite3 for local cache for user. I chose it over MySql for fast lookups, offline capability, etc.

# libsodium does not ship a CMake config package; locate it via find_library/find_path
find_library(SODIUM_LIB NAMES sodium libsodium REQUIRED)
find_path(SODIUM_INCLUDE NAMES sodium.h PATH_SUFFIXES sodium REQUIRED)

include_directories(include ${SODIUM_INCLUDE})

add_executable(sas-client
  src/main.cpp
  src/User.cpp
  src/Message.cpp
  src/MessageStore.cpp
  src/Conversation.cpp
  src/Client.cpp
)

target_link_libraries(sas-client PRIVATE
  CURL::libcurl
  ${SODIUM_LIB}
  SQLite::SQLite3
)

add_executable(sas-tests
  tests/test_main.cpp
  tests/test_message.cpp
  tests/test_user.cpp
  tests/test_store.cpp
  src/User.cpp
  src/Message.cpp
  src/MessageStore.cpp
  src/Conversation.cpp
)

target_include_directories(sas-tests PRIVATE include tests)

target_link_libraries(sas-tests PRIVATE
  ${SODIUM_LIB}
  SQLite::SQLite3
)