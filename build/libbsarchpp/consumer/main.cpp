// Creates a Skyrim SE archive with one file in a temporary directory and reads it back
// (build/libbsarchpp/build-libbsarchpp.sh runs it); prints "ok" on success.
#include <libbsarchpp/Bsa.h>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

int consumer_run(const fs::path& dir)
{
  const fs::path root = dir / "in";
  fs::create_directories(root / "textures");
  const std::string content(4096, 'x');
  std::ofstream(root / "textures" / "a.dds", std::ios::binary) << content;

  std::vector<fs::path> files{"textures/a.dds"};
  const fs::path archive = dir / "out.bsa";
  {
    libbsarchpp::Bsa bsa(archive, libbsarchpp::SSE, files, std::nullopt, true, false,
                         false);
    bsa.addFile(root, root / "textures" / "a.dds");
    bsa.save();
  }
  libbsarchpp::Bsa reopened(archive);
  const auto data = reopened.extractFileData(fs::path("textures/a.dds"));
  if (std::string(data.begin(), data.end()) != content) {
    std::cerr << "content differs\n";
    return 1;
  }
  std::cout << "ok\n";
  return 0;
}

#ifdef CONSUMER_MAIN
int main(int argc, char** argv)
{
  if (argc != 2) {
    std::cerr << "usage: consumer <scratch directory>\n";
    return 2;
  }
  try {
    return consumer_run(argv[1]);
  } catch (const std::exception& e) {
    std::cerr << e.what() << '\n';
    return 1;
  }
}
#endif
