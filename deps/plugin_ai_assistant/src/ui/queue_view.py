from __future__ import annotations

from PyQt6.QtCore import pyqtSignal
from PyQt6.QtWidgets import QHBoxLayout, QLabel, QPushButton, QVBoxLayout, QWidget


class QueueView(QWidget):
    """The messages waiting for the running turn, one row each with a remove button."""

    remove_requested = pyqtSignal(int)

    def __init__(self, parent=None) -> None:
        super().__init__(parent)
        self._layout = QVBoxLayout(self)
        self._layout.setContentsMargins(0, 0, 0, 0)
        self.hide()

    def set_items(self, items: list[str]) -> None:
        while self._layout.count():
            child = self._layout.takeAt(0)
            widget = child.widget()
            if widget is not None:
                widget.setParent(None)
                widget.deleteLater()
        for index, text in enumerate(items):
            row = QWidget(self)
            row_layout = QHBoxLayout(row)
            row_layout.setContentsMargins(0, 0, 0, 0)
            label = QLabel(self.tr("⏳ Queued: {0}").format(" ".join(text.split())), row)
            label.setToolTip(text)
            remove = QPushButton("✕", row)
            remove.setFixedWidth(28)
            remove.setToolTip(self.tr("Remove from the queue"))
            remove.clicked.connect(lambda _=False, i=index: self.remove_requested.emit(i))
            row_layout.addWidget(label, 1)
            row_layout.addWidget(remove)
            self._layout.addWidget(row)
        self.setVisible(bool(items))
