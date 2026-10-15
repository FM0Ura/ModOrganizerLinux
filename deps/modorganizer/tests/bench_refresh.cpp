// Benchmark of the refresh of the data tree (organizer_bench_refresh, built with
// MO2_BUILD_BENCHMARKS=ON).
//
//   organizer_bench_refresh <instance-dir> [repetitions=5] [threads=10]
//
// <instance-dir> is a synthetic instance made by benchmarks/gen_instance.py. The
// benchmark builds the DirectoryEntry of the mods of profiles/Default/modlist.txt in
// priority order the way DirectoryRefresher does (directoryrefresher.cpp): a root
// "data" with the game data directory as origin 0, then every mod as an origin of
// priority index + 1 through DirectoryEntry::addFromOrigin(walker, ...), each one on a
// thread of an env::ThreadPool (10 threads, the default of the refresh_thread_count
// setting), then FileRegister::sortOrigins() and the cleanup of cleanStructure(). Only
// the parts of DirectoryRefresher that need the application (the settings, the game
// plugin, mod info and archive parsing, which the synthetic instance has none of) are
// left out; everything that is timed is the same library code.
//
// One untimed run first (warms the page cache and the allocator), then the timed
// repetitions. Prints key=value lines; refresh_ms_median and peak_rss_kib are the two
// metrics of the baseline.

#include "envfs.h"
#include "shared/directoryentry.h"
#include "shared/fileentry.h"
#include "shared/fileregister.h"
#include "shared/filesorigin.h"

#include <sys/resource.h>

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <string>
#include <vector>

using namespace MOShared;

// the tests link the data tree without the rest of the application; these are defined
// in files that are not part of it (same as tests/test_main.cpp)

// main.cpp, called by startSafeThread()
void setExceptionHandlers() {}

// directoryrefresher.cpp
MOShared::DirectoryStats::DirectoryStats()
{
  std::memset(static_cast<void*>(this), 0, sizeof(DirectoryStats));
}

namespace
{

// same as ModThread in directoryrefresher.cpp, without the archives and the progress
struct ModThread
{
  DirectoryEntry* ds = nullptr;
  std::wstring modName;
  std::wstring path;
  int prio              = -1;
  DirectoryStats* stats = nullptr;
  env::DirectoryWalker walker;

  std::condition_variable cv;
  std::mutex mutex;
  bool ready = false;

  void wakeup()
  {
    {
      std::scoped_lock lock(mutex);
      ready = true;
    }

    cv.notify_one();
  }

  void run()
  {
    std::unique_lock lock(mutex);
    cv.wait(lock, [&] {
      return ready;
    });

    ds->addFromOrigin(walker, modName, path, prio, *stats);
    ready = false;
  }
};

struct Mod
{
  std::wstring name;
  std::wstring path;
  int priority;  // 0 = lowest
};

// profiles/Default/modlist.txt lists the enabled mods ("+name"), highest priority
// first, like the one Mod Organizer writes
std::vector<Mod> readMods(const std::filesystem::path& instance)
{
  std::ifstream in(instance / "profiles" / "Default" / "modlist.txt");
  if (!in) {
    std::fprintf(stderr, "cannot read %s\n",
                 (instance / "profiles/Default/modlist.txt").c_str());
    std::exit(2);
  }

  std::vector<std::string> names;
  for (std::string line; std::getline(in, line);) {
    if (!line.empty() && line.back() == '\r') {
      line.pop_back();
    }
    if (line.size() > 1 && line[0] == '+') {
      names.push_back(line.substr(1));
    }
  }

  std::vector<Mod> mods;
  for (std::size_t i = 0; i < names.size(); ++i) {
    const auto& name = names[names.size() - 1 - i];
    mods.push_back({std::filesystem::path(name).wstring(),
                    (instance / "mods" / name).wstring(), static_cast<int>(i)});
  }

  return mods;
}

// the same cleanup as DirectoryRefresher::cleanStructure()
void cleanStructure(DirectoryEntry* structure)
{
  structure->removeFile(L"meta.ini");
  structure->removeFile(L"readme.txt");
  structure->removeDir(std::wstring(L"fomod"));
}

struct Result
{
  double ms;
  std::size_t files;
  std::size_t origins;
};

// DirectoryRefresher::refresh() + addMultipleModsFilesToStructure()
Result refreshOnce(env::ThreadPool<ModThread>& threads,
                   const std::filesystem::path& dir, const std::vector<Mod>& mods,
                   std::unique_ptr<DirectoryEntry>& root)
{
  using clock = std::chrono::steady_clock;

  root.reset();  // free the previous tree before the timed part starts
  const auto start = clock::now();

  root.reset(new DirectoryEntry(L"data", nullptr, 0));

  {
    DirectoryStats dummy;
    root->addFromOrigin(L"data", (dir / "data").wstring(), 0, dummy);
  }

  std::vector<DirectoryStats> stats(mods.size());

  for (std::size_t i = 0; i < mods.size(); ++i) {
    auto& mt = threads.request();

    mt.ds      = root.get();
    mt.modName = mods[i].name;
    mt.path    = mods[i].path;
    mt.prio    = mods[i].priority + 1;
    mt.stats   = &stats[i];

    mt.wakeup();
  }

  threads.waitForAll();

  root->getFileRegister()->sortOrigins();
  cleanStructure(root.get());

  const auto end = clock::now();

  return {std::chrono::duration<double, std::milli>(end - start).count(),
          root->getFileRegister()->highestCount(), mods.size() + 1};
}

}  // namespace

int main(int argc, char** argv)
{
  if (argc < 2 || argc > 4) {
    std::fprintf(stderr, "usage: %s <instance-dir> [repetitions=5] [threads=10]\n",
                 argv[0]);
    return 2;
  }

  const std::filesystem::path dir = std::filesystem::absolute(argv[1]);
  const int reps                  = argc > 2 ? std::atoi(argv[2]) : 5;
  const std::size_t threadCount   = argc > 3 ? std::atoi(argv[3]) : 10;

  if (reps < 1 || threadCount < 1) {
    std::fprintf(stderr, "bad repetitions or thread count\n");
    return 2;
  }

  const auto mods = readMods(dir);
  if (mods.empty()) {
    std::fprintf(stderr, "no enabled mods in the modlist\n");
    return 2;
  }

  env::ThreadPool<ModThread> threads;
  threads.setMax(threadCount);

  std::unique_ptr<DirectoryEntry> root;

  const Result warm = refreshOnce(threads, dir, mods, root);

  std::vector<double> times;
  Result last = warm;
  for (int i = 0; i < reps; ++i) {
    last = refreshOnce(threads, dir, mods, root);
    times.push_back(last.ms);

    if (last.files != warm.files) {
      std::fprintf(stderr, "the file count changed between runs (%zu, %zu)\n",
                   warm.files, last.files);
      return 1;
    }
  }

  std::vector<double> sorted = times;
  std::sort(sorted.begin(), sorted.end());
  const double median =
      sorted.size() % 2
          ? sorted[sorted.size() / 2]
          : (sorted[sorted.size() / 2 - 1] + sorted[sorted.size() / 2]) / 2;

  rusage usage{};
  getrusage(RUSAGE_SELF, &usage);  // ru_maxrss is in KiB on Linux

  std::printf("mods=%zu\n", mods.size());
  std::printf("threads=%zu\n", threadCount);
  std::printf("files_registered=%zu\n", last.files);
  std::printf("repetitions=%d\n", reps);
  std::printf("first_run_ms=%.1f\n", warm.ms);
  std::printf("refresh_ms_all=");
  for (std::size_t i = 0; i < times.size(); ++i) {
    std::printf(i ? ",%.1f" : "%.1f", times[i]);
  }
  std::printf("\n");
  std::printf("refresh_ms_median=%.0f\n", median);
  std::printf("peak_rss_kib=%ld\n", usage.ru_maxrss);

  root.reset();
  return 0;
}
