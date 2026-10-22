import os
import sys

from PyQt6.QtWidgets import QApplication


def pytest_configure():
    global app

    # Linux: the support modules find uibase by their RUNPATH
    if sys.platform == "win32":
        os.add_dll_directory(str(os.getenv("UIBASE_PATH")))

    app = QApplication(sys.argv)
