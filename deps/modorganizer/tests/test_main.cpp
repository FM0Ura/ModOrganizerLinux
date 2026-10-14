#include <gtest/gtest.h>

#include <QCoreApplication>

#include <cstring>

#include <uibase/log.h>

#include "shared/fileregisterfwd.h"

// the tests link the data tree without the rest of the application; these are defined
// in files that are not part of them

// main.cpp, called by startSafeThread()
void setExceptionHandlers() {}

// directoryrefresher.cpp, same as there
MOShared::DirectoryStats::DirectoryStats()
{
  std::memset(static_cast<void*>(this), 0, sizeof(DirectoryStats));
}

int main(int argc, char** argv)
{
  QCoreApplication app(argc, argv);

  // the code under test logs warnings and errors (unreadable directories, ...)
  MOBase::log::LoggerConfiguration conf;
  conf.name     = "organizer_tests";
  conf.maxLevel = MOBase::log::Debug;
  conf.pattern  = "%^[%Y-%m-%d %H:%M:%S.%e %L] %v%$";
  MOBase::log::createDefault(conf);

  testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
