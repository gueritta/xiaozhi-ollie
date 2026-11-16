# xiaozhi-ollie — macOS → Linux port audit

Read-only audit run on **vortex** (CachyOS, x86_64) with `hermes` agent via `hc`,
2026-11-16. Repo: `~/workspaces/xiaozhi-ollie` (fork: `gueritta/xiaozhi-ollie`,
upstream `gabrielvaz/xiaozhi-ollie`, HEAD `7bc76db` "Ollie 2.7.0").
No files were modified during the audit (`git diff` empty).

Difficulty scale: Trivial / Easy / Moderate / Hard / Impossible.

## Matrix

| Path | Purpose | macOS mechanism | Linux difficulty | Linux equivalent |
|---|---|---|---|---|
| `servidor/instalar-servico.sh:8-45` | Installs/removes/restarts the server as a login service; writes `~/Library/LaunchAgents/<label>.plist`, logs to `~/Library/Logs/watcher-servidor.log` | launchd LaunchAgent (`launchctl bootstrap/bootout/kickstart gui/$UID`, `RunAtLoad`, `KeepAlive`, `ThrottleInterval`) | Easy | systemd user unit in `~/.config/systemd/user/`, `Restart=always`, `RestartSec=15`, `WantedBy=default.target`, `loginctl enable-linger`, `journalctl --user` |
| `servidor/iniciar.py:1-6,29-31` | Python launcher instead of `iniciar.sh` because launchd-started zsh cannot read the external disk; also sets `/opt/homebrew/bin` PATH | launchd + macOS volume-permission quirk | Trivial | Keep launcher or drop it; `ExecStart=<venv>/bin/python iniciar.py`; quirk does not exist |
| `servidor/extras/core/utils/reuniao.py:201-204`; `servidor/ponte-mcp/ponte.py:1063-1066` | Desktop notifications ("summary ready", "notification shown on the Mac") | `osascript -e 'display notification …'` | Easy | `notify-send` / `gdbus call` on session bus (set `DBUS_SESSION_BUS_ADDRESS` for the user service) |
| `servidor/extras/core/utils/reuniao.py:177-199` | Creates a rich note in Notes.app folder "Reuniões Watcher" from meeting summary + transcript (`tell application "Notes"`, HTML body) | osascript + Apple Notes AppleScript, needs Automation (TCC) approval and a GUI session | Moderate | No Notes equivalent; write the produced `resumo.md` to a notes dir, or push to Joplin/Obsidian/Nextcloud Notes. Rich-note parity is lost |
| `servidor/ponte-mcp/ponte.py:1039-1046` | Voice tool `mac_atalhos_listar`: lists runnable Shortcuts | `shortcuts list` (Shortcuts.app CLI) | Hard | None. Replace with explicit allowlisted MCP tools/scripts per action |
| `servidor/ponte-mcp/ponte.py:1049-1060` | Voice tool `mac_atalho_executar`: runs a named Shortcut, optional text input | `shortcuts run <name> [--input-path file]` | Hard | None. The "no free-form shell" product decision must be re-expressed as a command allowlist or per-action tools |
| `servidor/ponte-mcp/ponte.py:1070` | Startup warning when `shortcuts` is missing from PATH | `shutil.which("shortcuts")` | Trivial | Drop the check or warn about the replacement tools |
| `servidor/extras/core/utils/reuniao.py:29-32`; `diario.py:29-32`; `fotos.py:14-15`; `ponte.py:929-930` | Meeting/chat/photo mirrors and `Lembretes.md` reminders in iCloud Drive | `~/Library/Mobile Documents/com~apple~CloudDocs/Watcher/...` (iCloud File Provider) | Easy | Already overridable: `WATCHER_PASTA_REUNIOES`, `WATCHER_PASTA_CONVERSAS`, `WATCHER_PASTA_FOTOS`, `WATCHER_LEMBRETES` → point at any Syncthing/Nextcloud/rclone dir |
| `servidor/extras/core/utils/memoria_usuario.py:10`; `memoria.py:21,77`; `extras/core/api/cartao_handler.py:22`; `avisos_handler.py:119` | Agent memory (`*.md` read every conversation), reminders, SD-card backup destination, `QR.md` list | Same iCloud Drive path, **hardcoded with no env override** (cartao creates dirs under it) | Moderate | Same synced-dir approach, but requires adding config/env for these 4 sites first |
| `servidor/extras/core/utils/icloud.py:1-26` | Retries reads while iCloud sync returns `EDEADLK` ("Resource deadlock avoided", errno 11) | iCloud File Provider lock behaviour | Trivial | Loop becomes harmless dead code; keep or delete. Ordinary local reads do not raise it |
| `sincronizar-repos.sh:62` | Copies iCloud memory notes into the private repo before push | iCloud path + zsh | Easy | Point `MEMORIA` at a Syncthing/Nextcloud dir (or env var); zsh required |
| `firmwares/gravar-xiaozhi.sh:9,30`; `firmwares/atualizar-firmware.sh:16`; `docs/INSTALL.md:193` | Auto-detect the ESP32-S3 and flash firmware over USB-C | `/dev/cu.usbmodem*`, `/dev/cu.usbserial*`, `/dev/cu.wchusbserial*` (macOS callout devices) | Easy | `/dev/ttyACM*` and `/dev/ttyUSB*`; add user to `dialout`/`uucp`; `esptool` invocation unchanged |
| `docs/INSTALL.md:20-28` | Declares prerequisites: "On the Mac (Apple Silicon, macOS 14 or later)", Homebrew, `brew install uv`, `brew install ffmpeg`, Tailscale | Homebrew + macOS-only install contract | Easy | Distro packages (`ffmpeg`), standalone `uv` installer or pipx, Tailscale packages; rewrite docs |
| `servidor/extras/core/utils/reuniao.py:35`; `memoria.py:23`; `extras/core/api/reunioes_handler.py:22` | Audio conversion, segmentation and duration probing for meetings/offline memory | Hardcoded Apple-Silicon Homebrew paths `/opt/homebrew/bin/ffmpeg` and `ffprobe` | Easy | `/usr/bin/ffmpeg`/`ffprobe` or `shutil.which` + env override. **These paths already break on Intel Macs** (`/usr/local`) |
| `servidor/ponte-mcp/ponte.py:29-31`; `servidor/iniciar.py:31`; `servidor/iniciar.sh:16`; `extras/core/utils/codex_remoto.py:87,97`; `multica_remoto.py:59,69` | Locates `claude`, `codex`, `multica`, `herdr` | PATH/fallbacks biased to `/opt/homebrew/bin` and `/usr/local/bin` | Easy | `~/.local/bin`, `/usr/bin`; prefer `shutil.which` |
| `servidor/ponte-mcp/ponte.py:1033-1036` | `mac_status` reports uptime, disk and free memory | `memory_pressure -Q` (`uptime`/`df` are portable) | Easy | `free -m` or parse `/proc/meminfo`; format differs, so parse rather than pass through |
| `servidor/ponte-mcp/ponte.py:858-870` | Reads Claude Code's OAuth token to query 5-hour/weekly plan limits | `security find-generic-password -s "Claude Code-credentials" -w` (login Keychain) | Moderate | Read Claude Code's own credential file (`~/.claude/.credentials.json`) or libsecret `secret-tool`; same `claudeAiOauth.accessToken` shape |
| `servidor/testar-api.sh:36-38` | Checks generated TTS file size and plays it aloud | BSD `stat -f%z`, `afplay` | Easy | `stat -c%s`; `paplay` / `ffplay -nodisp -autoexit` / `aplay` |
| `firmwares/xiaozhi-ollie/simulador/rodar.sh:17` | Opens generated simulator PNGs | `open` (LaunchServices) | Trivial | `xdg-open` |
| `apple-watch/app/project.yml:1-110`; `app/Config/*.xcconfig`; `app/ExportOptions.plist:1-10`; `apple-watch/README.md:52-54` | Generates and builds the watchOS app: `xcodegen generate`, Xcode project, watchOS 26 + iOS 18 targets, Team ID, automatic codesigning, archive for `generic/platform=iOS`, App Store export | Xcode toolchain, Apple SDKs, `codesign`, Apple Developer signing | **Impossible** | None on Linux. Build on a Mac or a macOS CI runner (e.g. GitHub Actions `macos-*`). The relay and plugin are portable Node/Cloudflare Workers |
| `apple-watch/app/Ollie/Model/Keychain.swift:1-40`; `Ollie/Ollie.entitlements`; `OllieWidgets/OllieWidgets.entitlements` | Stores the pairing token in the watch Keychain, shared with complications | Security.framework generic-password items + `keychain-access-groups` entitlement | **Impossible** | Only if the client were ported off watchOS; then use the platform keystore |
| `apple-watch/app/Ollie/Model/Speaker.swift:1-10` | Speaks answers aloud in the watch's voice | AVFoundation (`AVSpeechSynthesizer`, `AVAudioSession`) + NaturalLanguage | **Impossible** | Only if ported; otherwise server-side TTS, or `piper`/`espeak-ng` for a desktop client |
| `servidor/arquivos/requirements-mac.txt:1-36` (used at `instalar.sh:20,25`) | Python dependency set tuned for macOS; `vosk` dropped because it had no macOS build | macOS wheel availability, not a runtime API | Trivial | Pip requirements are cross-platform; rename and optionally restore `vosk` |

## Main blockers

1. **Apple Shortcuts is the only architectural break.** The security model deliberately
   routes all Mac actions through `shortcuts run`. There is no drop-in on Linux — you must
   replace it with explicit per-action tools or a strict command allowlist. The free-form-shell
   ban and its confirmation flow must be preserved.
2. **The watchOS app cannot be built on Linux at all.** The relay (`apple-watch/relay`,
   Cloudflare Worker) and plugin (`apple-watch/plugin`, Node 22, no npm deps) are portable;
   only `apple-watch/app` is not.
3. **iCloud is mostly a config problem, but the override is incomplete.** 4 hardcoded sites
   (`memoria_usuario.py:10`, `memoria.py:21,77`, `cartao_handler.py:22`,
   `avisos_handler.py:119`) need a config knob before a Linux/Syncthing setup works end to end.
4. **Two osascript paths need macOS TCC grants** (Automation for Notes, Notifications).
   Under launchd they run outside your terminal's existing grants, so they can fail even when
   they work interactively — worth knowing before debugging "Notas falhou".

## Firmware / ESP-IDF path

**Portable.** `firmwares/xiaozhi-ollie/compilar.sh` builds fine on Linux with ESP-IDF v6.1 +
`esptool`. The only Mac-specific parts are the `/dev/cu.*` device names and the docs.
Silicon-side toolchain is Linux-native.

## Also worth noting

- The hardcoded `/opt/homebrew/bin/ffmpeg|ffprobe` is a latent bug on Intel Macs too,
  independent of Linux.
- All shell scripts are `#!/bin/zsh` and use zsh-only syntax (`${0:A:h}`, `print -P`, `(N)`
  null glob). zsh exists on Linux, so this is not a blocker — only a dependency to
  install/document.
- Docs/marketing reference the macOS mechanisms (`README.md:62,79`, `servidor/README.md:34`,
  `site/index.html:408,464,597,599`, `docs/INSTALL.md:176-181`) but are not runtime dependencies.
- `servidor/extras/core/utils/avisos.py:25` and firmware UI strings ("Notas", "iCloud") are
  labels only.
- Unrelated stray: an untracked, empty file named `-l` exists at the repo root — looks like a
  botched `ls -l` redirect, not part of the project.

## Verdict

The **server** is ~80% portable. The real work is concentrated in three places:
`instalar-servico.sh` (launchd → systemd), `ponte-mcp/ponte.py` (Shortcuts → shell/allowlist),
and the 4 hardcoded iCloud paths. The **firmware** side is clean.

Only genuinely impossible pieces are the watchOS app and its Keychain/Speech APIs.
