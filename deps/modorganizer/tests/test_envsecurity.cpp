#include <gtest/gtest.h>

#include <sys/stat.h>

#include <QString>

#include "envsecurity.h"
#include "tempdir.h"

TEST(EnvSecurity, NoSecurityProducts)
{
  EXPECT_TRUE(env::getSecurityProducts().empty());
}

TEST(EnvSecurity, FileRights)
{
  TempDir tmp;
  const auto file = tmp.createFile("a.sh");
  const auto path = QString::fromStdString(file.string());

  ASSERT_EQ(::chmod(file.c_str(), 0644), 0);

  auto fs = env::getFileSecurity(path);
  EXPECT_TRUE(fs.error.isEmpty());
  EXPECT_EQ(fs.owner, "(this user)");
  EXPECT_EQ(fs.rights.list, QStringList({"read", "write"}));
  EXPECT_FALSE(fs.rights.hasExecute);
  EXPECT_FALSE(fs.rights.normalRights);

  ASSERT_EQ(::chmod(file.c_str(), 0755), 0);

  fs = env::getFileSecurity(path);
  EXPECT_EQ(fs.rights.list, QStringList({"read", "write", "execute"}));
  EXPECT_TRUE(fs.rights.hasExecute);
  EXPECT_TRUE(fs.rights.normalRights);

  // missing file
  fs = env::getFileSecurity(QString::fromStdString((tmp.path() / "missing").string()));
  EXPECT_FALSE(fs.error.isEmpty());
}
