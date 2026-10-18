// Reader of text VDF files (Steam's KeyValues format: libraryfolders.vdf, appmanifest
// files, localconfig.vdf, ...), Linux only.
//
// The rules:
// - keys and values are quoted strings (with the \n, \t, \\ and \" escapes) or bare
//   words,
// - a key is followed by a value or by a block in braces,
// - '//' starts a comment until the end of the line, conditionals such as [$WIN32] are
//   ignored,
// - the files are untrusted input: parsing stops at the first malformed token and
//   keeps what came before it.

#ifndef UIBASE_LINUX_VDF_H
#define UIBASE_LINUX_VDF_H

#include <uibase/dllimport.h>

#include <QByteArray>
#include <QString>
#include <QStringView>

#include <initializer_list>
#include <optional>
#include <vector>

namespace MOBase::vdf
{

// one entry of a text VDF file (Steam's KeyValues): a key with a string value (text),
// or a block of entries; value(key) reads the text of a child
struct QDLLEXPORT Node
{
  QString key;
  QString text;
  std::vector<Node> children;
  bool block = false;

  // the last child with this key, ignoring case (Steam keeps the last of repeated
  // keys); null if there is none
  const Node* child(QStringView key) const;

  // the value of child(key), empty if there is none or it is a block
  QString value(QStringView key) const;

  // child(keys[0])->child(keys[1])...; null as soon as one is missing
  const Node* path(std::initializer_list<QStringView> keys) const;
};

// the entries of a text VDF document, as the children of a block node with an empty
// key; parsing stops at the first malformed token (unterminated string, unexpected
// brace) and keeps what came before it; nesting deeper than 64 levels stops it too.
QDLLEXPORT Node parse(const QByteArray& data);

// parse() of a file of at most 16 MiB; nullopt if it can't be read or is bigger
QDLLEXPORT std::optional<Node> parseFile(const QString& path);

}  // namespace MOBase::vdf

#endif  // UIBASE_LINUX_VDF_H
