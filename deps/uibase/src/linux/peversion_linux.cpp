// Minimal PE version resource and icon reader, see peversion_linux.h and
// uibase/linux/peicon.h.
//
// Layout references (Microsoft documentation): "PE Format" (DOS header, COFF header,
// optional header data directories, section table, .rsrc directory tables) and
// "VS_VERSIONINFO", "StringFileInfo", "VarFileInfo", "VS_FIXEDFILEINFO".

#include "peversion_linux.h"

#include <uibase/linux/peicon.h>

#include <QByteArray>
#include <QFile>
#include <algorithm>
#include <cstdint>
#include <vector>

namespace MOBase::details
{

namespace
{

  constexpr std::uint32_t RT_ICON             = 3;
  constexpr std::uint32_t RT_GROUP_ICON       = 14;
  constexpr std::uint32_t RT_VERSION          = 16;
  constexpr std::uint32_t VS_VERSION_INFO_ID  = 1;
  constexpr std::uint32_t VS_FFI_SIGNATURE    = 0xFEEF04BD;
  constexpr std::uint32_t HIGH_BIT            = 0x80000000;
  constexpr std::size_t MaxVersionResource    = 0x10000;  // wLength is 16-bit
  constexpr std::size_t MaxResourceDirEntries = 4096;
  constexpr std::size_t MaxIconGroupEntries   = 64;
  constexpr std::size_t MaxIconImage          = 1u << 20;
  constexpr std::size_t MaxIconGroup          = 6 + 14 * MaxIconGroupEntries;
  constexpr std::size_t MaxIconFile           = 4u << 20;
  constexpr std::int64_t MaxIconDimension     = 256;

  // little-endian reads from a buffer; the caller checks the bounds
  std::uint16_t u16(const QByteArray& b, std::size_t off)
  {
    const auto* p = reinterpret_cast<const unsigned char*>(b.constData()) + off;
    return static_cast<std::uint16_t>(p[0] | (p[1] << 8));
  }

  std::uint32_t u32(const QByteArray& b, std::size_t off)
  {
    return static_cast<std::uint32_t>(u16(b, off)) |
           (static_cast<std::uint32_t>(u16(b, off + 2)) << 16);
  }

  bool fits(std::uint64_t off, std::uint64_t len, std::uint64_t size)
  {
    return off <= size && len <= size - off;
  }

  // random access to the file, every read is checked against the file size
  class Reader
  {
  public:
    explicit Reader(const QString& path) : m_file(path)
    {
      if (m_file.open(QIODevice::ReadOnly)) {
        m_size = static_cast<std::uint64_t>(std::max<qint64>(m_file.size(), 0));
      }
    }

    std::uint64_t size() const { return m_size; }

    // exactly `len` bytes at `off`, or nullopt
    std::optional<QByteArray> read(std::uint64_t off, std::uint64_t len)
    {
      if (!m_file.isOpen() || !fits(off, len, m_size) || len > (1u << 24)) {
        return std::nullopt;
      }

      if (!m_file.seek(static_cast<qint64>(off))) {
        return std::nullopt;
      }

      QByteArray data = m_file.read(static_cast<qint64>(len));
      if (static_cast<std::uint64_t>(data.size()) != len) {
        return std::nullopt;
      }

      return data;
    }

  private:
    QFile m_file;
    std::uint64_t m_size = 0;
  };

  struct Section
  {
    std::uint32_t virtualAddress;
    std::uint32_t rawSize;
    std::uint32_t rawPointer;
  };

  // file offset of `len` bytes at `rva`, which must be inside the raw data of a
  // section; `remaining` receives the number of bytes from `rva` to the end of that
  // section
  std::optional<std::uint64_t> rvaToOffset(const std::vector<Section>& sections,
                                           std::uint32_t rva, std::uint64_t len,
                                           std::uint64_t* remaining = nullptr)
  {
    for (const auto& s : sections) {
      if (rva < s.virtualAddress) {
        continue;
      }

      const std::uint64_t delta = rva - s.virtualAddress;
      if (!fits(delta, len, s.rawSize)) {
        continue;
      }

      if (remaining) {
        *remaining = s.rawSize - delta;
      }

      return static_cast<std::uint64_t>(s.rawPointer) + delta;
    }

    return std::nullopt;
  }

  // the resource directory tree, bounded by the section that contains it
  class ResourceTree
  {
  public:
    ResourceTree(Reader& r, std::uint64_t offset, std::uint64_t size)
        : m_reader(r), m_offset(offset), m_size(size)
    {}

    struct Entry
    {
      std::uint32_t name;    // id, or name offset with the high bit set
      std::uint32_t target;  // subdirectory (high bit set) or data entry offset
    };

    std::optional<std::vector<Entry>> directory(std::uint32_t rel)
    {
      const auto header = read(rel, 16);
      if (!header) {
        return std::nullopt;
      }

      const std::size_t count = std::size_t(u16(*header, 12)) + u16(*header, 14);
      if (count > MaxResourceDirEntries) {
        return std::nullopt;
      }

      const auto data = read(std::uint64_t(rel) + 16, count * 8);
      if (!data) {
        return std::nullopt;
      }

      std::vector<Entry> entries;
      for (std::size_t i = 0; i < count; ++i) {
        entries.push_back({u32(*data, i * 8), u32(*data, i * 8 + 4)});
      }

      return entries;
    }

    std::optional<QByteArray> read(std::uint64_t rel, std::uint64_t len)
    {
      if (!fits(rel, len, m_size)) {
        return std::nullopt;
      }

      return m_reader.read(m_offset + rel, len);
    }

  private:
    Reader& m_reader;
    std::uint64_t m_offset;
    std::uint64_t m_size;
  };

  // subdirectory of the first entry accepted by `match`
  template <class Match>
  std::optional<std::uint32_t> findSubdirectory(ResourceTree& tree, std::uint32_t rel,
                                                Match&& match)
  {
    const auto entries = tree.directory(rel);
    if (!entries) {
      return std::nullopt;
    }

    for (const auto& e : *entries) {
      if (match(e) && (e.target & HIGH_BIT)) {
        return e.target & ~HIGH_BIT;
      }
    }

    return std::nullopt;
  }

  // the resource section of a PE file: its section table and the extent of the
  // resource directory tree
  struct PeResources
  {
    std::vector<Section> sections;
    std::uint64_t offset;  // file offset of the root directory
    std::uint64_t size;    // bytes from the root directory to the end of its section
  };

  std::optional<PeResources> openResources(Reader& r)
  {
    // DOS header
    const auto dos = r.read(0, 64);
    if (!dos || dos->at(0) != 'M' || dos->at(1) != 'Z') {
      return std::nullopt;
    }
    const std::uint64_t peOffset = u32(*dos, 0x3c);

    // PE signature and COFF header
    const auto coff = r.read(peOffset, 24);
    if (!coff || !coff->startsWith(QByteArray("PE\0\0", 4))) {
      return std::nullopt;
    }
    const std::uint16_t sectionCount   = u16(*coff, 4 + 2);
    const std::uint16_t optionalHeader = u16(*coff, 4 + 16);

    // optional header: magic and the resource data directory (index 2)
    const auto opt = r.read(peOffset + 24, optionalHeader);
    if (!opt || opt->size() < 2) {
      return std::nullopt;
    }

    std::size_t countOffset = 0, dirsOffset = 0;
    switch (u16(*opt, 0)) {
    case 0x10b:  // PE32
      countOffset = 92;
      dirsOffset  = 96;
      break;
    case 0x20b:  // PE32+
      countOffset = 108;
      dirsOffset  = 112;
      break;
    default:
      return std::nullopt;
    }

    const std::size_t resourceDir = dirsOffset + 2 * 8;
    if (!fits(resourceDir, 8, opt->size()) || u32(*opt, countOffset) <= 2) {
      return std::nullopt;
    }
    const std::uint32_t resourceRva = u32(*opt, resourceDir);
    if (resourceRva == 0) {
      return std::nullopt;
    }

    // section table
    const auto table = r.read(peOffset + 24 + optionalHeader, sectionCount * 40ull);
    if (!table) {
      return std::nullopt;
    }

    PeResources res;
    for (std::size_t i = 0; i < sectionCount; ++i) {
      const std::size_t s = i * 40;
      res.sections.push_back(
          {u32(*table, s + 12), u32(*table, s + 16), u32(*table, s + 20)});
    }

    res.size = 0;
    const auto resourceOffset = rvaToOffset(res.sections, resourceRva, 16, &res.size);
    if (!resourceOffset) {
      return std::nullopt;
    }
    res.offset = *resourceOffset;

    return res;
  }

  // the first language of the resource name directory `name`: the data entry offset
  std::optional<std::uint32_t> firstLanguageEntry(ResourceTree& tree,
                                                  std::uint32_t name)
  {
    const auto languages = tree.directory(name);
    if (!languages || languages->empty() || (languages->front().target & HIGH_BIT)) {
      return std::nullopt;
    }

    return languages->front().target;
  }

  // the contents of the IMAGE_RESOURCE_DATA_ENTRY at `entry`; a resource larger than
  // `maxSize` is cut to that size if `truncate` is set, else rejected
  std::optional<QByteArray> readDataEntry(Reader& r, ResourceTree& tree,
                                          const std::vector<Section>& sections,
                                          std::uint32_t entry, std::uint64_t maxSize,
                                          bool truncate)
  {
    // data RVA and size
    const auto dataEntry = tree.read(entry, 16);
    if (!dataEntry) {
      return std::nullopt;
    }

    std::uint64_t size = u32(*dataEntry, 4);
    if (size > maxSize) {
      if (!truncate) {
        return std::nullopt;
      }
      size = maxSize;
    }

    const auto dataOffset = rvaToOffset(sections, u32(*dataEntry, 0), size);
    if (!dataOffset) {
      return std::nullopt;
    }

    return r.read(*dataOffset, size);
  }

  // the raw VS_VERSION_INFO resource
  std::optional<QByteArray> readVersionResource(Reader& r)
  {
    const auto res = openResources(r);
    if (!res) {
      return std::nullopt;
    }

    // resource tree: type RT_VERSION / name VS_VERSION_INFO (else the first) / first
    // language
    ResourceTree tree(r, res->offset, res->size);

    const auto type = findSubdirectory(tree, 0, [](auto&& e) {
      return e.name == RT_VERSION;
    });
    if (!type) {
      return std::nullopt;
    }

    auto name = findSubdirectory(tree, *type, [](auto&& e) {
      return e.name == VS_VERSION_INFO_ID;
    });
    if (!name) {
      name = findSubdirectory(tree, *type, [](auto&&) {
        return true;
      });
    }
    if (!name) {
      return std::nullopt;
    }

    const auto entry = firstLanguageEntry(tree, *name);
    if (!entry) {
      return std::nullopt;
    }

    return readDataEntry(r, tree, res->sections, *entry, MaxVersionResource, true);
  }

  std::uint32_t u32be(const QByteArray& b, std::size_t off)
  {
    const auto* p = reinterpret_cast<const unsigned char*>(b.constData()) + off;
    return (static_cast<std::uint32_t>(p[0]) << 24) |
           (static_cast<std::uint32_t>(p[1]) << 16) |
           (static_cast<std::uint32_t>(p[2]) << 8) | static_cast<std::uint32_t>(p[3]);
  }

  // whether an RT_ICON image is a PNG or a BMP (BITMAPINFOHEADER) of a sane size; the
  // dimensions are checked here because decoding a small image that declares a huge
  // size would allocate a lot of memory
  bool acceptableIconImage(const QByteArray& data)
  {
    static const QByteArray pngSignature("\x89PNG\r\n\x1a\n", 8);

    if (data.startsWith(pngSignature)) {
      // signature, IHDR length and tag, then width and height (big-endian)
      if (data.size() < 24) {
        return false;
      }
      const std::uint32_t w = u32be(data, 16);
      const std::uint32_t h = u32be(data, 20);
      return w >= 1 && w <= MaxIconDimension && h >= 1 && h <= MaxIconDimension;
    }

    // BITMAPINFOHEADER: biSize, biWidth, biHeight (signed, twice the height: XOR and
    // AND masks)
    if (data.size() < 40 || u32(data, 0) < 40) {
      return false;
    }
    const std::int64_t w = static_cast<std::int32_t>(u32(data, 4));
    std::int64_t h       = static_cast<std::int32_t>(u32(data, 8));
    if (h < 0) {
      h = -h;
    }
    return w >= 1 && w <= MaxIconDimension && h >= 1 && h <= 2 * MaxIconDimension;
  }

  // .ico file from the first RT_GROUP_ICON and the RT_ICON images it references
  std::optional<QByteArray> readIconFile(Reader& r)
  {
    const auto res = openResources(r);
    if (!res) {
      return std::nullopt;
    }

    ResourceTree tree(r, res->offset, res->size);

    const auto groupType = findSubdirectory(tree, 0, [](auto&& e) {
      return e.name == RT_GROUP_ICON;
    });
    if (!groupType) {
      return std::nullopt;
    }

    const auto groupName = findSubdirectory(tree, *groupType, [](auto&&) {
      return true;
    });
    if (!groupName) {
      return std::nullopt;
    }

    const auto groupEntry = firstLanguageEntry(tree, *groupName);
    if (!groupEntry) {
      return std::nullopt;
    }

    const auto group =
        readDataEntry(r, tree, res->sections, *groupEntry, MaxIconGroup, true);
    if (!group || group->size() < 6 || u16(*group, 2) != 1) {
      return std::nullopt;
    }

    const auto imageType = findSubdirectory(tree, 0, [](auto&& e) {
      return e.name == RT_ICON;
    });
    if (!imageType) {
      return std::nullopt;
    }

    // GRPICONDIRENTRY (14 bytes): width, height, colors, reserved, planes, bits, size,
    // id; the entries that do not fit in the group or the limit are ignored
    const std::size_t declared = u16(*group, 4);
    const std::size_t count =
        std::min({declared, MaxIconGroupEntries, (std::size_t(group->size()) - 6) / 14});

    struct Image
    {
      QByteArray entry;  // width, height, colors, reserved, planes, bits
      QByteArray data;
    };
    std::vector<Image> images;
    std::size_t total = 6;  // ICONDIR

    for (std::size_t i = 0; i < count; ++i) {
      const std::size_t e = 6 + i * 14;
      const std::uint16_t id = u16(*group, e + 12);

      const auto name = findSubdirectory(tree, *imageType, [&](auto&& entry) {
        return entry.name == id;
      });
      if (!name) {
        continue;
      }

      const auto entry = firstLanguageEntry(tree, *name);
      if (!entry) {
        continue;
      }

      // the real size of the resource, not the declared one
      auto data = readDataEntry(r, tree, res->sections, *entry, MaxIconImage, false);
      if (!data || data->isEmpty() || !acceptableIconImage(*data)) {
        continue;
      }

      // the whole .ico stays under a limit: ICONDIRENTRY and the image
      const std::size_t added = 16 + static_cast<std::size_t>(data->size());
      if (total + added > MaxIconFile) {
        break;
      }
      total += added;

      images.push_back({group->mid(e, 8), std::move(*data)});
    }

    if (images.empty()) {
      return std::nullopt;
    }

    const auto put16 = [](QByteArray& b, std::uint16_t v) {
      b.append(char(v & 0xff));
      b.append(char(v >> 8));
    };
    const auto put32 = [&](QByteArray& b, std::uint32_t v) {
      put16(b, static_cast<std::uint16_t>(v & 0xffff));
      put16(b, static_cast<std::uint16_t>(v >> 16));
    };

    QByteArray ico;
    put16(ico, 0);
    put16(ico, 1);
    put16(ico, static_cast<std::uint16_t>(images.size()));

    // ICONDIRENTRY (16 bytes): the group entry with the id replaced by the offset
    std::uint32_t offset = static_cast<std::uint32_t>(6 + 16 * images.size());
    for (const auto& image : images) {
      ico.append(image.entry);  // width, height, colors, reserved, planes, bits
      put32(ico, static_cast<std::uint32_t>(image.data.size()));
      put32(ico, offset);
      offset += static_cast<std::uint32_t>(image.data.size());
    }

    for (const auto& image : images) {
      ico.append(image.data);
    }

    return ico;
  }

  std::size_t align4(std::size_t n)
  {
    return (n + 3) & ~std::size_t(3);
  }

  // one VS_VERSIONINFO-style node: wLength, wValueLength, wType, szKey, padding,
  // value, padding, children
  struct Node
  {
    std::size_t end;
    std::uint16_t type;
    QString key;
    std::size_t valueOffset;
    std::size_t valueSize;  // in bytes
    std::size_t childrenOffset;
  };

  std::optional<Node> parseNode(const QByteArray& b, std::size_t start,
                                std::size_t limit)
  {
    if (!fits(start, 6, limit)) {
      return std::nullopt;
    }

    const std::size_t length = u16(b, start);
    if (length < 6 || !fits(start, length, limit)) {
      return std::nullopt;
    }

    Node n;
    n.end                  = start + length;
    const std::size_t vlen = u16(b, start + 2);
    n.type                 = u16(b, start + 4);

    // NUL-terminated UTF-16 key
    std::size_t p = start + 6;
    for (;;) {
      if (!fits(p, 2, n.end)) {
        return std::nullopt;
      }

      const auto c = u16(b, p);
      p += 2;

      if (c == 0) {
        break;
      }

      n.key.append(QChar(c));
    }

    // text values are counted in characters, binary values in bytes; text values
    // that claim to be longer than their node are truncated
    n.valueOffset = std::min(align4(p), n.end);
    n.valueSize   = n.type == 1 ? vlen * 2 : vlen;
    if (!fits(n.valueOffset, n.valueSize, n.end)) {
      if (n.type != 1) {
        return std::nullopt;
      }
      n.valueSize = n.end - n.valueOffset;
    }

    n.childrenOffset = align4(n.valueOffset + n.valueSize);
    return n;
  }

  template <class F>
  void forEachChild(const QByteArray& b, const Node& parent, F&& f)
  {
    std::size_t p = parent.childrenOffset;

    while (fits(p, 6, parent.end)) {
      const auto child = parseNode(b, p, parent.end);
      if (!child) {
        return;
      }

      f(*child);
      p = align4(child->end);
    }
  }

  QString textValue(const QByteArray& b, const Node& n)
  {
    QString s;
    for (std::size_t p = n.valueOffset; p + 2 <= n.valueOffset + n.valueSize; p += 2) {
      const auto c = u16(b, p);
      if (c == 0) {
        break;
      }
      s.append(QChar(c));
    }
    return s;
  }

  // \VarFileInfo\Translation, first language and code page, as "040904b0"
  QString firstTranslation(const QByteArray& b, const Node& root)
  {
    QString translation;

    forEachChild(b, root, [&](const Node& n) {
      if (n.key != "VarFileInfo") {
        return;
      }

      forEachChild(b, n, [&](const Node& var) {
        if (translation.isEmpty() && var.key == "Translation" && var.valueSize >= 4) {
          translation = QString("%1%2")
                            .arg(u16(b, var.valueOffset), 4, 16, QChar('0'))
                            .arg(u16(b, var.valueOffset + 2), 4, 16, QChar('0'));
        }
      });
    });

    return translation;
  }

  QString productVersion(const QByteArray& b, const Node& root)
  {
    const QString translation = firstTranslation(b, root);
    if (translation.isEmpty()) {
      return {};
    }

    QString value;

    forEachChild(b, root, [&](const Node& n) {
      if (n.key != "StringFileInfo") {
        return;
      }

      forEachChild(b, n, [&](const Node& table) {
        if (table.key.compare(translation, Qt::CaseInsensitive) != 0) {
          return;
        }

        forEachChild(b, table, [&](const Node& string) {
          if (value.isEmpty() && string.key == "ProductVersion") {
            value = textValue(b, string);
          }
        });
      });
    });

    return value;
  }

}  // namespace

std::optional<PeVersionInfo> readPeVersionInfo(const QString& path)
{
  Reader reader(path);

  const auto data = readVersionResource(reader);
  if (!data) {
    return std::nullopt;
  }

  const auto root = parseNode(*data, 0, static_cast<std::size_t>(data->size()));
  if (!root || root->key != "VS_VERSION_INFO") {
    return std::nullopt;
  }

  PeVersionInfo info;

  if (root->type == 0 && root->valueSize >= 52 &&
      u32(*data, root->valueOffset) == VS_FFI_SIGNATURE) {
    const std::size_t v = root->valueOffset;
    info.fixed          = PeFixedFileInfo{u32(*data, v + 8), u32(*data, v + 12),
                                 u32(*data, v + 16), u32(*data, v + 20)};
  }

  info.productVersion = productVersion(*data, *root);

  return info;
}

std::optional<QByteArray> readPeIcon(const QString& path)
{
  Reader reader(path);
  return readIconFile(reader);
}

}  // namespace MOBase::details
