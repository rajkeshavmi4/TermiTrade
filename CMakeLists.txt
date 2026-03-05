cmake_minimum_required(VERSION 3.14)
project(TermiTrade VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Dependencies via FetchContent ──
include(FetchContent)

FetchContent_Declare(ftxui
  GIT_REPOSITORY https://github.com/ArthurSonzogni/FTXUI
  GIT_TAG        v5.0.0
)
FetchContent_Declare(json
  GIT_REPOSITORY https://github.com/nlohmann/json.git
  GIT_TAG        v3.11.3
)
FetchContent_MakeAvailable(ftxui json)

find_package(CURL REQUIRED)

# ── Build ──
add_executable(termitrade src/main.cpp)

target_include_directories(termitrade PRIVATE include)

target_link_libraries(termitrade PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  nlohmann_json::nlohmann_json
  CURL::libcurl
)

# ── Install ──
install(TARGETS termitrade DESTINATION bin)
