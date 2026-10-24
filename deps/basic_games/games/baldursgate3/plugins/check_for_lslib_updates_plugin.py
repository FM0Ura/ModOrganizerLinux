import sys

from .bg3_tool_plugin import BG3ToolPlugin
from .icons import download


class BG3ToolCheckForLsLibUpdates(BG3ToolPlugin):
    icon_bytes = download
    sub_name = "Check For LsLib Updates"
    desc = "Check to see if there has been a new release of LSLib and create download dialog if so."

    def display(self):
        from ...game_baldursgate3 import BG3Game

        game_plugin = self._organizer.managedGame()
        if isinstance(game_plugin, BG3Game):
            if sys.platform != "win32":
                # Linux: an explicit tool always tells why nothing happens
                game_plugin.utils.warn_load_order_unavailable(force=True)
                return
            game_plugin.utils.lslib_retriever.download_lslib_if_missing(True)
