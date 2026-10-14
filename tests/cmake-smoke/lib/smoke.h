#pragma once

#include <expected>
#include <string>

#include <QObject>

#define SMOKE_EXPORT __attribute__((visibility("default")))

namespace smoke
{

class SMOKE_EXPORT Greeter : public QObject
{
  Q_OBJECT
public:
  QString message() const;
};

SMOKE_EXPORT std::expected<std::string, std::string> run();

}  // namespace smoke
