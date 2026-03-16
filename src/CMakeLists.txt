# ── meridian-core static library ─────────────────────────────────────────────
# All .cpp files under src/ except main.cpp
file(GLOB_RECURSE DNS_CORE_SOURCES
  "${CMAKE_CURRENT_SOURCE_DIR}/common/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/api/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/core/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/providers/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/dal/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/gitops/*.cpp"
  "${CMAKE_CURRENT_SOURCE_DIR}/security/*.cpp"
)

add_library(meridian-core STATIC ${DNS_CORE_SOURCES})

target_include_directories(meridian-core PUBLIC
  "${PROJECT_SOURCE_DIR}/include"
  "${CMAKE_BINARY_DIR}/generated"
)

target_link_libraries(meridian-core PUBLIC
  PkgConfig::LIBPQXX
  OpenSSL::SSL
  OpenSSL::Crypto
  nlohmann_json::nlohmann_json
  spdlog::spdlog
  PkgConfig::LIBGIT2
  Crow::Crow
  httplib::httplib
  PkgConfig::LASSO
  PkgConfig::GLIB2
  PkgConfig::GOBJECT
  PkgConfig::JANSSON
  PkgConfig::LIBCURL
  PkgConfig::PCRE2
  oauth2
  cjose
)

# ── meridian-dns executable ──────────────────────────────────────────────────
add_executable(meridian-dns main.cpp)

target_link_libraries(meridian-dns PRIVATE
  meridian-core
)
