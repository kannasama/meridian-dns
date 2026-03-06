#include "api/StaticFileHandler.hpp"

#include <filesystem>
#include <fstream>
#include <sstream>

#include <spdlog/spdlog.h>

namespace fs = std::filesystem;

namespace dns::api {

StaticFileHandler::StaticFileHandler(const std::string& sUiDir)
    : _sUiDir(sUiDir) {}

void StaticFileHandler::setSetupToken(const std::string& sToken) {
  _sSetupToken = sToken;
}

void StaticFileHandler::registerRoutes(crow::SimpleApp& app) {
  if (_sUiDir.empty() || !fs::is_directory(_sUiDir)) {
    spdlog::info("UI directory not configured or not found — static serving disabled");
    return;
  }

  spdlog::info("Serving static UI files from {}", _sUiDir);

  // Crow catchall route — serves static files or falls back to index.html for SPA routing.
  // Uses (request, response&) void signature so the wrapper does NOT call res.end().
  // Router::handle() calls res.end() exactly once after this handler returns (routing.h:1714).
  // Calling res.end() here would double-end, corrupting keep-alive connection state.
  CROW_CATCHALL_ROUTE(app)
  ([this](const crow::request& req, crow::response& res) {
    std::string sPath = req.url;

    // Skip API routes — leave the 404 code set by Router::find_route
    if (sPath.rfind("/api/", 0) == 0) {
      return;
    }

    // Remove leading slash and try to serve the file
    if (!sPath.empty() && sPath[0] == '/') {
      sPath = sPath.substr(1);
    }

    // Try the exact file path
    std::string sFullPath = _sUiDir + "/" + sPath;
    if (!sPath.empty() && fs::is_regular_file(sFullPath)) {
      res.code = 200;
      res.body = readFile(sFullPath);
      res.set_header("Content-Type", mimeType(sFullPath));
      return;
    }

    // SPA fallback: serve index.html
    std::string sIndexPath = _sUiDir + "/index.html";
    if (fs::is_regular_file(sIndexPath)) {
      auto sContent = readFile(sIndexPath);

      // Inject setup token into index.html when setup mode is active
      if (!_sSetupToken.empty()) {
        auto nPos = sContent.find("</head>");
        if (nPos != std::string::npos) {
          std::string sScript = "<script>window.__SETUP_TOKEN__=\"" +
                                _sSetupToken + "\";</script>";
          sContent.insert(nPos, sScript);
        }
      }

      res.code = 200;
      res.body = std::move(sContent);
      res.set_header("Content-Type", "text/html; charset=utf-8");
      return;
    }

    // File not found — leave 404 from Router::find_route
  });
}

std::string StaticFileHandler::readFile(const std::string& sPath) {
  std::ifstream ifs(sPath, std::ios::binary);
  if (!ifs) return {};
  std::ostringstream oss;
  oss << ifs.rdbuf();
  return oss.str();
}

std::string StaticFileHandler::mimeType(const std::string& sPath) {
  auto sExt = fs::path(sPath).extension().string();
  if (sExt == ".html") return "text/html; charset=utf-8";
  if (sExt == ".css") return "text/css; charset=utf-8";
  if (sExt == ".js") return "application/javascript; charset=utf-8";
  if (sExt == ".json") return "application/json; charset=utf-8";
  if (sExt == ".svg") return "image/svg+xml";
  if (sExt == ".png") return "image/png";
  if (sExt == ".ico") return "image/x-icon";
  if (sExt == ".woff") return "font/woff";
  if (sExt == ".woff2") return "font/woff2";
  if (sExt == ".ttf") return "font/ttf";
  return "application/octet-stream";
}

}  // namespace dns::api
