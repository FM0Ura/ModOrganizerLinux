#include "guidedmatch.h"

#include <algorithm>

namespace modlists
{

GuidedQueue::GuidedQueue(std::vector<GuidedItem> items) : m_items(std::move(items)) {}

std::optional<std::size_t> GuidedQueue::current() const
{
  for (std::size_t i = 0; i < m_items.size(); ++i) {
    if (m_items[i].state == GuidedItem::Pending) {
      return i;
    }
  }
  return std::nullopt;
}

MatchResult GuidedQueue::match(int modId, int fileId)
{
  const auto cur = current();

  // a Pending item wins over a duplicate of the same file further down
  std::optional<std::size_t> found;
  for (std::size_t i = 0; i < m_items.size(); ++i) {
    const GuidedItem& it = m_items[i];
    if (it.modId != modId || it.fileId != fileId) {
      continue;
    }
    if (it.state == GuidedItem::Pending) {
      found = i;
      break;
    }
    if (!found) {
      found = i;
    }
  }

  if (!found) {
    return MatchResult::NotInList;
  }

  GuidedItem& item = m_items[*found];
  switch (item.state) {
  case GuidedItem::Queued:
    return MatchResult::AlreadyQueued;
  case GuidedItem::Skipped:
    return MatchResult::SkippedItem;
  case GuidedItem::Pending:
    break;
  }

  item.state = GuidedItem::Queued;
  return (cur && *cur == *found) ? MatchResult::Current : MatchResult::OtherItem;
}

void GuidedQueue::skip()
{
  if (const auto cur = current()) {
    m_items[*cur].state = GuidedItem::Skipped;
  }
}

void GuidedQueue::unskip(std::size_t index)
{
  if (index < m_items.size() && m_items[index].state == GuidedItem::Skipped) {
    m_items[index].state = GuidedItem::Pending;
  }
}

std::size_t GuidedQueue::done() const
{
  return static_cast<std::size_t>(
      std::count_if(m_items.begin(), m_items.end(), [](const GuidedItem& i) {
        return i.state == GuidedItem::Queued;
      }));
}

std::size_t GuidedQueue::total() const
{
  return m_items.size();
}

}  // namespace modlists
