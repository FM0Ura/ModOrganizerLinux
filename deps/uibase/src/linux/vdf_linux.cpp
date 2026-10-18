// Text VDF (Steam KeyValues) reader of include/uibase/linux/vdf.h. The files are
// untrusted input: size and nesting depth are bounded and malformed content ends the
// parse, keeping what came before it.

#include <uibase/linux/vdf.h>

#include <QFile>

namespace MOBase::vdf
{

namespace
{

  constexpr qint64 MaxVdfSize    = 16 * 1024 * 1024;
  constexpr std::size_t MaxDepth = 64;

  class VdfTokenizer
  {
  public:
    enum class Kind
    {
      String,
      Open,
      Close,
      End,
      Error
    };

    struct Token
    {
      Kind kind;
      QByteArray text;
    };

    explicit VdfTokenizer(const QByteArray& data) : m_data(data) {}

    Token next()
    {
      skipSpaceAndComments();

      if (m_pos >= m_data.size()) {
        return {Kind::End, {}};
      }

      const char c = m_data[m_pos];

      if (c == '{') {
        ++m_pos;
        return {Kind::Open, {}};
      }

      if (c == '}') {
        ++m_pos;
        return {Kind::Close, {}};
      }

      if (c == '"') {
        return quoted();
      }

      return bare();
    }

  private:
    const QByteArray& m_data;
    qsizetype m_pos = 0;

    static bool isSpace(char c)
    {
      return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f';
    }

    void skipSpaceAndComments()
    {
      while (m_pos < m_data.size()) {
        const char c = m_data[m_pos];

        if (isSpace(c)) {
          ++m_pos;
        } else if (c == '/' && m_pos + 1 < m_data.size() && m_data[m_pos + 1] == '/') {
          // comment until the end of the line
          while (m_pos < m_data.size() && m_data[m_pos] != '\n') {
            ++m_pos;
          }
        } else if (c == '[') {
          // conditional such as [$WIN32], ignored
          while (m_pos < m_data.size() && m_data[m_pos] != ']') {
            ++m_pos;
          }
          ++m_pos;
        } else {
          break;
        }
      }
    }

    Token quoted()
    {
      QByteArray text;
      ++m_pos;  // opening quote

      while (m_pos < m_data.size()) {
        const char c = m_data[m_pos++];

        if (c == '"') {
          return {Kind::String, text};
        }

        if (c == '\\' && m_pos < m_data.size()) {
          const char e = m_data[m_pos++];
          switch (e) {
          case 'n':
            text.append('\n');
            break;
          case 't':
            text.append('\t');
            break;
          case '\\':
          case '"':
            text.append(e);
            break;
          default:
            text.append('\\').append(e);
            break;
          }
          continue;
        }

        text.append(c);
      }

      // unterminated string
      return {Kind::Error, {}};
    }

    Token bare()
    {
      const qsizetype start = m_pos;

      while (m_pos < m_data.size()) {
        const char c = m_data[m_pos];
        if (isSpace(c) || c == '{' || c == '}' || c == '"') {
          break;
        }
        ++m_pos;
      }

      return {Kind::String, m_data.mid(start, m_pos - start)};
    }
  };

  // entries until the closing brace of this block (or the end); false when the parse
  // must stop (malformed, too deep)
  bool parseBlock(VdfTokenizer& tokens, Node& block, std::size_t depth, bool inner)
  {
    for (;;) {
      const auto key = tokens.next();

      if (key.kind == VdfTokenizer::Kind::Close) {
        return inner;  // a closing brace at the top level is malformed
      }
      if (key.kind != VdfTokenizer::Kind::String) {
        return key.kind == VdfTokenizer::Kind::End && !inner;
      }

      const auto value = tokens.next();
      Node node;
      node.key = QString::fromUtf8(key.text);

      if (value.kind == VdfTokenizer::Kind::Open) {
        if (depth + 1 > MaxDepth) {
          return false;
        }
        node.block    = true;
        const bool ok = parseBlock(tokens, node, depth + 1, true);
        block.children.push_back(std::move(node));
        if (!ok) {
          return false;
        }
        continue;
      }

      if (value.kind != VdfTokenizer::Kind::String) {
        return false;
      }

      node.text = QString::fromUtf8(value.text);
      block.children.push_back(std::move(node));
    }
  }

}  // namespace

const Node* Node::child(QStringView k) const
{
  const Node* found = nullptr;
  for (const auto& c : children) {
    if (QStringView(c.key).compare(k, Qt::CaseInsensitive) == 0) {
      found = &c;
    }
  }
  return found;
}

QString Node::value(QStringView k) const
{
  const auto* c = child(k);
  return (c && !c->block) ? c->text : QString();
}

const Node* Node::path(std::initializer_list<QStringView> keys) const
{
  const Node* n = this;
  for (auto k : keys) {
    n = n->child(k);
    if (!n) {
      return nullptr;
    }
  }
  return n;
}

Node parse(const QByteArray& data)
{
  Node root;
  root.block = true;
  VdfTokenizer tokens(data);
  parseBlock(tokens, root, 0, false);
  return root;
}

std::optional<Node> parseFile(const QString& path)
{
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly) || file.size() > MaxVdfSize) {
    return {};
  }
  return parse(file.read(MaxVdfSize));
}

}  // namespace MOBase::vdf
