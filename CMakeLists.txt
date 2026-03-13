cmake_minimum_required(VERSION 3.14)
project(TermiTrade VERSION 1.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ── Options ───────────────────────────────────
option(USE_NCURSES "Build with ncurses fallback renderer" ON)

# ── Dependencies via FetchContent ─────────────
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

# ── Optional: ncurses fallback ────────────────
if(USE_NCURSES)
  find_package(Curses QUIET)
  if(CURSES_FOUND)
    message(STATUS "ncurses found — building with fallback renderer")
  else()
    # Try pkgconfig on systems where CMake FindCurses doesn't work
    find_package(PkgConfig QUIET)
    if(PkgConfig_FOUND)
      pkg_check_modules(NCURSES QUIET ncurses)
    endif()
    if(NCURSES_FOUND)
      message(STATUS "ncurses found via pkg-config")
      set(CURSES_LIBRARIES ${NCURSES_LIBRARIES})
      set(CURSES_INCLUDE_DIRS ${NCURSES_INCLUDE_DIRS})
      set(CURSES_FOUND TRUE)
    else()
      message(STATUS "ncurses NOT found — fallback renderer disabled (USE_NCURSES=OFF to silence)")
    endif()
  endif()
endif()

# ── Build ─────────────────────────────────────
add_executable(termitrade src/main.cpp)

target_include_directories(termitrade PRIVATE include)

target_link_libraries(termitrade PRIVATE
  ftxui::screen
  ftxui::dom
  ftxui::component
  nlohmann_json::nlohmann_json
  CURL::libcurl
)

# Conditionally enable ncurses
if(USE_NCURSES AND CURSES_FOUND)
  target_compile_definitions(termitrade PRIVATE TERMITRADE_NCURSES=1)
  target_include_directories(termitrade PRIVATE ${CURSES_INCLUDE_DIRS})
  target_link_libraries(termitrade PRIVATE ${CURSES_LIBRARIES})
endif()

# ── Install ────────────────────────────────────
install(TARGETS termitrade DESTINATION bin)

# ── Copy example config on first build ────────
if(NOT EXISTS "${CMAKE_BINARY_DIR}/config.json")
  configure_file(
    "${CMAKE_SOURCE_DIR}/config.json.example"
    "${CMAKE_BINARY_DIR}/config.json"
    COPYONLY
  )
endif()

# ── Summary ────────────────────────────────────
message(STATUS "")
message(STATUS "  TermiTrade ${PROJECT_VERSION}")
message(STATUS "  ncurses fallback : ${CURSES_FOUND}")
message(STATUS "  Install prefix   : ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
