from __future__ import annotations

from dataclasses import dataclass, field, asdict
import json
import os
import shutil
import subprocess
from pathlib import Path

PROVIDERS = (
    "claude_cli",
    "codex_cli",
    "agy_cli",
    "ollama",
    "gemini",
    "openai",
    "anthropic",
)

PERMISSION_MODES = ("safe", "autonomous")


def _in_container() -> bool:
    return os.path.exists("/.dockerenv") or os.path.exists("/run/.containerenv")


def detect_available_clis() -> dict[str, bool]:
    result: dict[str, bool] = {}
    in_box = _in_container() and shutil.which("distrobox-host-exec") is not None
    for binary in ("claude", "codex", "agy"):
        if shutil.which(binary) is not None:
            result[binary] = True
        elif in_box:
            try:
                proc = subprocess.run(
                    ["distrobox-host-exec", "sh", "-c", f"command -v {binary}"],
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                    timeout=3,
                    check=False,
                )
                result[binary] = proc.returncode == 0
            except Exception:
                result[binary] = False
        else:
            result[binary] = False
    return result


@dataclass
class AssistantConfig:
    provider: str = "claude_cli"
    permission_mode: str = "safe"
    model_name: str = ""
    ollama_base_url: str = "http://localhost:11434/v1"
    openai_base_url: str = "https://api.openai.com/v1"
    gemini_base_url: str = "https://generativelanguage.googleapis.com/v1beta/openai/"
    api_keys: dict[str, str] = field(default_factory=dict)

    @classmethod
    def load(cls, data_dir: Path) -> AssistantConfig:
        cfg_file = Path(data_dir) / "config.json"
        if not cfg_file.is_file():
            return cls()
        try:
            raw = json.loads(cfg_file.read_text(encoding="utf-8"))
            return cls(
                provider=raw.get("provider", "claude_cli"),
                permission_mode=raw.get("permission_mode", "safe"),
                model_name=raw.get("model_name", ""),
                ollama_base_url=raw.get("ollama_base_url", "http://localhost:11434/v1"),
                openai_base_url=raw.get("openai_base_url", "https://api.openai.com/v1"),
                gemini_base_url=raw.get(
                    "gemini_base_url",
                    "https://generativelanguage.googleapis.com/v1beta/openai/",
                ),
                api_keys=dict(raw.get("api_keys") or {}),
            )
        except Exception:
            return cls()

    def save(self, data_dir: Path) -> None:
        data_path = Path(data_dir)
        data_path.mkdir(parents=True, exist_ok=True)
        cfg_file = data_path / "config.json"
        tmp_file = data_path / "config.json.tmp"
        fd = os.open(str(tmp_file), os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as f:
                json.dump(asdict(self), f, indent=2, ensure_ascii=False)
            os.chmod(tmp_file, 0o600)
            os.replace(tmp_file, cfg_file)
            os.chmod(cfg_file, 0o600)
        finally:
            if tmp_file.exists():
                tmp_file.unlink(missing_ok=True)
