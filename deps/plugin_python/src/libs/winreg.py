"""winreg for Linux: the registry of Windows, always empty.

Plugins written for Windows import winreg at the top of their modules (e.g.,
RootBuilder, to read the current instance of Mod Organizer), and Python only has it
on Windows: without this module, the import fails and the plugin is not loaded.

Here every key is missing: opening, querying, enumerating or deleting raises
FileNotFoundError, as Windows does for a key that does not exist (what these plugins
already handle), and writing raises PermissionError. Installed on Linux only, in
plugin_python/libs (in sys.path).
"""

import os
import re

error = OSError

HKEY_CLASSES_ROOT = 0x80000000
HKEY_CURRENT_USER = 0x80000001
HKEY_LOCAL_MACHINE = 0x80000002
HKEY_USERS = 0x80000003
HKEY_PERFORMANCE_DATA = 0x80000004
HKEY_CURRENT_CONFIG = 0x80000005
HKEY_DYN_DATA = 0x80000006

KEY_QUERY_VALUE = 0x0001
KEY_SET_VALUE = 0x0002
KEY_CREATE_SUB_KEY = 0x0004
KEY_ENUMERATE_SUB_KEYS = 0x0008
KEY_NOTIFY = 0x0010
KEY_CREATE_LINK = 0x0020
KEY_WOW64_64KEY = 0x0100
KEY_WOW64_32KEY = 0x0200
KEY_READ = 0x20019
KEY_WRITE = 0x20006
KEY_EXECUTE = KEY_READ
KEY_ALL_ACCESS = 0xF003F

REG_NONE = 0
REG_SZ = 1
REG_EXPAND_SZ = 2
REG_BINARY = 3
REG_DWORD = 4
REG_DWORD_LITTLE_ENDIAN = 4
REG_DWORD_BIG_ENDIAN = 5
REG_LINK = 6
REG_MULTI_SZ = 7
REG_RESOURCE_LIST = 8
REG_FULL_RESOURCE_DESCRIPTOR = 9
REG_RESOURCE_REQUIREMENTS_LIST = 10
REG_QWORD = 11
REG_QWORD_LITTLE_ENDIAN = 11


class HKEYType:
    """Never created: no key can be opened."""


def _missing(*args, **kwargs):
    raise FileNotFoundError(2, "The system cannot find the file specified")


def _refused(*args, **kwargs):
    raise PermissionError(13, "Access is denied")


ConnectRegistry = _missing
OpenKey = _missing
OpenKeyEx = _missing
QueryValue = _missing
QueryValueEx = _missing
QueryInfoKey = _missing
EnumKey = _missing
EnumValue = _missing
DeleteKey = _missing
DeleteKeyEx = _missing
DeleteValue = _missing
QueryReflectionKey = _missing
DisableReflectionKey = _missing
EnableReflectionKey = _missing

CreateKey = _refused
CreateKeyEx = _refused
SetValue = _refused
SetValueEx = _refused
SaveKey = _refused
LoadKey = _refused


def CloseKey(hkey):
    pass


def FlushKey(hkey):
    pass


def ExpandEnvironmentStrings(string):
    """%NAME% is replaced by the variable NAME, kept when it is not set."""
    return re.sub(
        r"%([^%]+)%", lambda m: os.environ.get(m.group(1), m.group(0)), string
    )
