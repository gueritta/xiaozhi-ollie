#!/bin/zsh
# Grava o XiaoZhi no SenseCAP Watcher PRESERVANDO o que já está configurado no aparelho.
#
# Partições poupadas (lidas da tabela dentro do próprio binário):
#   - nvsfactory (calibração de fábrica da Seeed)
#   - nvs        (Wi-Fi, endereço do servidor, volume, ajustes salvos)
# Ou seja: depois de gravar, o Watcher continua na mesma rede e no mesmo servidor.
# Com --limpar as duas são sobrescritas (só para devolver o aparelho ao estado de fábrica).
#
# Uso: ./gravar-xiaozhi.sh [porta] [--oficial] [--limpar]
#      ex.: ./gravar-xiaozhi.sh /dev/ttyACM0
# Atualização rápida (só programa + mascote, também sem tocar em nvs): ./atualizar-firmware.sh
set -euo pipefail

DIR="${0:A:h}"
BIN="$DIR/02-xiaozhi-v2.5.0/merged-binary.bin"
MODO="manter"
[[ " $* " == *" --limpar "* ]] && MODO="limpar"
if [[ " $* " != *" --oficial "* && -f "$DIR/xiaozhi-ollie/saida/merged-binary.bin" ]]; then
  BIN="$DIR/xiaozhi-ollie/saida/merged-binary.bin"
fi
set -- ${@:#--oficial} ${@:#--limpar}
echo "Firmware: $BIN"
if [[ $MODO == limpar ]]; then
  echo "Modo: LIMPAR (zera nvs e nvsfactory)"
else
  echo "Modo: MANTER credenciais e ajustes"
fi
BACKUPS="$DIR/backups"
export UV_CACHE_DIR="$DIR/../servidor/.cache/uv"
# esptool 5.x: usa o do sistema (Linux/macOS) se houver; senão, uvx
if command -v esptool >/dev/null 2>&1; then
  ESPTOOL=(esptool --chip esp32s3)
elif command -v esptool.py >/dev/null 2>&1; then
  ESPTOOL=(esptool.py --chip esp32s3)
else
  ESPTOOL=(uvx --from "esptool>=5,<6" esptool --chip esp32s3)
fi

[[ -f "$BIN" ]] || unzip -o -q "$DIR/02-xiaozhi-v2.5.0/v2.5.0_sensecap-watcher.zip" -d "$DIR/02-xiaozhi-v2.5.0"

porta="${1:-}"
if [[ -z "$porta" ]]; then
  for p in /dev/cu.usbmodem* /dev/cu.usbserial* /dev/cu.wchusbserial* /dev/ttyACM* /dev/ttyUSB*(N); do
    [[ -e "$p" ]] || continue
    if "${ESPTOOL[@]}" -p "$p" chip-id >/dev/null 2>&1; then porta="$p"; break; fi
  done
fi
[[ -n "$porta" ]] || { echo "Não achei o ESP32-S3. Conecte o USB-C de baixo do Watcher e passe a porta." >&2; exit 1; }
echo "ESP32-S3 em $porta"

mkdir -p "$BACKUPS"
carimbo=$(date +%Y%m%d-%H%M%S)
ARQS_ARGS="$BACKUPS/.gravacao.args"
ARQS_POUPAR="$BACKUPS/.poupar.txt"

# Lê a tabela de partições de dentro do binário: monta os pedaços a gravar (pulando as nvs)
# e a lista das partições que serão poupadas e conferidas depois.
/usr/bin/python3 - "$BIN" "$BACKUPS" "$MODO" "$ARQS_ARGS" "$ARQS_POUPAR" <<'PY'
import pathlib, struct, sys

binario, outdir = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
modo, arqs_args, arqs_poupar = sys.argv[3], pathlib.Path(sys.argv[4]), pathlib.Path(sys.argv[5])
dados = binario.read_bytes()

partes = []
for i in range(95):
    bruto = dados[0x8000 + i * 32: 0x8000 + (i + 1) * 32]
    if len(bruto) < 32 or bruto[:2] != b"\xaa\x50":
        break
    tipo, subtipo = bruto[2], bruto[3]
    inicio, tamanho = struct.unpack("<II", bruto[4:12])
    nome = bruto[12:28].rstrip(b"\x00").decode(errors="replace")
    partes.append((nome, tipo, subtipo, inicio, tamanho))

# nvs = data(1) / nvs(2): nvsfactory e nvs. São as que guardam credenciais e ajustes.
poupar = [] if modo == "limpar" else [(n, o, t) for (n, ty, s, o, t) in partes if ty == 1 and s == 2]

def recortes(total, buracos):
    saida, cursor = [], 0
    for ini, tam in sorted(buracos):
        if ini > cursor:
            saida.append((cursor, ini - cursor))
        cursor = max(cursor, ini + tam)
    if cursor < total:
        saida.append((cursor, total - cursor))
    return saida

linhas_args = []
for i, (ini, tam) in enumerate(recortes(len(dados), [(o, t) for (_, o, t) in poupar])):
    arq = outdir / f".xiaozhi-parte{i}.bin"
    arq.write_bytes(dados[ini:ini + tam])
    linhas_args.append(f"0x{ini:X} {arq}")
arqs_args.write_text("\n".join(linhas_args) + "\n", encoding="utf-8")
arqs_poupar.write_text("".join(f"{n} 0x{o:X} {t}\n" for (n, o, t) in poupar), encoding="utf-8")
PY

while read -r nome ini tam; do
  echo "Poupando $nome: $ini (${tam} bytes)"
done < "$ARQS_POUPAR"

# Backup da flash inteira (uma vez) e das partições que vão ser poupadas
if ! ls "$BACKUPS"/watcher-flash-completa-*.bin >/dev/null 2>&1; then
  echo "Backup da flash inteira (32 MB, alguns minutos)..."
  "${ESPTOOL[@]}" -p "$porta" -b 2000000 read-flash 0x0 0x2000000 "$BACKUPS/watcher-flash-completa-$carimbo.bin"
fi
while read -r nome ini tam; do
  "${ESPTOOL[@]}" -p "$porta" -b 2000000 read-flash "$ini" "$tam" "$BACKUPS/$nome-antes-$carimbo.bin" >/dev/null
done < "$ARQS_POUPAR"

echo "Gravando XiaoZhi..."
ARGS_GRAVAR=()
while read -r ini arq; do
  ARGS_GRAVAR+=("$ini" "$arq")
done < "$ARQS_ARGS"
"${ESPTOOL[@]}" -p "$porta" -b 2000000 write-flash --flash-mode dio --flash-size 32MB "${ARGS_GRAVAR[@]}"

echo "Conferindo as partições poupadas..."
falhou=0
while read -r nome ini tam; do
  depois="$BACKUPS/.$nome-depois.bin"
  "${ESPTOOL[@]}" -p "$porta" -b 2000000 read-flash "$ini" "$tam" "$depois" >/dev/null
  if cmp -s "$BACKUPS/$nome-antes-$carimbo.bin" "$depois"; then
    echo "  OK: $nome intacta"
  else
    echo "  ATENÇÃO: $nome mudou; restaurando." >&2
    "${ESPTOOL[@]}" -p "$porta" -b 2000000 write-flash "$ini" "$BACKUPS/$nome-antes-$carimbo.bin"
    falhou=1
  fi
  rm -f "$depois"
done < "$ARQS_POUPAR"
rm -f "$BACKUPS"/.xiaozhi-parte*.bin "$ARQS_ARGS" "$ARQS_POUPAR"

if [[ $falhou -eq 0 ]]; then
  echo "Pronto. Credenciais e ajustes mantidos: o Watcher volta na mesma rede e no mesmo servidor."
else
  echo "Gravação concluída, mas uma partição poupada mudou e foi restaurada. Confira o boot." >&2
fi
