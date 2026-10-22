# PortalVNC

Защищённый интерактивный удалённый доступ к Windows-компьютерам — аналог VNC с
улучшенными возможностями под администрирование парка машин в доменной и
бездоменной среде.

- Доступ членам локальной группы «Администраторы» по SID (`S-1-5-32-544`) —
  работает на любой локали Windows (анг./рус./тур./…).
- Вход учётной записью Windows (пароль проверяет сама ОС через `LogonUser`),
  без собственного хранилища паролей.
- Канал — TLS со взаимной проверкой сертификатов: посторонний VNC-клиент
  подключиться не может.
- Нативные exe, статическая линковка, минимум файлов рядом. Без .NET и прочих
  рантаймов.

## Компоненты

| Папка | Что это | Статус |
|---|---|---|
| `protocol/` | Общий модуль: mTLS (mbedTLS), рукопожатие + версии, вход+SID | ✅ |
| `agent/` | Агент: служба Windows, захват экрана, ввод, RFB поверх mTLS, значок в трее, экран блокировки/UAC, мультисессии | ✅ |
| `viewer/` | Клиент-просмотрщик (окно Win32), говорит только на нашем протоколе | ✅ |
| `gateway/` | Мост браузер↔агент (Node): WebSocket + mTLS + RFB, для портала и noVNC | ✅ (проверено в браузере) |
| `installer/` | MSI-установщик (WiX): служба + брандмауэр, под GPO | ✅ |
| `tools/` | Скрипты: dev-сертификаты, локальный запуск, установка службы | ✅ |
| `docs/` | Дизайн и планы реализации | — |

## Сборка

Требуется: Visual Studio Build Tools 2022 (C++), Windows SDK 10, CMake ≥3.25,
vcpkg (в `./vcpkg`). Node 22 для шлюза. WiX 5 для MSI. Подробности —
[cmake/toolchain-notes.md](cmake/toolchain-notes.md).

```powershell
cmake --preset default
cmake --build --preset default
```

Результаты: `build\agent\Release\pvnc-agent.exe`,
`build\viewer\Release\pvnc-viewer.exe` (по ~1 МБ, без рантаймов).

## Быстрый старт (локально)

```powershell
# dev-сертификаты (CA + агент + клиент)
powershell -ExecutionPolicy Bypass -File tools\make-dev-certs.ps1

# агент на 127.0.0.1:7654 + команда запуска просмотрщика
powershell -ExecutionPolicy Bypass -File tools\run-local-slice.ps1
```

Режимы агента: `--config PATH` (консоль, dev), `--install`/`--uninstall`
(служба), `--service` (под SCM), `--worker [--port N]` (запускается службой в
сессии). Конфиг — `config.json` рядом с exe (см. `agent/config.example.json`).

### Служба (dev, с правами администратора)

```powershell
powershell -ExecutionPolicy Bypass -File tools\install-service.ps1
# ... затем tools\uninstall-service.ps1
```

### Браузерный доступ (шлюз + noVNC)

```powershell
# агент запущен; затем шлюз с конфигом «токен -> цель + учётка»
$env:PVNC_GW_CONFIG="path\to\gw-config.json"; node gateway\server.js
# открыть http://localhost:8787/
```
Подробности и интеграция в веб-портал — [gateway/README.md](gateway/README.md).

### MSI

```powershell
powershell -ExecutionPolicy Bypass -File installer\build-msi.ps1
# -> installer\PortalVNC.msi (ставит службу, открывает порт; раздаётся по GPO)
```

## Порты и мультисессии

Базовый порт — **7654**. На терминальном сервере служба запускает рабочего в
каждой активной сессии: активная консольная — на базовом порту, остальные (RDP)
— на `базовый + sessionId`. Так одиночная рабочая станция всегда на 7654.

## Безопасность

- mTLS: агент принимает только клиента с сертификатом от нашего CA.
- Авторизация: членство в группе Администраторы по SID (любая локаль) + доп. SID
  из конфига.
- `allowedClients` (IP/CIDR), пустой список = не пускать никого.
- Значок в трее управляется конфигом (по умолчанию выключен — фоновый режим).

## Лицензия

GPLv3 (см. `LICENSE`).
