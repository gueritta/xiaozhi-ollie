#!/bin/zsh
# Instala o servidor como serviço de login: sobe no login e reinicia se cair.
# macOS: LaunchAgent (launchd). Linux: unidade de usuário do systemd.
# Uso: ./instalar-servico.sh [instalar|remover|reiniciar|log]
set -euo pipefail
DIR="${0:A:h}"
[[ -f "$DIR/.env" ]] && { set -a; source "$DIR/.env"; set +a; }
ROTULO="${WATCHER_ROTULO:-com.xiaozhi-ollie.servidor}"
# Python real do venv (o launchd não deixa o zsh ler o disco externo; o Python consegue)
PYTHON="$(readlink -f "$DIR/xiaozhi-server/.venv/bin/python")"
mkdir -p "$DIR/logs"

ACao="${1:-instalar}"

if [[ "$(uname -s)" == "Darwin" ]]; then
  PLIST="$HOME/Library/LaunchAgents/$ROTULO.plist"
  LOG="$HOME/Library/Logs/watcher-servidor.log"
  mkdir -p "$(dirname "$PLIST")"
  case "$ACao" in
    instalar)
      cat > "$PLIST" <<XML
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>Label</key><string>$ROTULO</string>
  <key>ProgramArguments</key><array><string>$PYTHON</string><string>$DIR/iniciar.py</string></array>
  <key>WorkingDirectory</key><string>$HOME</string>
  <key>RunAtLoad</key><true/>
  <key>KeepAlive</key><true/>
  <key>ThrottleInterval</key><integer>15</integer>
  <key>StandardOutPath</key><string>$LOG</string>
  <key>StandardErrorPath</key><string>$LOG</string>
</dict>
</plist>
XML
      launchctl bootout "gui/$(id -u)/$ROTULO" 2>/dev/null || true
      launchctl bootstrap "gui/$(id -u)" "$PLIST"
      echo "Serviço instalado. Log: $LOG"
      ;;
    remover)
      launchctl bootout "gui/$(id -u)/$ROTULO" 2>/dev/null || true
      rm -f "$PLIST"
      ;;
    reiniciar)
      launchctl kickstart -k "gui/$(id -u)/$ROTULO"
      ;;
    log)
      tail -f "$LOG"
      ;;
  esac
  exit 0
fi

# ---- Linux: unidade de usuário do systemd ------------------------------------
UNIDADE="$HOME/.config/systemd/user/$ROTULO.service"
mkdir -p "$(dirname "$UNIDADE")"
UID_ATUAL="$(id -u)"
case "$ACao" in
  instalar)
    cat > "$UNIDADE" <<UNIT
[Unit]
Description=Ollie (xiaozhi-server + ponte MCP)
After=network-online.target
Wants=network-online.target

[Service]
Type=simple
WorkingDirectory=$HOME
ExecStart=$PYTHON $DIR/iniciar.py
Restart=always
RestartSec=15
# Notificações de área de trabalho a partir do serviço de usuário
Environment=DBUS_SESSION_BUS_ADDRESS=unix:path=/run/user/$UID_ATUAL/bus
Environment=PATH=$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin

[Install]
WantedBy=default.target
UNIT
    systemctl --user daemon-reload
    systemctl --user enable --now "$ROTULO.service"
    # Continua rodando mesmo sem sessão aberta (equivale ao RunAtLoad do launchd)
    loginctl enable-linger "$USER" 2>/dev/null || \
      echo "Aviso: rode 'sudo loginctl enable-linger $USER' para o serviço subir sem login." >&2
    echo "Serviço instalado. Log: journalctl --user -u $ROTULO -f"
    ;;
  remover)
    systemctl --user disable --now "$ROTULO.service" 2>/dev/null || true
    rm -f "$UNIDADE"
    systemctl --user daemon-reload
    ;;
  reiniciar)
    systemctl --user restart "$ROTULO.service"
    ;;
  log)
    journalctl --user -u "$ROTULO" -f
    ;;
esac
