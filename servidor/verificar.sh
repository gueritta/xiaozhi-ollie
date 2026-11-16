#!/bin/zsh
# Checa tudo que o servidor do Watcher precisa, sem mudar nada.
DIR="${0:A:h}"
set -a; source "$DIR/.env"; set +a
ok()   { print -P "%F{green}✔%f $1"; }
erro() { print -P "%F{red}✘%f $1"; }

"$DIR/testar-api.sh" --sem-som

herdr agent list >/dev/null 2>&1 && ok "herdr respondendo" || erro "herdr não responde"
command -v claude >/dev/null && ok "claude $(claude --version 2>/dev/null | head -1)" || erro "claude ausente"
command -v codex  >/dev/null && ok "$(codex --version 2>/dev/null | head -1)" || erro "codex ausente"
multica auth status >/dev/null 2>&1 && ok "Multica autenticado" || erro "Multica sem login (rode: multica login)"
multica daemon status 2>/dev/null | grep -qi running && ok "Multica daemon rodando" || erro "Multica daemon parado (rode: multica daemon start)"

lsof -nP -iTCP:8000 -sTCP:LISTEN >/dev/null 2>&1 \
  || ss -ltn 2>/dev/null | grep -q ':8000 ' \
  && ok "servidor ouvindo na porta 8000" || erro "servidor parado"
# OTA: rede local (URL_BASE_OTA) ou Funnel do Tailscale
if [[ -n "${URL_BASE_OTA:-}" ]]; then
  base_ota="$URL_BASE_OTA"; rotulo="LAN"
else
  base_ota="https://$HOST_PUBLICO/$SEGREDO/http"; rotulo="Funnel"
fi
if curl -s -o /dev/null -w '%{http_code}' -X POST "$base_ota/xiaozhi/ota/" \
     -H 'Device-Id: 00:00:00:00:00:00' -H 'Client-Id: verificar' -H 'Content-Type: application/json' \
     -d '{"application":{"version":"0"}}' | grep -q 200; then
  ok "$rotulo: OTA respondendo em $base_ota/xiaozhi/ota/"
else
  erro "$rotulo: OTA não responde em $base_ota/xiaozhi/ota/ (servidor parado ou URL_BASE_OTA errado)"
fi
if [[ -z "${URL_BASE_OTA:-}" ]]; then
  code=$(curl -s -o /dev/null -w '%{http_code}' "https://$HOST_PUBLICO/")
  [[ "$code" != 200 ]] && ok "Raiz pública fechada (HTTP $code)" || erro "Raiz pública aberta!"
fi
