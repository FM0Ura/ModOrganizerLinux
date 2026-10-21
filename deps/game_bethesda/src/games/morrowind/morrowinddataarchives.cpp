#include "morrowinddataarchives.h"
#include "registry.h"
#include <utility.h>

#include "gamegamebryo.h"

QStringList MorrowindDataArchives::vanillaArchives() const
{
  return {"Morrowind.bsa"};
}

QStringList MorrowindDataArchives::getArchives(const QString& iniFile) const
{
  wchar_t buffer[256];
  QStringList result;
  // the INI with another case (the Proton prefix has Skyrim.ini for skyrim.ini)
  std::wstring iniFileW =
      QDir::toNativeSeparators(GameGamebryo::resolveIgnoringCase(iniFile))
          .toStdWString();

  errno = 0;

  QString key = "Archive ";
  int i       = 0;
  while (::GetPrivateProfileStringW(L"Archives",
                                    (key + QString::number(i)).toStdWString().c_str(),
                                    L"", buffer, 256, iniFileW.c_str()) != 0) {
    result.append(QString::fromStdWString(buffer).trimmed());
    i++;
  }

  return result;
}

void MorrowindDataArchives::setArchives(const QString& iniFile, const QStringList& list)
{
  const std::wstring iniFileW =
      GameGamebryo::resolveIgnoringCase(iniFile).toStdWString();
  ::WritePrivateProfileSectionW(L"Archives", NULL, iniFileW.c_str());

  QString key      = "Archive ";
  int writtenCount = 0;
  foreach (const QString& value, list) {
    if (!MOBase::WriteRegistryValue(
            L"Archives", (key + QString::number(writtenCount)).toStdWString().c_str(),
            value.toStdWString().c_str(), iniFileW.c_str())) {
      qWarning("failed to set archives in \"%s\"", qUtf8Printable(iniFile));
    }
    ++writtenCount;
  }
}

QStringList MorrowindDataArchives::archives(const MOBase::IProfile* profile) const
{
  QStringList result;

  QString iniFile =
      profile->localSettingsEnabled()
          ? QDir(profile->absolutePath()).absoluteFilePath("morrowind.ini")
          : gameDirectory().absoluteFilePath("morrowind.ini");
  result.append(getArchives(iniFile));

  return result;
}

void MorrowindDataArchives::writeArchiveList(MOBase::IProfile* profile,
                                             const QStringList& before)
{
  QString iniFile =
      profile->localSettingsEnabled()
          ? QDir(profile->absolutePath()).absoluteFilePath("morrowind.ini")
          : gameDirectory().absoluteFilePath("morrowind.ini");
  setArchives(iniFile, before);
}
