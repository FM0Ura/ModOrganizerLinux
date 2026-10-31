from __future__ import annotations

from pathlib import Path
from PyQt6.QtWidgets import (
    QDialog,
    QVBoxLayout,
    QFormLayout,
    QLineEdit,
    QLabel,
    QDialogButtonBox,
)
from ..config import AssistantConfig, detect_available_clis


class AssistantSettingsDialog(QDialog):
    def __init__(self, config: AssistantConfig, data_dir: Path, parent=None) -> None:
        super().__init__(parent)
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
