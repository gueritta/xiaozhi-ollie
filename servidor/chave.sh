#!/bin/zsh
# Guarda uma chave no .env sem ecoar na tela nem deixar rastro no histórico do shell.
# Uso: ./chave.sh tts|asr|llm      (tts = ElevenLabs, asr = Groq, llm = OpenRouter)
set -euo pipefail

DIR="${0:A:h}"
ARQ="${ENV_FILE:-$DIR/.env}"

case "${1:-}" in
  tts) NOME="API_KEY_TTS" ;;   # ElevenLabs
  asr) NOME="API_KEY_ASR" ;;   # Groq (whisper-large-v3)
  llm) NOME="API_KEY" ;;       # OpenRouter (o modelo de conversa)
  *)   echo "Uso: $0 tts|asr|llm" >&2; exit 2 ;;
esac

[[ -f "$ARQ" ]] || { echo "Não achei $ARQ" >&2; exit 1; }

printf '%s (cole e Enter, nada aparece na tela): ' "$NOME"
read -rs VALOR
echo
[[ -n "$VALOR" ]] || { echo "vazio: nada mudou" >&2; exit 1; }

# A chave vai por variável de ambiente (não por argv), então não aparece em `ps` nem no histórico
NOME="$NOME" VALOR="$VALOR" /usr/bin/python3 - "$ARQ" <<'PY'
import os, pathlib, sys
nome, valor, arq = os.environ["NOME"], os.environ["VALOR"], pathlib.Path(sys.argv[1])
linhas = arq.read_text(encoding="utf-8").splitlines()
alvo = f"{nome}="
for i, linha in enumerate(linhas):
    if linha.startswith(alvo):
        linhas[i] = alvo + valor
        break
else:
    linhas.append(alvo + valor)
arq.write_text("\n".join(linhas) + "\n", encoding="utf-8")
PY

chmod 600 "$ARQ"
echo "ok: $NOME salvo em $ARQ (${#VALOR} caracteres)"
