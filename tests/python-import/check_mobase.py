# Imports the installed mobase module with the system's Python (Phase 4d.1): mobase is a
# pybind11 extension that needs uibase and Qt; PyQt6 must be the system's.
#   python3 -I check_mobase.py <install prefix>
import pathlib
import sys

install = pathlib.Path(sys.argv[1])
sys.path.insert(0, str(install / "bin" / "plugins" / "plugin_python" / "libs"))

import PyQt6.QtCore  # noqa: E402  (the system's PyQt6, loaded before mobase)
import mobase  # noqa: E402

assert hasattr(mobase, "IPlugin"), "mobase has no IPlugin"
assert hasattr(mobase.utils, "getKnownFolder"), "mobase.utils has no getKnownFolder"
assert hasattr(mobase.utils.KnownFolder, "Documents"), "KnownFolder has no Documents"
documents = mobase.utils.getKnownFolder(mobase.utils.KnownFolder.Documents)
assert isinstance(documents, PyQt6.QtCore.QDir), "getKnownFolder does not return a QDir"
print("mobase ok")
