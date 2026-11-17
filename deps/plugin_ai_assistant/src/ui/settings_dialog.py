from __future__ import annotations

import subprocess
from pathlib import Path
from typing import Callable
from PyQt6.QtWidgets import (
    QDialog,
    QVBoxLayout,
    QFormLayout,
    QLineEdit,
    QLabel,
    QDialogButtonBox,
    QHBoxLayout,
    QPushButton,
)
from ..config import AssistantConfig, detect_available_clis
from ..agent.cli_runner import agy_register_command


def _run_command(cmd: list[str]) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, capture_output=True, text=True, timeout=30, check=False)


class AssistantSettingsDialog(QDialog):
    def __init__(
        self,
        config: AssistantConfig,
        data_dir: Path,
        parent=None,
        bridge_path: Path | None = None,
        endpoint_path: Path | None = None,
        run_command: Callable[[list[str]], subprocess.CompletedProcess] = _run_command,
    ) -> None:
        super().__init__(parent)
        self._bridge_path = bridge_path
        self._endpoint_path = endpoint_path
        self._run_command = run_command
        self.setWindowTitle("AI Assistant Settings")
        self._config = config
        self._data_dir = data_dir

        layout = QVBoxLayout(self)
        clis = detect_available_clis()
        cli_status = " | ".join(
            f"{name}: {'Available' if ok else 'Not found'}" for name, ok in clis.items()
        )
        layout.addWidget(QLabel(f"<b>Host CLI Agents:</b> {cli_status}"))

        form = QFormLayout()
        self.model_edit = QLineEdit(self._config.model_name)
        self.ollama_url_edit = QLineEdit(self._config.ollama_base_url)
        self.openai_url_edit = QLineEdit(self._config.openai_base_url)

        self.gemini_key_edit = QLineEdit(self._config.api_keys.get("gemini", ""))
        self.gemini_key_edit.setEchoMode(QLineEdit.EchoMode.Password)

        self.openai_key_edit = QLineEdit(self._config.api_keys.get("openai", ""))
        self.openai_key_edit.setEchoMode(QLineEdit.EchoMode.Password)

        self.anthropic_key_edit = QLineEdit(self._config.api_keys.get("anthropic", ""))
        self.anthropic_key_edit.setEchoMode(QLineEdit.EchoMode.Password)

        form.addRow("Model Override (optional):", self.model_edit)
        form.addRow("Ollama Base URL:", self.ollama_url_edit)
        form.addRow("OpenAI-Compatible URL:", self.openai_url_edit)
        form.addRow("Google Gemini API Key:", self.gemini_key_edit)
        form.addRow("OpenAI / OpenRouter API Key:", self.openai_key_edit)
        form.addRow("Anthropic API Key:", self.anthropic_key_edit)
        layout.addLayout(form)

        # Antigravity has no per-session MCP option: register the MO2 server once
        agy_row = QHBoxLayout()
        self.agy_register_btn = QPushButton("Register in Antigravity")
        self.agy_register_btn.setToolTip(
            "Runs 'agy mcp add mo2 …' so that the Antigravity CLI can use the MO2 tools"
        )
        self.agy_register_btn.setEnabled(bridge_path is not None and endpoint_path is not None)
        self.agy_register_btn.clicked.connect(self._on_register_agy)
        self.agy_status = QLabel()
        self.agy_status.setWordWrap(True)
        agy_row.addWidget(self.agy_register_btn)
        agy_row.addWidget(self.agy_status, 1)
        layout.addLayout(agy_row)

        buttons = QDialogButtonBox(
            QDialogButtonBox.StandardButton.Save | QDialogButtonBox.StandardButton.Cancel
        )
        buttons.accepted.connect(self._on_save)
        buttons.rejected.connect(self.reject)
        layout.addWidget(buttons)

    def _on_save(self) -> None:
        self._config.model_name = self.model_edit.text().strip()
        self._config.ollama_base_url = self.ollama_url_edit.text().strip() or "http://localhost:11434/v1"
        self._config.openai_base_url = self.openai_url_edit.text().strip() or "https://api.openai.com/v1"
        self._config.api_keys["gemini"] = self.gemini_key_edit.text().strip()
        self._config.api_keys["openai"] = self.openai_key_edit.text().strip()
        self._config.api_keys["anthropic"] = self.anthropic_key_edit.text().strip()
        self._config.save(self._data_dir)
        self.accept()

    def _on_register_agy(self) -> None:
        cmd = agy_register_command(self._bridge_path, self._endpoint_path)
        try:
            proc = self._run_command(cmd)
        except (OSError, subprocess.SubprocessError) as exc:
            self.agy_status.setText(f"Failed: {exc}")
            return
        if proc.returncode == 0:
            self.agy_status.setText("Registered: Antigravity can now use the MO2 tools.")
        else:
            detail = (proc.stderr or proc.stdout or f"exit code {proc.returncode}").strip()
            self.agy_status.setText(f"Failed: {detail}")
