#include "posix_support.h"
#include "version.h"

#include <curl/curl.h>

#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <system_error>

namespace fs = std::filesystem;

namespace lootcli::posix
{

fs::path lootDataPath(const std::string& home, const std::string& xdgDataHome)
{
  if (home.empty()) {
    return {};
  }

  const auto flatpak = fs::path(home) / ".var/app/io.github.loot.loot/data/LOOT";
  std::error_code ec;
  if (fs::is_directory(flatpak, ec)) {
    return flatpak;
  }

  if (!xdgDataHome.empty() && fs::path(xdgDataHome).is_absolute()) {
    return fs::path(xdgDataHome) / "LOOT";
  }

  return fs::path(home) / ".local/share/LOOT";
}

fs::path lootDataPath()
{
  const char* home = std::getenv("HOME");
  const char* xdg  = std::getenv("XDG_DATA_HOME");
  return lootDataPath(home ? home : "", xdg ? xdg : "");
}

std::optional<std::string> downloadFile(const std::string& url, const fs::path& file)
{
  static const CURLcode initialised = curl_global_init(CURL_GLOBAL_DEFAULT);
  if (initialised != CURLE_OK) {
    return std::string("curl_global_init failed: ") + curl_easy_strerror(initialised);
  }

  const fs::path partial = file.string() + ".download";
  FILE* out              = std::fopen(partial.c_str(), "wb");
  if (!out) {
    const auto e = errno;
    return "cannot open " + partial.string() + ": " + std::strerror(e);
  }

  std::optional<std::string> error;
  char message[CURL_ERROR_SIZE] = {};
  long status                   = 0;

  if (CURL* curl = curl_easy_init()) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, out);  // the default callback is fwrite()
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_MAXREDIRS, 10L);
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT, 30L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, 60L);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, message);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "http,https,file");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "http,https");
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "lootcli/" LOOTCLI_VERSION_STRING);

    const CURLcode rc = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    curl_easy_cleanup(curl);

    if (rc != CURLE_OK) {
      error = message[0] ? std::string(message) : std::string(curl_easy_strerror(rc));
    } else if (status != 0 && status != 200) {
      // 0: a file:// URL, which has no status
      error = "HTTP " + std::to_string(status);
    }
  } else {
    error = "curl_easy_init failed";
  }

  if (std::fclose(out) != 0 && !error) {
    const auto e = errno;
    error        = "cannot write " + partial.string() + ": " + std::strerror(e);
  }

  std::error_code ec;
  if (!error) {
    fs::rename(partial, file, ec);
    if (ec) {
      error = "cannot replace " + file.string() + ": " + ec.message();
    }
  }

  if (error) {
    fs::remove(partial, ec);
  }

  return error;
}

}  // namespace lootcli::posix
