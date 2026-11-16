"""Inicializa o servidor do Watcher (usado pelo LaunchAgent).

Faz o mesmo que iniciar.sh, mas em Python: o macOS bloqueia o zsh iniciado pelo
launchd de ler o disco externo, e o Python do servidor tem acesso.
1. Lê o .env, gera xiaozhi-server/data/.config.yaml e .mcp_server_settings.json.
2. Substitui este processo pelo xiaozhi-server (mesmo Python).
"""

import json
import os
import re
import sys
from pathlib import Path

DIR = Path(__file__).resolve().parent
SRV = DIR / "xiaozhi-server"
ENV_FILE = Path(os.environ.get("ENV_FILE", DIR / ".env"))

if not ENV_FILE.exists():
    sys.exit(f"Falta o arquivo {ENV_FILE} (copie de .env.exemplo).")

env = {}
for linha in ENV_FILE.read_text(encoding="utf-8").splitlines():
    m = re.match(r"^([A-Z0-9_]+)=(.*)$", linha.strip())
    if m:
        env[m.group(1)] = m.group(2).strip().strip('"')
os.environ.update(env)
os.environ.update({
    "UV_CACHE_DIR": str(DIR / ".cache/uv"),
    "XDG_CACHE_HOME": str(DIR / ".cache"),
    "PATH": ":".join([str(Path.home() / ".local/bin"), "/opt/homebrew/bin", "/usr/local/bin", "/usr/bin", "/bin", "/usr/sbin", "/sbin"]),
})

# Idioma (IDIOMA no .env): nome para os prompts e voz padrão, se VOZ estiver vazia
sys.path.insert(0, str(DIR / "extras/core/utils"))
import idioma  # noqa: E402

os.environ["IDIOMA"] = idioma.IDIOMA
os.environ["IDIOMA_NOME"] = idioma.NOME
# Idioma das respostas (vai para o prompt como {{language}}). Vazio = o do IDIOMA.
# Ex.: "English. If the user speaks French, answer in French instead."
os.environ["IDIOMA_RESPOSTA"] = os.environ.get("IDIOMA_RESPOSTA") or idioma.NOME
os.environ["OLLIE_ERRO"] = idioma.t("Tive um problema agora. Tenta de novo daqui a pouco.",
                                    "Something went wrong. Please try again in a moment.",
                                    "刚才出了点问题，请稍后再试。",
                                    "He tenido un problema. Inténtalo de nuevo en un momento.")
if not os.environ.get("VOZ"):
    os.environ["VOZ"] = idioma.VOZES[idioma.IDIOMA]

# Endereços anunciados ao aparelho. Rede local: URL_BASE_WS/URL_BASE_OTA no .env
# (ex.: ws://10.0.0.5:8000, http://10.0.0.5:8003) sem prefixo secreto. Sem elas,
# vale o Funnel do Tailscale montado a partir de HOST_PUBLICO/SEGREDO.
host = (os.environ.get("HOST_PUBLICO") or "").strip()
segredo = (os.environ.get("SEGREDO") or "").strip()
base_ws = (os.environ.get("URL_BASE_WS") or "").rstrip("/")
base_ota = (os.environ.get("URL_BASE_OTA") or "").rstrip("/")
if base_ws:
    os.environ["URL_WS"] = base_ws + "/xiaozhi/v1/"
elif host and segredo:
    os.environ["URL_WS"] = f"wss://{host}/{segredo}/ws/xiaozhi/v1/"
if base_ota:
    os.environ["URL_VISAO"] = base_ota + "/mcp/vision/explain"
elif host and segredo:
    os.environ["URL_VISAO"] = f"https://{host}/{segredo}/http/mcp/vision/explain"
os.environ.setdefault("IP_ESCUTA", "0.0.0.0" if base_ws else "127.0.0.1")

# ASR e TTS podem usar outro provedor (e outra chave) que não o do LLM: a OpenRouter
# não tem modelo de transcrição para /audio/transcriptions. Vazio = usa API_BASE_URL/API_KEY.
os.environ["API_KEY_ASR"] = os.environ.get("API_KEY_ASR") or os.environ.get("API_KEY", "")
os.environ["BASE_ASR"] = (os.environ.get("URL_BASE_ASR") or os.environ["API_BASE_URL"]).rstrip("/")
os.environ["API_KEY_TTS"] = os.environ.get("API_KEY_TTS") or os.environ.get("API_KEY", "")
os.environ["BASE_TTS"] = (os.environ.get("URL_BASE_TTS") or os.environ["API_BASE_URL"]).rstrip("/")
# Provedor de voz: OpenAITTS (endpoint /audio/speech) ou ElevenTTS (ElevenLabs, type: custom)
os.environ["TTS_MODULO"] = os.environ.get("TTS_MODULO") or "OpenAITTS"
os.environ["VOZ_ID"] = os.environ.get("VOZ_ID") or "21m00Tcm4TlvDq8ikWAM"   # Rachel (predefinida)
os.environ["ELEVEN_MODELO"] = os.environ.get("ELEVEN_MODELO") or "eleven_flash_v2_5"

faltando = set()


def troca(m: re.Match) -> str:
    valor = os.environ.get(m.group(1))
    if not valor:
        faltando.add(m.group(1))
        return m.group(0)
    return valor


config = re.sub(r"\$\{([A-Z0-9_]+)\}", troca, (DIR / "config.template.yaml").read_text(encoding="utf-8"))
if faltando:
    sys.exit(f"Variáveis faltando no .env: {', '.join(sorted(faltando))}")

(SRV / "data").mkdir(exist_ok=True)
destino = SRV / "data/.config.yaml"
destino.write_text(config, encoding="utf-8")
destino.chmod(0o600)

mcp = {
    "mcpServers": {
        "mac-mini": {
            "command": str(SRV / ".venv/bin/python"),
            "args": [str(DIR / "ponte-mcp/ponte.py")],
            "env": {"PONTE_RAIZES": env.get("PONTE_RAIZES", str(Path.home() / "dev")), "HOME": str(Path.home())},
        }
    }
}
(SRV / "data/.mcp_server_settings.json").write_text(json.dumps(mcp, indent=2), encoding="utf-8")

# Ferramentas próprias (plugins próprios) -> plugins_func/functions
import shutil
for arq in (DIR / "plugins").glob("*.py"):
    shutil.copy(arq, SRV / "plugins_func/functions" / arq.name)
# Prompt do agente (editado em arquivos/): copiado a cada início, não só no instalar.sh
agente = DIR / "arquivos/agent-base-prompt-ptbr.txt"
if agente.exists():
    shutil.copy(agente, SRV / agente.name)
# Módulos próprios do servidor (extras/ espelha a árvore do xiaozhi-server)
for arq in (DIR / "extras").rglob("*.py"):
    destino_extra = SRV / arq.relative_to(DIR / "extras")
    destino_extra.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy(arq, destino_extra)

os.chdir(SRV)
python = str(SRV / ".venv/bin/python")
os.execv(python, [python, "app.py"])
