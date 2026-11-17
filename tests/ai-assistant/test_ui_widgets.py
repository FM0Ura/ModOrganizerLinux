from __future__ import annotations

from PyQt6.QtWidgets import QMainWindow
from ai_assistant import createPlugin
from ai_assistant.ui.approval_card import ApprovalCard


def test_approval_card_emits_decisions():
    received = []
    card = ApprovalCard("req-1", "remove_mod", {"mod_name": "BadMod"}, destructive=True)
    card.decision_made.connect(lambda req_id, dec: received.append((req_id, dec)))
    card.approve_btn.click()
    assert received == [("req-1", "approve")]


def test_reinit_in_same_process_is_clean(fake_organizer):
    plugin = createPlugin()
    assert plugin.init(fake_organizer) is True
    assert hasattr(fake_organizer, "_ui_callback")

    win1 = QMainWindow()
    fake_organizer._ui_callback(win1)
    assert plugin._dock is not None
    first_port = plugin._mcp_server.port
    assert first_port > 0

    # Simulate MO2_SMOKE_RESTART_ONCE in same process with a new QMainWindow
    win2 = QMainWindow()
    fake_organizer._ui_callback(win2)
    assert plugin._dock.parentWidget() is win2
    assert plugin._mcp_server.port == first_port

    # Check quick action and mode toggle in chat panel
    panel = plugin._dock.chat_panel
    panel.mode_combo.setCurrentIndex(1)  # Autonomous
    assert plugin._dispatcher._permission_mode == "autonomous"
    plugin._mcp_server.stop()
