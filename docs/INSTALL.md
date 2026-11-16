# Installing Xiaozhi Ollie

This guide takes you from a SenseCAP Watcher fresh out of the box to saying “Hey Ollie” and hearing about your agent sessions. Plan for about an hour, most of it waiting for builds and downloads.

There are two parts:

1. **The server**, which runs on your Mac. It hears you, thinks with a language model, speaks back and runs the agent tools.
2. **The firmware**, which you build on the Mac and flash to the Watcher over USB-C.

You can follow the steps by hand, or hand them to a coding agent with the prompt in [step 1](#step-1-optional-let-a-coding-agent-do-it).

## What you need

**Hardware**

- A SenseCAP Watcher. The “for XiaoZhi” edition is the closest match, but any Watcher works.
- A USB-C data cable (some cables only charge).
- Optional: a microSD card, **FAT32 with MBR**. ESP-IDF cannot read exFAT or GPT.

**On the Mac** (Apple Silicon, macOS 14 or later)

| Tool | Why | Install |
|---|---|---|
| Homebrew | Installs the rest | [brew.sh](https://brew.sh) |
| uv | Python environments | `brew install uv` |
| ffmpeg | Audio for meetings and offline memory | `brew install ffmpeg` |
| Tailscale, with Funnel allowed | Lets the Watcher reach your Mac from anywhere | [tailscale.com/download](https://tailscale.com/download) |
| ESP-IDF v6.1 | Builds the firmware | see [step 5](#step-5-install-esp-idf) |
| An OpenAI-compatible API key | Speech-to-text, the model and text-to-speech | [OpenRouter](https://openrouter.ai/keys) works out of the box |
| Claude Code, Codex, herdr | The agents Ollie keeps an eye on | each one's own installer; all optional |
| Multica CLI | Issues and autopilots | optional |

## Step 1 (optional): let a coding agent do it

Open a terminal in the repository folder, start **Claude Code**, **Codex** or another coding agent, and paste this prompt. The agent does the setup with you and stops before anything that needs your hands or your approval.

```text
You are helping me install Xiaozhi Ollie from this repository on my Mac.
Read README.md and docs/INSTALL.md first, then follow docs/INSTALL.md step by step.

Rules:
- Before each step, tell me in one sentence what you are about to do. Run the commands yourself
  when you can, and check the result before moving on.
- Check prerequisites first (uv, ffmpeg, tailscale, ESP-IDF v6.1, and optionally claude, codex,
  herdr, multica). If something is missing, show me the install command and ask before running it.
- Secrets: never print, log or commit the contents of servidor/.env. When the API key is needed,
  ask me to paste it into servidor/.env myself, then just confirm the line is filled.
- Ask me which language I want (IDIOMA: en-US, pt-BR, zh-CN or es-ES), my HOST_PUBLICO (Tailscale
  machine name, ending in .ts.net), NOME_USUARIO and PONTE_RAIZES (the folders where my code
  projects live) and write them to servidor/.env.
- Stop and wait for me before: turning on Tailscale Funnel, installing the background service,
  flashing the Watcher, and any command that erases data.
- To flash, ask me to connect the Watcher with the USB-C port on the bottom. Use
  firmwares/gravar-xiaozhi.sh for the first flash and firmwares/atualizar-firmware.sh for updates.
- Never commit anything in firmwares/backups/: those files contain my Wi-Fi passwords.
- If a command fails, read the error, explain it in plain words and propose a fix before retrying.
- At the end, run servidor/verificar.sh, show me the summary, and tell me what to say to the
  Watcher to test it.
```

## Step 2: get the code

```sh
git clone https://github.com/gabrielvaz/xiaozhi-ollie.git
cd xiaozhi-ollie
```

Everything below runs from this folder.

## Step 3: set up the server

```sh
servidor/instalar.sh
```

This downloads the xiaozhi-esp32-server at a tested commit, applies the translations and fixes, creates the Python environment and writes `servidor/.env` with a fresh secret path and signing key.

Now open `servidor/.env` and fill in:

| Variable | What to put |
|---|---|
| `IDIOMA` | `en-US`, `pt-BR`, `zh-CN` or `es-ES`. Screen, voice and answers follow it, and the firmware build reads it too |
| `API_KEY` | Your OpenRouter (or other OpenAI-compatible) key |
| `HOST_PUBLICO` | Your Mac's Tailscale name, such as `my-mac.tail1234.ts.net` (no `https://`). Find it with `tailscale status` or in the Tailscale admin console |
| `NOME_USUARIO` | Your first name, used in greetings |
| `PONTE_RAIZES` | Folders where your projects live, separated by `:` |
| `WATCHER_VOCABULARIO` | Optional: names the meeting transcription should spell right |

Leave `SEGREDO` and `AUTH_KEY` as generated. The models have working defaults, and when `VOZ` is empty the server picks a voice for your language.

Test the provider with real calls:

```sh
servidor/testar-api.sh
```

It runs a chat with a tool call, a voice sample and a transcription. Fix any error here before going on.

## Step 4: open the door with Tailscale Funnel

The Watcher needs to reach your Mac from home Wi-Fi or from your phone's hotspot. Funnel publishes **only** the secret path from `.env`; everything else stays closed.

```sh
servidor/configurar-funnel.sh ligar
```

The first time, Tailscale may ask you to allow Funnel for your tailnet in the browser. The script prints the OTA URL; the firmware build in step 6 embeds it for you.

Then install the server as a background service that starts at login:

```sh
servidor/instalar-servico.sh instalar
servidor/instalar-servico.sh log      # follow the log; Ctrl+C to stop following
```

## Step 5: install ESP-IDF

Skip this if you already have ESP-IDF v6.1.

```sh
mkdir -p ~/esp
git clone -b v6.1 --recursive https://github.com/espressif/esp-idf.git ~/esp/esp-idf-v6.1
~/esp/esp-idf-v6.1/install.sh esp32s3
```

The build looks in `~/esp/esp-idf-v6.1` by default. If yours lives elsewhere, create `firmwares/xiaozhi-ollie/local.env` (it is ignored by git):

```sh
export IDF_PATH=/path/to/esp-idf-v6.1
export IDF_TOOLS_PATH=/path/to/.espressif
```

## Step 6: build the firmware

```sh
firmwares/xiaozhi-ollie/compilar.sh
```

It downloads XiaoZhi v2.5.0, applies the Ollie patches, embeds your server address from `servidor/.env` and builds in the language set by `IDIOMA`. To build another language without changing `.env`, pass it: `firmwares/xiaozhi-ollie/compilar.sh --idioma en-US`. The first build takes several minutes. The result is `firmwares/xiaozhi-ollie/saida/merged-binary.bin`.

## Step 7: flash the Watcher

Connect the Watcher with the **USB-C port on the bottom** (the one on the back only charges). The Mac sees two serial ports; the script finds the ESP32-S3 on its own.

```sh
firmwares/gravar-xiaozhi.sh
```

On the first run it saves a **full 32 MB backup** of the flash to `firmwares/backups/`, then writes the firmware without touching the factory partition and checks it afterwards. Keep that backup somewhere safe and private: it contains your Wi-Fi passwords. It is all you need to go back to the original firmware.

For later updates, use the script that keeps Wi-Fi and settings:

```sh
firmwares/atualizar-firmware.sh
```

## Step 8: connect the Watcher to Wi-Fi

1. After flashing, the Watcher creates a network called `Xiaozhi-XXXX`.
2. Join it from your phone; the setup page opens (or browse to the address shown on the screen).
3. Add your home Wi-Fi. Add your phone's hotspot too, so Ollie works away from home. On an iPhone, turn on **Maximize Compatibility**: the Watcher only uses 2.4 GHz.
4. The server address is already built in. The **Advanced** tab is only for pointing to a different server.

## Step 9: check everything

```sh
servidor/verificar.sh
```

It checks the API key and models, herdr, Claude Code, Codex, Multica, the local server and the public Funnel path, and that the public root stays closed.

Then say **“Hey Ollie”**, or click the wheel, and ask how your sessions are doing.

## Optional extras

**Personal files in iCloud Drive.** Ollie reads and writes a `Watcher` folder in iCloud Drive:

| Path | Use |
|---|---|
| `Watcher/Memória/*.md` | Notes Ollie always knows. Start with `sobre-mim.md`: who you are, your projects, how you like answers |
| `Watcher/Lembretes.md` | Reminders saved by voice |
| `Watcher/QR.md` | Extra entries for the QR code app |
| `Watcher/Reuniões/` | Meeting transcripts and summaries |

**Multica.** Run `multica login` and `multica daemon start` to use the issue and autopilot tools.

**Other XiaoZhi devices.** Any device running XiaoZhi v2 can use your Ollie server for voice and the agent tools. Paste the OTA URL printed by `configurar-funnel.sh` into the **Custom OTA URL** field of its Wi-Fi setup page.

## Troubleshooting

| Problem | What to try |
|---|---|
| The flash script does not find the Watcher | Use the bottom USB-C port and a data cable. Pass the port by hand: `firmwares/gravar-xiaozhi.sh /dev/cu.usbmodemXXXX` |
| The Watcher says it cannot reach the server | Run `servidor/verificar.sh`. Check that the service is running (`servidor/instalar-servico.sh log`) and that Funnel is on |
| Voice works but the agent tools fail | Check `PONTE_RAIZES` in `servidor/.env` and that `claude`, `codex` and `herdr` run in a normal terminal |
| The build stops with “region overflowed” | The app partition is full. Remove an app from `firmwares/xiaozhi-ollie/placa/registro_apps.h` or move fonts to the assets partition |
| You want the original firmware back | Flash the full backup from `firmwares/backups/` with esptool |

## Linux, local network (no Tailscale)

Verified on CachyOS (Arch), x86_64, with ESP-IDF v6.0.1. Same code, two substitutions:
systemd instead of launchd, and plain HTTP/WebSocket on your LAN instead of Tailscale Funnel.
Everything works except Apple Notes, Apple Shortcuts and the watchOS app (see
`docs/PORT-AUDIT.md`).

**What you need**

| Tool | Install |
|---|---|
| git, zsh, ffmpeg | `sudo pacman -S git zsh ffmpeg` (or your distro's equivalents) |
| uv | `curl -LsSf https://astral.sh/uv/install.sh \| sh` |
| ESP-IDF v6.0.1 or newer | `git clone -b v6.1 --depth 1 https://github.com/espressif/esp-idf ~/esp/esp-idf && ~/esp/esp-idf/install.sh` |
| esptool 5.x | `sudo pacman -S esptool`, or let `gravar-xiaozhi.sh` fall back to `uvx` |
| Your user in `uucp`/`dialout` | `sudo usermod -aG uucp $USER` (log out and back in) |
| An OpenAI-compatible API key | same as macOS |

**1. Point the server and the firmware at this machine**

In `servidor/.env` use the LAN addresses instead of the Funnel hostname (leave
`HOST_PUBLICO` empty):

```sh
HOST_PUBLICO=
URL_BASE_OTA=http://192.168.1.50:8003
URL_BASE_WS=ws://192.168.1.50:8000
```

`URL_BASE_*` makes the server listen on `0.0.0.0` and announce those addresses to
the Watcher; the firmware embeds the OTA URL at build time. Use the address from
`ip -4 addr show scope global`, and give the machine a static lease in your router.

**2. Build and flash the firmware**

```sh
firmwares/xiaozhi-ollie/compilar.sh                  # ~15 min the first time, ~800 MB of downloads
firmwares/gravar-xiaozhi.sh /dev/ttyACM1             # the Watcher shows two ports; the ESP32-S3 is the second
```

ESP-IDF is found through `IDF_PATH` (environment or `firmwares/xiaozhi-ollie/local.env`),
falling back to `~/esp/esp-idf-v6.1` and `~/esp/esp-idf`. The first flash backs up the
whole 32 MB flash and verifies the factory `nvsfactory` partition is untouched.

**3. Run the server as a user service**

```sh
servidor/instalar.sh                                 # fill API_KEY in servidor/.env afterwards
servidor/instalar-servico.sh instalar                # systemd user unit, restarts on crash
servidor/verificar.sh                                # checks the API, the agents and the LAN OTA path
servidor/instalar-servico.sh log                     # journalctl --user -u com.xiaozhi-ollie.servidor
```

`instalar-servico.sh` writes `~/.config/systemd/user/com.xiaozhi-ollie.servidor.service`
with `Restart=always`, `RestartSec=15` and `loginctl enable-linger` so it survives logout.
The same script still writes a LaunchAgent when run on macOS.

**4. What changes on Linux**

| On macOS | On Linux |
|---|---|
| iCloud Drive/Watcher | `WATCHER_PASTA` (default `~/Watcher`); point it at a Syncthing/Nextcloud folder if you want the same sync |
| Apple Notes note per meeting | `resumo.md` and `transcricao.md` in the meeting folder (opens fine in Obsidian/Joplin) |
| Shortcuts (`shortcuts run`) | `servidor/atalhos.json`: a named action → argv list, no shell, still requires `confirmado=true` |
| `osascript` notifications | `notify-send` (the systemd unit exports `DBUS_SESSION_BUS_ADDRESS`) |
| Keychain (Claude Code token) | `~/.claude/.credentials.json` |
| `memory_pressure -Q` | `free -m` |
| `/dev/cu.usbmodem*` | `/dev/ttyACM*` / `/dev/ttyUSB*` |

**Troubleshooting on Linux**

| Problem | What to try |
|---|---|
| `Failed to connect to Espressif device` | Try the other `/dev/ttyACM*`; the Watcher exposes the ESP32-S3 and the Himax on two ports. Check you are in `uucp` |
| The Watcher cannot reach the server | `ip -4 addr show` — is the IP in `URL_BASE_OTA`/`URL_BASE_WS` still this machine's? `curl http://IP:8003/xiaozhi/ota/` should answer |
| The server does not start | `servidor/instalar-servico.sh log`; a missing variable in `servidor/.env` stops it with a list of names |
| No desktop notifications | `notify-send oi` in your session; inside the unit the D-Bus address comes from `/run/user/<uid>/bus` |

## Expansion port (Grove + 2×4 header)

The firmware exposes the device's own I/O to the assistant as MCP tools, so you drive it by
voice — nothing to change on the server:

| Tool | What it does |
|---|---|
| `self.led.set_color` / `self.led.effect` / `self.led.release` | RGB LED (ws2813, GPIO40): any colour, solid/breath/blink/off, or hand it back to the device states |
| `self.i2c.scan` / `self.i2c.read` / `self.i2c.write` | The Grove port and the header's I²C (GPIO47/48) — any I²C sensor, no per-sensor firmware. Writes to the device's own chips (audio codec, IO expander, touch) are refused |
| `self.gpio.list` / `self.gpio.set` / `self.gpio.get` / `self.gpio.pwm` / `self.adc.read` | The two GPIO pins of the 2×4 header: digital out/in, PWM (servo, fan, dimmer) and ADC on GPIO1–10 |
| `self.uart.config` / `self.uart.write` / `self.uart.read` | A serial bridge to another board (ESP32, Flipper Zero…): baud from 300 to 921600, text or hex |

Pins and UART are a build-time decision (`firmwares/xiaozhi-ollie/local.env`, outside git).
On the SenseCAP Watcher the only pins free at runtime are GPIO17/18 — the schematic names
them `ESP_GPIO17/UART1_TX` and `ESP_GPIO18/UART1_RX`, and every other GPIO (and all 16
I/O-expander pins) belongs to the display, camera, SD, audio, knob or power rails:

```sh
export OLLIE_GPIO_LIVRES="17,18"        # the two GPIO pins of the 2x4 header
export OLLIE_UART_TX="17"               # UART bridge on the same pair
export OLLIE_UART_RX="18"
export OLLIE_UART_BAUD="921600"
```

Empty = those tools refuse with a message saying so (LED and I²C stay available). Then
`compilar.sh` and `gravar-xiaozhi.sh` — the flash keeps Wi-Fi, server address and settings.
`compilar.sh` is the right entry point: it sources `local.env`, so the pin config reaches the
build. (Running `aplicar_patches.py` by hand also works — it falls back to reading
`local.env`.)

Two caveats with 17/18: they are **not ADC-capable** on the ESP32-S3 (ADC1 is GPIO1–10), so
`self.adc.read` applies only if you give up another pin that can do it; and they double as the
**Himax flasher UART** (921600 8N1), used when updating the Watcher's AI coprocessor — don't
run a Himax update while the assistant holds the serial bridge. `self.gpio.set`/`pwm` refuse
a pin that the UART currently owns.

Electrical limits: 3.3 V logic, ~20 mA per pin. LEDs, sensors and opto-isolated inputs are
fine; relays, solenoids and motors need a transistor/MOSFET driver and their own supply.
The Grove port is I²C only (SDA GPIO47 / SCL GPIO48) — it is not a general-purpose GPIO.
