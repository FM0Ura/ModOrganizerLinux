from __future__ import annotations

from datetime import datetime
from typing import Callable

from PyQt6.QtCore import Qt, pyqtSignal
from PyQt6.QtWidgets import (
    QHBoxLayout,
    QInputDialog,
    QLabel,
    QLineEdit,
    QListWidget,
    QListWidgetItem,
    QMenu,
    QMessageBox,
    QPushButton,
    QVBoxLayout,
    QWidget,
)

from ..history.store import ConversationStore

ID_ROLE = Qt.ItemDataRole.UserRole


def _local_stamp(iso: str) -> str:
    try:
        return datetime.fromisoformat(iso.replace("Z", "+00:00")).astimezone().strftime("%Y-%m-%d %H:%M")
    except ValueError:
        return iso


def _provider_label(key: str) -> str:
    from .chat_panel import PROVIDER_LABELS  # late: chat_panel imports this module

    return dict(PROVIDER_LABELS).get(key, key)


class HistoryView(QWidget):
    """Search, reopen, rename, export and delete saved conversations."""

    open_requested = pyqtSignal(str)
    back_requested = pyqtSignal()
    export_requested = pyqtSignal(str)
    delete_requested = pyqtSignal(str)
    rename_requested = pyqtSignal(str, str)

    def __init__(self, store_getter: Callable[[], ConversationStore], parent=None) -> None:
        super().__init__(parent)
        self._store_getter = store_getter
        layout = QVBoxLayout(self)
        top = QHBoxLayout()
        self.back_btn = QPushButton(self.tr("← Back"))
        self.back_btn.clicked.connect(self.back_requested)
        self.search = QLineEdit()
        self.search.setPlaceholderText(self.tr("Search conversations..."))
        self.search.setClearButtonEnabled(True)
        self.search.textChanged.connect(self.refresh)
        top.addWidget(self.back_btn)
        top.addWidget(self.search, 1)
        layout.addLayout(top)

        self.list = QListWidget()
        self.list.setContextMenuPolicy(Qt.ContextMenuPolicy.CustomContextMenu)
        self.list.customContextMenuRequested.connect(self._on_context_menu)
        self.list.itemActivated.connect(lambda item: self.open_requested.emit(item.data(ID_ROLE)))
        layout.addWidget(self.list, 1)

        self.message_label = QLabel()
        self.message_label.setWordWrap(True)
        layout.addWidget(self.message_label)

    def show_message(self, text: str) -> None:
        self.message_label.setText(text)

    def refresh(self, query: str | None = None) -> None:
        """Reloads the list; with no query it keeps the text typed in the search box."""
        if not isinstance(query, str):
            query = self.search.text()
        store = self._store_getter()
        entries = store.search(query.strip()) if query.strip() else store.list()
        self.message_label.clear()
        self.list.clear()
        for e in entries:
            text = "{}  ·  {} · {}".format(
                e["title"] or self.tr("(untitled)"), _local_stamp(e["updated_at"]), _provider_label(e["provider"])
            )
            item = QListWidgetItem(text)
            item.setData(ID_ROLE, e["id"])
            self.list.addItem(item)
        if not entries:
            self.message_label.setText(self.tr("No conversations found."))

    def _on_context_menu(self, pos) -> None:
        item = self.list.itemAt(pos)
        if item is None:
            return
        conv_id = item.data(ID_ROLE)
        menu = QMenu(self)
        menu.addAction(self.tr("Rename"), lambda: self.rename(conv_id))
        menu.addAction(self.tr("Export .md"), lambda: self.export_requested.emit(conv_id))
        menu.addAction(self.tr("Delete"), lambda: self.delete(conv_id))
        menu.popup(self.list.viewport().mapToGlobal(pos))

    def _title_of(self, conv_id: str) -> str:
        for i in range(self.list.count()):
            item = self.list.item(i)
            if item.data(ID_ROLE) == conv_id:
                return item.text().split("  ·  ")[0]
        return ""

    def rename(self, conv_id: str) -> None:
        title, ok = QInputDialog.getText(
            self, self.tr("Rename conversation"), self.tr("Title:"), text=self._title_of(conv_id)
        )
        if ok and title.strip():
            self.rename_requested.emit(conv_id, title)

    def delete(self, conv_id: str) -> None:
        answer = QMessageBox.question(
            self,
            self.tr("Delete conversation"),
            self.tr("Delete \"{0}\"? This cannot be undone.").format(self._title_of(conv_id)),
        )
        if answer == QMessageBox.StandardButton.Yes:
            self.delete_requested.emit(conv_id)
