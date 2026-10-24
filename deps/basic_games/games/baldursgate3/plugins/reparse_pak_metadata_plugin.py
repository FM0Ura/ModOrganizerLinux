import sys

from .bg3_tool_plugin import BG3ToolPlugin
from .icons import refresh


class BG3ToolReparsePakMetadata(BG3ToolPlugin):
    icon_bytes = refresh
    sub_name = "Reparse Pak Metadata"
    desc = "Force reparsing mod metadata immediately."

    def display(self):
        from ...game_baldursgate3 import BG3Game

        game_plugin = self._organizer.managedGame()
        if isinstance(game_plugin, BG3Game):
            if sys.platform != "win32":
                # Linux: an explicit tool always tells why nothing happens
                game_plugin.utils.warn_load_order_unavailable(force=True)
                return
            game_plugin.utils.construct_modsettings_xml(
                exec_path="bin/bg3", force_reparse_metadata=True
            )
