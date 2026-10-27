#include "gtest/gtest.h"

#include <uibase/log.h>

namespace
{
    // the runner logs (e.g., when unloading plugins), so the tests need the default
    // logger of Mod Organizer, like test_sibling.cpp and test_failure.cpp
    class LoggerEnvironment : public ::testing::Environment {
    public:
        void SetUp() override
        {
            MOBase::log::LoggerConfiguration conf;
            conf.name     = "runner-tests";
            conf.maxLevel = MOBase::log::Debug;
            conf.pattern  = "%v";
            MOBase::log::createDefault(conf);
        }
    };

    [[maybe_unused]] const auto* const loggerEnvironment =
        ::testing::AddGlobalTestEnvironment(new LoggerEnvironment);
}  // namespace
