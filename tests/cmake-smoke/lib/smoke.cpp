#include "smoke.h"

namespace smoke
{

QString Greeter::message() const
{
  return tr("ok");
}

std::expected<std::string, std::string> run()
{
  Greeter greeter;
  const auto text = greeter.message().toStdString();
  if (text.empty()) {
    return std::unexpected(std::string("empty message"));
  }
  return text;
}

}  // namespace smoke

extern "C" {
SMOKE_EXPORT int smoke_visible_symbol()
{
  return 1;
}
int smoke_hidden_symbol()
{
  return 2;
}
}
