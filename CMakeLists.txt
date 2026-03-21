cmake_minimum_required(VERSION 3.20)
project(meridian-dns
  VERSION 1.1.0
  LANGUAGES CXX
  DESCRIPTION "Meridian DNS — Multi-Provider DNS Management"
)

# ── C++20 Standard ──────────────────────────────────────────────────────────
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# ── Compiler warnings ──────────────────────────────────────────────────────
add_compile_options(-Wall -Wextra -Wpedantic -Werror)

# ── Export compile_commands.json for clangd / IDE integration ──────────────
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# ── Version header generation ──────────────────────────────────────────────
configure_file(
  ${CMAKE_SOURCE_DIR}/include/common/Version.hpp.in
  ${CMAKE_BINARY_DIR}/generated/common/Version.hpp
)

# ── Dependency discovery ───────────────────────────────────────────────────
find_package(OpenSSL REQUIRED)
find_package(nlohmann_json REQUIRED)
find_package(spdlog REQUIRED)

include(FindPkgConfig)
pkg_check_modules(LIBPQXX REQUIRED IMPORTED_TARGET libpqxx)
pkg_check_modules(LIBGIT2 REQUIRED IMPORTED_TARGET libgit2)

# ── Auth dependencies (lasso / jansson / libcurl / pcre2 via system pkg-config)
pkg_check_modules(LASSO   REQUIRED IMPORTED_TARGET lasso)
pkg_check_modules(GLIB2   REQUIRED IMPORTED_TARGET glib-2.0)
pkg_check_modules(GOBJECT REQUIRED IMPORTED_TARGET gobject-2.0)
pkg_check_modules(JANSSON REQUIRED IMPORTED_TARGET jansson)
pkg_check_modules(LIBCURL REQUIRED IMPORTED_TARGET libcurl)
pkg_check_modules(PCRE2   REQUIRED IMPORTED_TARGET libpcre2-8)

# ── cjose + liboauth2 (autotools, built from source) ─────────────────────
include(ExternalProject)

set(EP_INSTALL_DIR "${CMAKE_BINARY_DIR}/ep_install")

ExternalProject_Add(cjose_ep
  GIT_REPOSITORY https://github.com/OpenIDC/cjose.git
  GIT_TAG        v0.6.2.3
  GIT_SHALLOW    TRUE
  BUILD_IN_SOURCE TRUE
  CONFIGURE_COMMAND autoreconf -fi
             COMMAND ./configure --prefix=${EP_INSTALL_DIR} CFLAGS=-fPIC
  BUILD_COMMAND     make -j4
  INSTALL_COMMAND   make install
  BUILD_BYPRODUCTS "${EP_INSTALL_DIR}/lib/libcjose.a"
)

ExternalProject_Add(liboauth2_ep
  GIT_REPOSITORY https://github.com/OpenIDC/liboauth2.git
  GIT_TAG        v2.1.0
  GIT_SHALLOW    TRUE
  BUILD_IN_SOURCE TRUE
  CONFIGURE_COMMAND autoreconf -fi
             COMMAND ./configure --prefix=${EP_INSTALL_DIR}
                     PKG_CONFIG_PATH=${EP_INSTALL_DIR}/lib/pkgconfig:${EP_INSTALL_DIR}/lib64/pkgconfig
                     CFLAGS=-fPIC
  BUILD_COMMAND     make -j4
  INSTALL_COMMAND   make install
  DEPENDS cjose_ep
  BUILD_BYPRODUCTS "${EP_INSTALL_DIR}/lib/liboauth2.a"
)

# Pre-create the install include directory so CMake can validate
# INTERFACE_INCLUDE_DIRECTORIES at generation time (ExternalProject
# populates it at build time).
file(MAKE_DIRECTORY "${EP_INSTALL_DIR}/include")

# Imported targets for linking.
# BUILD_BYPRODUCTS above tells Ninja that the EP step produces these .a
# files, so the IMPORTED_LOCATION is resolvable in the build graph.
add_library(cjose STATIC IMPORTED GLOBAL)
set_target_properties(cjose PROPERTIES
  IMPORTED_LOCATION "${EP_INSTALL_DIR}/lib/libcjose.a"
  INTERFACE_INCLUDE_DIRECTORIES "${EP_INSTALL_DIR}/include"
)
add_dependencies(cjose cjose_ep)

add_library(oauth2 STATIC IMPORTED GLOBAL)
set_target_properties(oauth2 PROPERTIES
  IMPORTED_LOCATION "${EP_INSTALL_DIR}/lib/liboauth2.a"
  INTERFACE_INCLUDE_DIRECTORIES "${EP_INSTALL_DIR}/include"
)
add_dependencies(oauth2 liboauth2_ep)

# ── Crow (HTTP server) ──────────────────────────────────────────────────────
# Header-only; fetched at configure time — no system package required.
include(FetchContent)
FetchContent_Declare(
  Crow
  GIT_REPOSITORY https://github.com/CrowCpp/Crow.git
  GIT_TAG        v1.3.1
  GIT_SHALLOW    TRUE
)
set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS    OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(Crow)

# ── cpp-httplib (HTTP client for provider REST calls) ────────────────────
FetchContent_Declare(httplib
  GIT_REPOSITORY https://github.com/yhirose/cpp-httplib.git
  GIT_TAG        v0.18.7
  GIT_SHALLOW    TRUE
)
set(HTTPLIB_REQUIRE_OPENSSL ON CACHE BOOL "" FORCE)
set(HTTPLIB_COMPILE OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(httplib)

# ── UI build (optional) ───────────────────────────────────────────────────
option(BUILD_UI "Build the Vue 3 web UI (requires Node.js)" ON)
if(BUILD_UI)
  find_program(NPM_EXECUTABLE npm REQUIRED)
  set(UI_SOURCE_DIR "${CMAKE_SOURCE_DIR}/ui")
  set(UI_BUILD_DIR "${UI_SOURCE_DIR}/dist")
  add_custom_target(ui-build ALL
    COMMAND ${NPM_EXECUTABLE} ci
    COMMAND ${NPM_EXECUTABLE} run build
    WORKING_DIRECTORY ${UI_SOURCE_DIR}
    COMMENT "Building Vue 3 web UI"
  )
  install(DIRECTORY ${UI_BUILD_DIR}/ DESTINATION share/meridian-dns/ui)
endif()

# ── Subdirectories ─────────────────────────────────────────────────────────
add_subdirectory(src)
add_subdirectory(tests)
