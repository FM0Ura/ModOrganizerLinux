#include "../src/linux/secretservice_linux.h"
#include "tempdir.h"

#include <gtest/gtest.h>

#include <QByteArray>
#include <string>
#include <unistd.h>

#ifdef MO2_WITH_LIBSECRET

namespace
{
// sets an environment variable for the lifetime of the object
struct ScopedEnv
{
  ScopedEnv(const char* name, const QByteArray& value)
      : m_name(name), m_had(qEnvironmentVariableIsSet(name)), m_old(qgetenv(name))
  {
    qputenv(name, value);
  }
  ~ScopedEnv()
  {
    if (m_had) {
      qputenv(m_name, m_old);
    } else {
      qunsetenv(m_name);
    }
  }
  const char* m_name;
  bool m_had;
  QByteArray m_old;
};
}  // namespace

// no session bus: the Secret Service is unavailable, not "failed" (spec §3.2)
TEST(SecretServiceUnavailable, NoSessionBus)
{
  TempDir runtime;
  const auto bus = "unix:path=" + (runtime.path() / "no-such-bus").string();
  ScopedEnv address("DBUS_SESSION_BUS_ADDRESS", QByteArray::fromStdString(bus));
  ScopedEnv xdg("XDG_RUNTIME_DIR", QByteArray::fromStdString(runtime.path().string()));
  ScopedEnv display("DISPLAY", "");

  const auto r = secretservice::lookup("ModOrganizer2_test");
  EXPECT_EQ(r.status, secretservice::Status::Unavailable) << r.error;
  EXPECT_FALSE(r.error.empty());
}

// against a real Secret Service: run by tests/secret-service.sh (ctest
// secret_service_integration), which starts a gnome-keyring-daemon of its own and sets
// MO2_TESTS_SECRET_SERVICE=1; skipped otherwise, so that the user's keyring is never
// touched
TEST(SecretServiceIntegration, RoundTrip)
{
  if (qgetenv("MO2_TESTS_SECRET_SERVICE") != "1") {
    GTEST_SKIP() << "run by tests/secret-service.sh";
  }

  const std::string name = "ModOrganizer2_test_" + std::to_string(::getpid());

  // like the OAuth tokens: JSON with quotes, plus non-ASCII, about 4 KB
  std::string value = R"({"access_token":"çãø🔑","refresh_token":")";
  value += std::string(4000, 'x');
  value += R"("})";

  auto r = secretservice::lookup(name);
  EXPECT_EQ(r.status, secretservice::Status::NotFound) << r.error;

  r = secretservice::store(name, "Mod Organizer 2: " + name, value);
  ASSERT_EQ(r.status, secretservice::Status::Ok) << r.error;

  r = secretservice::lookup(name);
  ASSERT_EQ(r.status, secretservice::Status::Ok) << r.error;
  EXPECT_EQ(r.value, value);

  // storing again replaces the value
  r = secretservice::store(name, "Mod Organizer 2: " + name, "second");
  ASSERT_EQ(r.status, secretservice::Status::Ok) << r.error;
  EXPECT_EQ(secretservice::lookup(name).value, "second");

  r = secretservice::clear(name);
  EXPECT_EQ(r.status, secretservice::Status::Ok) << r.error;
  EXPECT_EQ(secretservice::lookup(name).status, secretservice::Status::NotFound);

  // clearing what does not exist is not an error
  EXPECT_EQ(secretservice::clear(name).status, secretservice::Status::NotFound);
}

#endif  // MO2_WITH_LIBSECRET
