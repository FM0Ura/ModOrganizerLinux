from __future__ import annotations

import stat
from pathlib import Path
from ai_assistant import createPlugin
from ai_assistant.config import AssistantConfig, detect_available_clis


def test_config_roundtrip_and_0600_permissions(tmp_path: Path):
    data_dir = tmp_path / "ai_assistant"
    cfg = AssistantConfig(
        provider="claude_cli",
        permission_mode="safe",
        model_name="claude-3-7-sonnet",
        api_keys={"openai": "sk-secret-key-123"},
    )
    cfg.save(data_dir)
    cfg_file = data_dir / "config.json"
    assert cfg_file.exists()
    mode = stat.S_IMODE(cfg_file.stat().st_mode)
    assert mode == 0o600

    loaded = AssistantConfig.load(data_dir)
    assert loaded.provider == "claude_cli"
    assert loaded.permission_mode == "safe"
    assert loaded.api_keys["openai"] == "sk-secret-key-123"


def test_detect_available_clis_returns_expected_keys():
    clis = detect_available_clis()
    assert set(clis.keys()) == {"claude", "codex", "agy"}
    for val in clis.values():
        assert isinstance(val, bool)


def test_plugin_metadata_and_hermetic_init(fake_organizer, tmp_path: Path):
    plugin = createPlugin()
    assert plugin.name() == "AI Assistant"
    assert plugin.init(fake_organizer) is True
    # Hermetic check: init() alone must not write config.json before user action
    assert not (Path(fake_organizer.pluginDataPath()) / "ai_assistant/config.json").exists()


def test_plugin_icon_is_the_bundled_svg():
    plugin = createPlugin()
    assert plugin.icon().isNull() is False
