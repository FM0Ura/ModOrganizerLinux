from .registry import ToolRegistry, ToolSpec
from .dispatcher import MainThreadDispatcher
from .mod_tools import register_mod_tools
from .plugin_tools import register_plugin_tools
from .diagnostic_tools import register_diagnostic_tools


def build_default_registry() -> ToolRegistry:
    reg = ToolRegistry()
    register_diagnostic_tools(reg)
    register_mod_tools(reg)
    register_plugin_tools(reg)
    return reg


__all__ = ["ToolRegistry", "ToolSpec", "MainThreadDispatcher", "build_default_registry"]
