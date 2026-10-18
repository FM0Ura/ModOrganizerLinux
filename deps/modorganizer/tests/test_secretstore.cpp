#include "../src/linux/secretstore_linux.h"

#include <gtest/gtest.h>

#include <map>

using secretservice::Reply;
using secretservice::Status;

namespace
{
// what the fake Secret Service holds and how it answers; the store owns the backend,
// the test keeps this
struct FakeState
{
  std::map<QString, QString> values;
  // anything but Ok: every call answers with this status
  Status forced = Status::Ok;
  int calls     = 0;
};

class FakeBackend : public secretstore::Backend
{
public:
  explicit FakeBackend(FakeState& s) : m_s(s) {}

  Reply lookup(const QString& name) override
  {
    ++m_s.calls;
    if (m_s.forced != Status::Ok) {
      return {m_s.forced, {}, "forced"};
    }
    const auto it = m_s.values.find(name);
    if (it == m_s.values.end()) {
      return {Status::NotFound, {}, {}};
    }
    return {Status::Ok, it->second.toStdString(), {}};
  }

  Reply store(const QString& name, const QString& value) override
  {
    ++m_s.calls;
    if (m_s.forced != Status::Ok) {
      return {m_s.forced, {}, "forced"};
    }
    m_s.values[name] = value;
    return {Status::Ok, {}, {}};
  }

  Reply clear(const QString& name) override
  {
    ++m_s.calls;
    if (m_s.forced != Status::Ok) {
      return {m_s.forced, {}, "forced"};
    }
    return {m_s.values.erase(name) ? Status::Ok : Status::NotFound, {}, {}};
  }

private:
  FakeState& m_s;
};

secretstore::Store makeStore(FakeState& s)
{
  return secretstore::Store(std::make_unique<FakeBackend>(s));
}
}  // namespace

TEST(SecretStore, RoundTripThroughTheBackend)
{
  FakeState s;
  auto store = makeStore(s);

  EXPECT_TRUE(store.set("ModOrganizer2_APIKEY", "çãø🔑"));
  EXPECT_EQ(s.values["ModOrganizer2_APIKEY"], "çãø🔑");
  EXPECT_EQ(store.get("ModOrganizer2_APIKEY"), "çãø🔑");
  EXPECT_TRUE(store.remove("ModOrganizer2_APIKEY"));
  EXPECT_TRUE(store.get("ModOrganizer2_APIKEY").isEmpty());
  EXPECT_FALSE(store.inMemory());
  EXPECT_FALSE(store.takeMemoryWarning());
}

TEST(SecretStore, RemovingWhatDoesNotExistIsNotAnError)
{
  FakeState s;
  auto store = makeStore(s);
  EXPECT_TRUE(store.remove("ModOrganizer2_APIKEY"));
}

// spec §3.2: no Secret Service, memory for the rest of the session, one warning on
// the first store
TEST(SecretStore, UnavailableSwitchesToMemoryForTheSession)
{
  FakeState s;
  s.forced   = Status::Unavailable;
  auto store = makeStore(s);

  EXPECT_TRUE(store.set("a", "1"));
  EXPECT_TRUE(store.inMemory());
  EXPECT_EQ(store.get("a"), "1");
  EXPECT_TRUE(store.takeMemoryWarning());
  EXPECT_FALSE(store.takeMemoryWarning());

  // the service is not tried again, even if it came back
  s.forced         = Status::Ok;
  const auto calls = s.calls;
  EXPECT_TRUE(store.set("b", "2"));
  EXPECT_EQ(store.get("b"), "2");
  EXPECT_TRUE(store.remove("a"));
  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_EQ(s.calls, calls);
  EXPECT_TRUE(s.values.empty());

  // one warning per session
  EXPECT_FALSE(store.takeMemoryWarning());
}

// reading at startup only logs: the warning waits for the first store
TEST(SecretStore, UnavailableOnReadDoesNotWarn)
{
  FakeState s;
  s.forced   = Status::Unavailable;
  auto store = makeStore(s);

  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_TRUE(store.inMemory());
  EXPECT_FALSE(store.takeMemoryWarning());

  EXPECT_TRUE(store.set("a", "1"));
  EXPECT_TRUE(store.takeMemoryWarning());
}

// a locked wallet or a dismissed prompt: the operation fails, nothing goes to memory
// and the next call tries the Secret Service again
TEST(SecretStore, FailedKeepsTheSecretService)
{
  FakeState s;
  s.values["a"] = "kept";
  s.forced      = Status::Failed;
  auto store    = makeStore(s);

  EXPECT_FALSE(store.set("a", "new"));
  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_FALSE(store.remove("a"));
  EXPECT_FALSE(store.inMemory());
  EXPECT_FALSE(store.takeMemoryWarning());

  s.forced = Status::Ok;
  EXPECT_EQ(store.get("a"), "kept");
  EXPECT_TRUE(store.set("a", "new"));
  EXPECT_EQ(s.values["a"], "new");
}

// built without libsecret
TEST(SecretStore, NoBackendIsMemoryOnly)
{
  secretstore::Store store(nullptr);
  EXPECT_TRUE(store.inMemory());
  EXPECT_TRUE(store.get("a").isEmpty());
  EXPECT_FALSE(store.takeMemoryWarning());
  EXPECT_TRUE(store.set("a", "1"));
  EXPECT_EQ(store.get("a"), "1");
  EXPECT_TRUE(store.takeMemoryWarning());
  EXPECT_FALSE(store.takeMemoryWarning());
}
