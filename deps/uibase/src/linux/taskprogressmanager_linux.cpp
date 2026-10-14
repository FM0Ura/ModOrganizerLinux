// Linux implementation of the taskbar parts of TaskProgressManager: there is no
// taskbar progress on Linux yet, m_Taskbar stays null so progress updates are ignored.

#include "taskprogressmanager.h"

namespace MOBase
{

void TaskProgressManager::showProgress() {}

bool TaskProgressManager::tryCreateTaskbar()
{
  m_Taskbar = nullptr;
  return false;
}

}  // namespace MOBase
