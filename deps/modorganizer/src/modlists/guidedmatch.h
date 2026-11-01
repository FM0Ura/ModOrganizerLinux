#ifndef MODLISTS_GUIDEDMATCH_H
#define MODLISTS_GUIDEDMATCH_H

#include <cstddef>
#include <optional>
#include <vector>

// The queue of the guided downloads: the user downloads the files from the Nexus
// one by one and every nxm:// that arrives is matched against the list.
namespace modlists
{

struct GuidedItem
{
  enum State
  {
    Pending,
    Queued,
    Skipped
  };

  int entryIndex  = 0;
  int sourceIndex = 0;
  int modId       = 0;
  int fileId      = 0;
  State state     = Pending;
};

enum class MatchResult
{
  Current,
  OtherItem,
  AlreadyQueued,
  SkippedItem,
  NotInList
};

class GuidedQueue
{
public:
  explicit GuidedQueue(std::vector<GuidedItem> items);

  // the first Pending item
  std::optional<std::size_t> current() const;

  // Current and OtherItem mark the item Queued; the rest changes nothing
  MatchResult match(int modId, int fileId);

  // skips the current item
  void skip();
  // a skipped item comes back only through this
  void unskip(std::size_t index);

  // a Pending item whose file the user gave by hand (a manual source) is Queued;
  // false for any other item
  bool setQueued(std::size_t index);

  // the Queued items
  std::size_t done() const;
  std::size_t total() const;
  const std::vector<GuidedItem>& items() const { return m_items; }

private:
  std::vector<GuidedItem> m_items;
};

}  // namespace modlists

#endif  // MODLISTS_GUIDEDMATCH_H
