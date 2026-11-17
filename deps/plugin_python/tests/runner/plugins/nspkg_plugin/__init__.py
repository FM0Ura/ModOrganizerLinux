import mobase

# a sub-package without __init__.py is a namespace package, like rootbuilder.plugin in
# RootBuilder: its __path__ is recalculated from the parent package in sys.modules; it
# is imported by createPlugin(), after this package, so that it comes after the package
# in sys.modules (as in RootBuilder)


class NamespaceCyclicHelper:
    def __init__(self, plugin: "NamespaceCyclicPlugin"):
        self.plugin = plugin


class NamespaceCyclicPlugin(mobase.IPlugin):
    def __init__(self):
        super().__init__()
        self._helper = NamespaceCyclicHelper(self)
        from .inner import helper

        self._value = helper.VALUE

    def init(self, organizer: mobase.IOrganizer) -> bool:
        return True

    def author(self) -> str:
        return "The Author"

    def name(self) -> str:
        return "The Namespace Cyclic Name"

    def description(self) -> str:
        return "The Description"

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo("1.0.0")

    def settings(self) -> list[mobase.PluginSetting]:
        return []


def createPlugin() -> mobase.IPlugin:
    return NamespaceCyclicPlugin()
