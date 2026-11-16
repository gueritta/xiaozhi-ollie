#!/bin/zsh
# Sobe o xiaozhi-server com a ponte MCP.
# Só delega ao iniciar.py, que gera a configuração, copia plugins/extras e dá exec no
# servidor — assim o modo manual (./iniciar.sh) e o serviço (iniciar.py) fazem o mesmo.
# Uso: ./iniciar.sh                        (lê o .env desta pasta)
#      ENV_FILE=/outro/.env ./iniciar.sh
set -euo pipefail

DIR="${0:A:h}"
PYTHON="$DIR/xiaozhi-server/.venv/bin/python"

if [[ ! -f "${ENV_FILE:-$DIR/.env}" ]]; then
  echo "Falta o arquivo ${ENV_FILE:-$DIR/.env} (copie de .env.exemplo)." >&2
  exit 1
fi
if [[ ! -x "$PYTHON" ]]; then
  echo "Ambiente não instalado: rode ./instalar.sh antes." >&2
  exit 1
fi

export PATH="$HOME/.local/bin:/usr/local/bin:/usr/bin:/bin:/usr/sbin:/sbin"
exec "$PYTHON" "$DIR/iniciar.py"
