from __future__ import annotations

from dataclasses import dataclass
import json
from typing import Callable, Any
from pydantic import BaseModel
from langchain_core.tools import StructuredTool, BaseTool


@dataclass
class ToolSpec:
    name: str
    description: str
    args_schema: type[BaseModel]
    func: Callable[..., dict[str, Any]]
    is_mutating: bool = False
    destructive: bool = False


class ToolRegistry:
    def __init__(self) -> None:
        self._specs: dict[str, ToolSpec] = {}

    def register(
        self,
        *,
        name: str,
        description: str,
        args_schema: type[BaseModel],
        is_mutating: bool = False,
        destructive: bool = False,
    ) -> Callable:
        def decorator(func: Callable[..., dict[str, Any]]) -> Callable[..., dict[str, Any]]:
            self._specs[name] = ToolSpec(
                name=name,
                description=description,
                args_schema=args_schema,
                func=func,
                is_mutating=is_mutating,
                destructive=destructive,
            )
            return func

        return decorator

    def get(self, name: str) -> ToolSpec | None:
        return self._specs.get(name)

    def all_specs(self) -> list[ToolSpec]:
        return list(self._specs.values())

    def to_mcp_tools(self) -> list[dict[str, Any]]:
        tools: list[dict[str, Any]] = []
        for spec in self._specs.values():
            schema = spec.args_schema.model_json_schema()
            tools.append(
                {
                    "name": spec.name,
                    "description": spec.description,
                    "inputSchema": schema,
                }
            )
        return tools

    def to_langchain_tools(self, dispatcher) -> tuple[list[BaseTool], list[BaseTool]]:
        safe_tools: list[BaseTool] = []
        write_tools: list[BaseTool] = []
        for spec in self._specs.values():
            def _make_runner(tool_name: str):
                def _run(**kwargs) -> str:
                    res = dispatcher.execute(tool_name, kwargs)
                    return json.dumps(res, ensure_ascii=False)
                return _run

            lc_tool = StructuredTool.from_function(
                func=_make_runner(spec.name),
                name=spec.name,
                description=spec.description,
                args_schema=spec.args_schema,
            )
            if spec.is_mutating:
                write_tools.append(lc_tool)
            else:
                safe_tools.append(lc_tool)
        return safe_tools, write_tools
