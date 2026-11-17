from __future__ import annotations

import html
import json
from PyQt6.QtCore import pyqtSignal
from PyQt6.QtWidgets import QFrame, QVBoxLayout, QHBoxLayout, QLabel, QPushButton


class ApprovalCard(QFrame):
    decision_made = pyqtSignal(str, str)

    def __init__(self, request_id: str, tool_name: str, args: dict, destructive: bool = False, parent=None) -> None:
        super().__init__(parent)
        self.request_id = request_id
        self.setFrameShape(QFrame.Shape.StyledPanel)
        border_color = "#d9534f" if destructive else "#f0ad4e"
        self.setStyleSheet(f"QFrame {{ border: 1px solid {border_color}; border-radius: 6px; padding: 6px; }}")

        layout = QVBoxLayout(self)
        title = QLabel(f"<b>{'[Destructive] ' if destructive else ''}Pending Action: {html.escape(tool_name)}</b>")
        args_label = QLabel(f"<code>{html.escape(json.dumps(args, ensure_ascii=False))}</code>")
        args_label.setWordWrap(True)
        layout.addWidget(title)
        layout.addWidget(args_label)

        btn_row = QHBoxLayout()
        self.approve_btn = QPushButton("Approve")
        self.approve_all_btn = QPushButton("Approve All in Session")
        self.reject_btn = QPushButton("Reject")

        self.approve_btn.clicked.connect(lambda: self._decide("approve"))
        self.approve_all_btn.clicked.connect(lambda: self._decide("approve_session"))
        self.reject_btn.clicked.connect(lambda: self._decide("reject"))

        btn_row.addWidget(self.approve_btn)
        btn_row.addWidget(self.approve_all_btn)
        btn_row.addWidget(self.reject_btn)
        layout.addLayout(btn_row)

    def _decide(self, decision: str) -> None:
        self.approve_btn.setEnabled(False)
        self.approve_all_btn.setEnabled(False)
        self.reject_btn.setEnabled(False)
        self.decision_made.emit(self.request_id, decision)
