#include <dlfcn.h>

#include <expected>
#include <format>
#include <iostream>
#include <string>

#include "smoke.h"

static_assert(__cplusplus > 202002L, "mo2_configure_target must enable C++23");

namespace
{
std::expected<int, std::string> parse(int value)
{
  if (value < 0) {
    return std::unexpected(std::format("negative value: {}", value));
  }
  return value * 2;
}
}  // namespace

int main()
{
  const auto good = parse(21);
  const auto bad  = parse(-1);
  if (!good || *good != 42 || bad || bad.error() != "negative value: -1") {
    std::cerr << "std::expected / std::format check failed\n";
    return 1;
  }

  // the library must export what it marks as exported and hide everything else
  // (-fvisibility=hidden on shared libraries)
  if (dlsym(RTLD_DEFAULT, "smoke_visible_symbol") == nullptr) {
    std::cerr << "exported symbol is missing from the shared library\n";
    return 1;
  }
  if (dlsym(RTLD_DEFAULT, "smoke_hidden_symbol") != nullptr) {
    std::cerr << "non-exported symbol leaked: -fvisibility=hidden not applied\n";
    return 1;
  }

  const auto result = smoke::run();
  if (!result) {
    std::cerr << result.error() << "\n";
    return 1;
  }

  std::cout << *result << "\n";
  return 0;
}
