#!/usr/bin/env bash
# Guard against platform-sized reads in the save game readers of game_bethesda.
#
# The save formats store the 32-bit long of Windows. On Linux, std::int64_t and
# std::uint64_t are `long` / `unsigned long`, the types that FileWrapper::read<T>() and
# skip<T>() specialise to 4 bytes (see gamebryo/linux/gamebryosavegame_linux.h), so
# read<std::uint64_t>() would silently read 4 bytes there. 64-bit on-disk fields must be
# spelled `unsigned long long` or read with readLong().
#
# Fails (exit 1) printing file:line for read<[u]int64_t>, skip<[u]int64_t> and
# read<[unsigned] long>. A line is exempt with a `// save-read-ok: <reason>` comment on
# the same line or the line above.
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
dir="$root/deps/game_bethesda/src"
[[ -d "$dir" ]] || { echo "check-save-reads: $dir not found" >&2; exit 2; }

pattern='(read|skip)[[:space:]]*<[[:space:]]*(std::)?u?int64_t[[:space:]]*>|read[[:space:]]*<[[:space:]]*(unsigned[[:space:]]+)?long[[:space:]]*>'

found=0
while IFS= read -r hit; do
  [[ "$hit" == *"save-read-ok:"* ]] && continue
  # the marker may also be on the line above (the hit is file:line:text)
  file="${hit%%:*}"; rest="${hit#*:}"; lineno="${rest%%:*}"
  if ((lineno > 1)) && [[ "$(sed -n "$((lineno - 1))p" "$file")" == *"save-read-ok:"* ]]; then
    continue
  fi
  echo "${hit#"$root"/}"
  found=1
done < <(grep -rnE --include='*.h' --include='*.cpp' --include='*.hpp' "$pattern" "$dir" || true)

if ((found)); then
  echo "check-save-reads: platform-sized read in a save game reader (see the header of $0)." >&2
  echo "Use a fixed-width type that is not long on Linux, or add '// save-read-ok: <reason>'." >&2
  exit 1
fi
echo "check-save-reads: ok"
