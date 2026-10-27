import mobase


class CyclicHelper:
    def __init__(self, plugin: "CyclicPlugin"):
        self.plugin = plugin


class CyclicPlugin(mobase.IPlugin):
    # the plugin and its helper reference each other, like the objects of plugins such
    # as RootBuilder: only the garbage collector can free them
    def __init__(self):
        super().__init__()
        self._helper = CyclicHelper(self)

    def init(self, organizer: mobase.IOrganizer) -> bool:
        return True

    def author(self) -> str:
        return "The Author"

    def name(self) -> str:
        return "The Cyclic Name"

    def description(self) -> str:
        return "The Description"

    def version(self) -> mobase.VersionInfo:
        return mobase.VersionInfo("1.0.0")

    def settings(self) -> list[mobase.PluginSetting]:
        return []


def createPlugin() -> mobase.IPlugin:
    return CyclicPlugin()
