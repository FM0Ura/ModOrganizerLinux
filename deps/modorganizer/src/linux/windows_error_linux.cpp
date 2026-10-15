// Linux implementation of windows_error::constructMessage() of
// shared/windows_error.cpp: error codes are errno values, described by strerror().

#include "../shared/windows_error.h"

#include <cstring>
#include <sstream>

namespace MOShared
{

std::string windows_error::constructMessage(const std::string& input, int inErrorCode)
{
  const int errorCode = inErrorCode != -1 ? inErrorCode : errno;

  std::ostringstream finalMessage;
  finalMessage << input << " (" << std::strerror(errorCode) << " [" << errorCode
               << "])";

  // restore the error code, like on Windows
  errno = errorCode;

  return finalMessage.str();
}

}  // namespace MOShared
