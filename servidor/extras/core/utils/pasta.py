"""Pasta compartilhada do Watcher: iCloud Drive no macOS, pasta local/sincronizada no resto.

WATCHER_PASTA é a raiz (padrão: iCloud Drive/Watcher no macOS, ~/Watcher nos outros sistemas).
Aponte-a para um diretório do Syncthing/Nextcloud/rclone e o espelhamento continua igual.
"""

import os
import platform
from pathlib import Path

_PADRAO_MAC = Path.home() / "Library/Mobile Documents/com~apple~CloudDocs/Watcher"

RAIZ = Path(os.environ.get("WATCHER_PASTA")
            or (_PADRAO_MAC if platform.system() == "Darwin" else Path.home() / "Watcher"))


def sub(nome: str) -> Path:
    """Caminho de uma subpasta/arquivo dentro da raiz (quem escreve cria o diretório)."""
    return RAIZ / nome
