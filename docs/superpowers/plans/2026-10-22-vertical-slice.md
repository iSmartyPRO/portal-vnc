# PortalVNC — План №1: вертикальный срез (протокол + агент + просмотрщик)

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Довести до работающего состояния минимальный сквозной сценарий: агент на Windows захватывает рабочий стол и отдаёт его по зашифрованному каналу (mTLS) только клиенту с нашим сертификатом, после входа учётной записью Windows — члену группы «Администраторы» (по SID); просмотрщик подключается, видит экран и управляет мышью/клавиатурой.

**Architecture:** Один Git-репозиторий, CMake + vcpkg (триплет `x64-windows-static`). Три CMake-таргета: `pvnc_protocol` (статическая библиотека: транспорт mTLS + рукопожатие/версии + SID-авторизация), `pvnc_agent` (консольный exe — сервер на базе libvncserver, захват через GDI), `pvnc_viewer` (exe с окном Win32 — на базе libvncclient). RFB-поток идёт внутри mTLS-туннеля. На этом этапе агент — обычный консольный процесс (не служба), захват только обычного рабочего стола (без Secure Desktop) — это следующие планы.

**Tech Stack:** C++17, MSVC 14.44 (VS Build Tools 2022), Windows SDK 10.0.26100, CMake ≥3.25, vcpkg (manifest mode). Зависимости: `libvncserver` (тянет zlib, libjpeg-turbo), `mbedtls` (mTLS), `catch2` (тесты). Генерация тестовых сертификатов — системным `openssl`.

**Spec:** `docs/superpowers/specs/2026-10-22-portalvnc-design.md`

## Global Constraints

- Целевая платформа: Windows x64. Архитектура сборки: `x64-windows-static` (всё статически, минимум файлов рядом с exe).
- Язык: C++17. Компилятор MSVC 14.44; SDK 10.0.26100.
- Порт агента по умолчанию: **7654**.
- SID группы администраторов: `S-1-5-32-544`. Авторизация — по SID, не по имени группы (работает на любой локали Windows).
- Агент не хранит паролей: проверка через `LogonUserW` средствами ОС.
- Транспорт — TLS со взаимной проверкой сертификатов: агент принимает только клиента с сертификатом, подписанным нашим CA. Чужой/публичный VNC-клиент подключиться не может.
- Лицензия репозитория — GPL (из-за LibVNC). Файл `LICENSE` в корне.
- Рядом с каждым exe — минимум файлов; зависимости слинкованы статически.

## Review Focus

- **Клиент без нашего сертификата / с чужим сертификатом** — агент должен разорвать соединение на этапе TLS-рукопожатия, до какого-либо обмена данными (Task 2).
- **Верный пароль, но пользователь НЕ в группе администраторов** — доступ запрещён с кодом отказа `AUTH_NOT_ADMIN`, даже если пароль правильный (Task 4).
- **Несовпадение версии протокола** — структурированный отказ `VERSION_MISMATCH` с указанием версий сторон, а не зависание или разрыв без объяснения (Task 3).
- **Подключение с адреса не из `allowedClients`** — отказ до TLS (быстрый отсев по IP), пустой список = не пускать никого (Task 5).
- **Срыв TLS-туннеля посреди RFB-сессии** — агент освобождает сессию и ресурсы захвата, продолжает принимать новые подключения, не падает (Task 6).

---

### Task 0: Репозиторий, toolchain и сборочный скелет

**Files:**
- Create: `C:\apps\PortalVNC\.gitignore`, `LICENSE` (GPLv3), `README.md`
- Create: `CMakeLists.txt` (корневой), `vcpkg.json`, `CMakePresets.json`
- Create: `cmake/toolchain-notes.md` (как запускать сборку)
- Create: `tools/make-dev-certs.ps1` (генерация dev CA + сертификат агента + сертификат клиента через openssl)
- Create заглушки таргетов: `protocol/CMakeLists.txt`, `agent/CMakeLists.txt`, `viewer/CMakeLists.txt`, и по одному `.cpp` с `main`/пустой функцией на таргет, чтобы собралось.

**Interfaces:**
- Produces: рабочая команда сборки (`cmake --preset default && cmake --build --preset default`), которая компилирует три пустых таргета; vcpkg подтягивает `libvncserver`, `mbedtls`, `catch2`.

- [ ] **Step 1:** `git init` в `C:\apps\PortalVNC`; создать `.gitignore` (игнор `build/`, `vcpkg_installed/`, `out/`, `*.user`, dev-сертификаты `*.key`, `*.crt`, `*.pem`), `LICENSE` (полный текст GPLv3), `README.md` (карта репозитория: `protocol/`, `agent/`, `viewer/`, `docs/`, `tools/`; порядок сборки).
- [ ] **Step 2:** Установить vcpkg: клонировать в `C:\apps\PortalVNC\vcpkg` (или переиспользовать общий, если найдётся), запустить `bootstrap-vcpkg.bat`. Записать путь в `cmake/toolchain-notes.md`.
- [ ] **Step 3:** `vcpkg.json` (manifest) с зависимостями `libvncserver`, `mbedtls`, `catch2`; `CMakePresets.json` с пресетом `default`: генератор Ninja или VS, `CMAKE_TOOLCHAIN_FILE` = vcpkg, `VCPKG_TARGET_TRIPLET=x64-windows-static`, `CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded`.
- [ ] **Step 4:** Корневой `CMakeLists.txt` (`cmake_minimum_required(VERSION 3.25)`, `project(PortalVNC CXX)`, `set(CMAKE_CXX_STANDARD 17)`, `enable_testing()`, `add_subdirectory` на три таргета); пустые таргеты с `main(){return 0;}` (agent, viewer) и пустой `.cpp` (protocol как `add_library`).
- [ ] **Step 5:** Запустить конфигурацию и сборку. Expected: три таргета собрались, vcpkg поставил зависимости. При провале сборки libvncserver под `x64-windows-static` — зафиксировать в `toolchain-notes.md` рабочие опции (это и есть снятие главного риска сборки).
- [ ] **Step 6:** `tools/make-dev-certs.ps1`: генерит `devca.key/devca.crt`, `agent.key/agent.crt` (подписан CA, CN=agent), `client.key/client.crt` (подписан CA, CN=portalvnc-client) в `./certs/` (в gitignore). Запустить, убедиться что файлы созданы.
- [ ] **Step 7: Commit** — `chore: repo skeleton, vcpkg manifest, cmake build, dev cert tool`

---

### Task 1: Каркас протокола — типы сообщений и версии

**Files:**
- Create: `protocol/include/pvnc/protocol.h`, `protocol/src/protocol.cpp`
- Test: `protocol/test/test_protocol.cpp`

**Interfaces:**
- Produces:
  - `namespace pvnc` с константами: `constexpr uint32_t kMagic = 0x50564E31; // "PVN1"`, `constexpr uint16_t kProtocolVersion = 1;`.
  - `enum class RefuseCode : uint16_t { None=0, VersionMismatch=1, AuthFailed=2, AuthNotAdmin=3, NotAllowed=4, BadHandshake=5 };`
  - `struct ClientHello { uint32_t magic; uint16_t version; uint32_t capabilities; };`
  - `struct ServerHello { uint32_t magic; uint16_t version; uint32_t capabilities; };`
  - `std::vector<uint8_t> encode(const ClientHello&);` и `std::optional<ClientHello> decodeClientHello(const uint8_t* data, size_t len);` (аналогично для ServerHello).
  - `std::optional<uint16_t> negotiateVersion(uint16_t client, uint16_t server);` — возвращает min, если совместимы (на этом этапе: равенство), иначе `nullopt`.

- [ ] **Step 1: Написать падающие тесты** `test_protocol.cpp`:
  - `TEST_CASE("encode/decode ClientHello roundtrip")` — закодировать ClientHello{kMagic, 1, 0}, декодировать, поля совпадают.
  - `TEST_CASE("decode rejects wrong magic")` — байты с другим magic → `nullopt`.
  - `TEST_CASE("decode rejects short buffer")` — усечённый буфер → `nullopt`.
  - `TEST_CASE("negotiateVersion equal ok")` — (1,1) → 1.
  - `TEST_CASE("negotiateVersion mismatch")` — (1,2) → `nullopt`.
- [ ] **Step 2: Запустить, убедиться что падает** — `ctest -R protocol` (или прямой запуск) Expected: FAIL (символы не определены / не слинковано).
- [ ] **Step 3: Реализовать** `encode/decode*` и `negotiateVersion` в `protocol.cpp` (сетевой порядок байт, фиксированный размер записи).
- [ ] **Step 4: Запустить тесты** Expected: PASS.
- [ ] **Step 5: Commit** — `feat(protocol): hello messages and version negotiation`

---

### Task 2: Транспорт mTLS (mbedTLS)

**Files:**
- Create: `protocol/include/pvnc/tls.h`, `protocol/src/tls.cpp`
- Test: `protocol/test/test_tls.cpp`

**Interfaces:**
- Produces:
  - `class TlsContext` — загрузка CA, собственного сертификата и ключа. Конструкторы: `static TlsContext server( caPath, certPath, keyPath);` и `static TlsContext client(caPath, certPath, keyPath);` Обе требуют взаимной проверки (`MBEDTLS_SSL_VERIFY_REQUIRED`).
  - `class TlsConn` — обёртка над соединённым сокетом: `ssize_t read(void* buf, size_t);` `ssize_t write(const void* buf, size_t);` `void close();` `std::string peerCommonName() const;`
  - `TlsConn TlsContext::accept(SOCKET raw);` (серверный handshake) и `TlsConn TlsContext::connect(SOCKET raw);` (клиентский handshake). Бросают `TlsError` при неуспехе рукопожатия/проверки.
- Consumes: Winsock (`SOCKET`).

- [ ] **Step 1: Написать падающий тест** `test_tls.cpp` — интеграционный, на петле localhost, с сертификатами из `tools/make-dev-certs.ps1` (тест читает их из env-пути `PVNC_TEST_CERTS`):
  - `TEST_CASE("mutual TLS handshake succeeds and tunnels bytes")` — поднять серверный сокет в потоке с `TlsContext::server`, подключиться `TlsContext::client`; записать «ping» → прочитать «ping»; `peerCommonName()` на сервере == `portalvnc-client`.
  - `TEST_CASE("client without CA-signed cert is rejected")` — клиент с самоподписанным сертификатом → handshake бросает/закрывается, сервер не получает байт.
- [ ] **Step 2: Запустить, убедиться что падает** Expected: FAIL (нет TlsContext).
- [ ] **Step 3: Реализовать** `TlsContext`/`TlsConn` на mbedTLS: загрузка PEM, `mbedtls_ssl_conf_authmode(VERIFY_REQUIRED)`, привязка bio к сокету, извлечение CN пира из `mbedtls_ssl_get_peer_cert`.
- [ ] **Step 4: Запустить тест** Expected: PASS (оба случая).
- [ ] **Step 5: Commit** — `feat(protocol): mutual-TLS transport over winsock`

---

### Task 3: Рукопожатие поверх TLS + согласование версий

**Files:**
- Create: `protocol/include/pvnc/handshake.h`, `protocol/src/handshake.cpp`
- Test: `protocol/test/test_handshake.cpp`

**Interfaces:**
- Consumes: `TlsConn` (Task 2), `ClientHello/ServerHello/negotiateVersion/RefuseCode` (Task 1).
- Produces:
  - `struct Credentials { std::wstring domain; std::wstring user; std::wstring password; };`
  - `struct HandshakeResult { bool ok; RefuseCode refuse; uint16_t agreedVersion; Credentials creds; };`
  - `HandshakeResult serverHandshake(TlsConn&);` — читает ClientHello, при несовпадении версии шлёт ServerHello с `RefuseCode::VersionMismatch` и возвращает `ok=false`; иначе читает блок учётных данных (длина+UTF-8 поля) и возвращает их (проверку делает Task 4, не здесь).
  - `HandshakeResult clientHandshake(TlsConn&, const Credentials&);` — шлёт ClientHello, читает ServerHello, при `RefuseCode` возвращает его; иначе шлёт учётные данные.
- Примечание по безопасности: пароль живёт только в памяти, затирается после использования; в лог не пишется.

- [ ] **Step 1: Написать падающие тесты** `test_handshake.cpp` (на паре соединённых `TlsConn` в двух потоках):
  - `TEST_CASE("handshake exchanges version and credentials")` — клиент шлёт creds{dom,user,pass}, сервер их получает, `agreedVersion==1`.
  - `TEST_CASE("version mismatch yields VERSION_MISMATCH refuse")` — подменить версию клиента на 99; сервер вернул `ok=false, refuse=VersionMismatch`; клиент получил тот же код.
- [ ] **Step 2: Запустить, убедиться что падает** Expected: FAIL.
- [ ] **Step 3: Реализовать** `serverHandshake`/`clientHandshake` (кодирование кред-блока: `uint16` длина на поле, UTF-8; UTF-8↔UTF-16 через `MultiByteToWideChar`).
- [ ] **Step 4: Запустить тесты** Expected: PASS.
- [ ] **Step 5: Commit** — `feat(protocol): post-TLS handshake with version and credentials`

---

### Task 4: Авторизация по учётной записи Windows и SID

**Files:**
- Create: `protocol/include/pvnc/auth.h`, `protocol/src/auth.cpp`
- Test: `protocol/test/test_auth.cpp`

**Interfaces:**
- Produces:
  - `enum class AuthOutcome { Ok, BadCredentials, NotAdmin };`
  - `AuthOutcome authenticate(const Credentials&, const std::vector<std::wstring>& extraSids);` — `LogonUserW(LOGON32_LOGON_NETWORK)`; при успехе проверяет наличие в токене SID `S-1-5-32-544` (через `CheckTokenMembership` с well-known SID `WinBuiltinAdministratorsSid`) или любого SID из `extraSids`; возвращает `Ok`/`NotAdmin`; при неуспехе входа — `BadCredentials`. Закрывает токен.
  - `bool tokenHasSid(HANDLE token, PSID sid);` (внутренняя, тестируемая отдельно от LogonUser).
- Consumes: `Credentials` (Task 3).

- [ ] **Step 1: Написать падающие тесты** `test_auth.cpp` (часть помечена как требующая реальных кред — через env `PVNC_TEST_USER/PASS`, пропуск если нет):
  - `TEST_CASE("tokenHasSid finds Administrators for current elevated process token")` — открыть токен своего процесса, собрать well-known Administrators SID, проверить что `tokenHasSid` его находит (запускается в контексте админа; если процесс не админ — тест помечается как ожидаемо `false`, проверяем логику на заведомо отсутствующем SID).
  - `TEST_CASE("authenticate rejects wrong password as BadCredentials")` — заведомо неверный пароль → `BadCredentials`.
  - (опц., при наличии env) `TEST_CASE("authenticate admin user returns Ok")`.
- [ ] **Step 2: Запустить, убедиться что падает** Expected: FAIL.
- [ ] **Step 3: Реализовать** `authenticate`/`tokenHasSid`.
- [ ] **Step 4: Запустить тесты** Expected: PASS (с учётом условных пропусков).
- [ ] **Step 5: Commit** — `feat(protocol): windows logon + builtin-admins SID authorization`

---

### Task 5: Фильтр клиентов по IP + конфиг агента

**Files:**
- Create: `agent/src/config.h`, `agent/src/config.cpp`
- Test: `agent/test/test_config.cpp`
- Create пример: `agent/config.example.json`

**Interfaces:**
- Produces:
  - `struct AgentConfig { bool trayIcon=false; std::string listenAddress="0.0.0.0"; uint16_t port=7654; std::string tlsCert, tlsKey, tlsCa; std::vector<std::string> allowedClients; std::vector<std::wstring> accessSids; std::string logFile; };`
  - `AgentConfig loadConfig(const std::string& path);` — читает JSON (мини-парсер или vcpkg `nlohmann-json`; добавить в манифест при выборе); отсутствующие ключи = безопасные значения выше.
  - `bool isClientAllowed(const std::string& ip, const std::vector<std::string>& allowed);` — пустой список = `false` (никого); поддержка одиночных IP и CIDR IPv4.
- Consumes: —

- [ ] **Step 1: Написать падающие тесты** `test_config.cpp`:
  - `TEST_CASE("defaults when keys absent")` — пустой `{}` → port 7654, trayIcon false, allowedClients пуст.
  - `TEST_CASE("empty allowlist denies all")` — `isClientAllowed("10.0.0.5", {})` == false.
  - `TEST_CASE("exact ip allowed")` и `TEST_CASE("cidr match")` — `192.168.1.0/24` пускает `.1.50`, не пускает `.2.50`.
- [ ] **Step 2: Запустить, убедиться что падает** Expected: FAIL.
- [ ] **Step 3: Реализовать** загрузку JSON и `isClientAllowed` (CIDR через разбор маски). Создать `config.example.json` со всеми ключами.
- [ ] **Step 4: Запустить тесты** Expected: PASS.
- [ ] **Step 5: Commit** — `feat(agent): config.json loader and ip allowlist`

---

### Task 6: Захват рабочего стола (GDI) и кадровый источник

**Files:**
- Create: `agent/src/capture_gdi.h`, `agent/src/capture_gdi.cpp`
- Test: `agent/test/test_capture.cpp`

**Interfaces:**
- Produces:
  - `struct Frame { int width; int height; int stride; std::vector<uint8_t> bgra; };`
  - `class GdiCapture { public: GdiCapture(); bool grab(Frame& out); int width() const; int height() const; };` — `grab` берёт снимок основного монитора через `BitBlt` из `GetDC(NULL)` в DIB-секцию, формат BGRA32.
- Consumes: —

- [ ] **Step 1: Написать падающий тест** `test_capture.cpp`:
  - `TEST_CASE("grab returns a frame matching screen metrics")` — `width()/height()` == `GetSystemMetrics(SM_CXSCREEN/CYSCREEN)`; после `grab` размер буфера == `stride*height`, buffer не весь нулевой.
- [ ] **Step 2: Запустить, убедиться что падает** Expected: FAIL.
- [ ] **Step 3: Реализовать** `GdiCapture`.
- [ ] **Step 4: Запустить тест** Expected: PASS.
- [ ] **Step 5: Commit** — `feat(agent): GDI desktop capture`

---

### Task 7: Агент-сервер — RFB поверх mTLS, цикл приёма

**Files:**
- Create: `agent/src/rfb_server.h`, `agent/src/rfb_server.cpp`, `agent/src/main.cpp`
- Test: `agent/test/test_rfb_server.cpp` (юнит на адаптер транспорта; сквозная проверка — ручная, в Task 9)

**Interfaces:**
- Consumes: `TlsContext/TlsConn` (2), `serverHandshake` (3), `authenticate` (4), `AgentConfig/isClientAllowed` (5), `GdiCapture/Frame` (6).
- Produces:
  - `class RfbServer { RfbServer(const AgentConfig&); int run(); };` — цикл: `accept` TCP → проверка IP (`isClientAllowed`, до TLS) → `TlsContext::accept` → `serverHandshake` → при `ok` `authenticate`; при `Ok` запускает libvncserver, где framebuffer наполняется из `GdiCapture`, а ввод (`ptrAddEvent`/`kbdAddEvent`) применяется через `SendInput`. Весь RFB I/O идёт через `TlsConn` (адаптер сокет-функций libvncserver на наш TLS — ключевая интеграция).
  - Освобождение сессии и ресурсов при разрыве; сервер продолжает принимать подключения.
- Примечание: интеграция «libvncserver поверх произвольного транспорта» — главный технический риск; снять спайком до полной реализации (см. раздел ниже).

- [ ] **Step 1:** Написать юнит-тест `test_rfb_server.cpp` на адаптер транспорта: обёртка, предъявляющая libvncserver функции чтения/записи, которые ходят в `TlsConn`; тест гоняет байты через адаптер на паре localhost-TLS. (Полный RFB — вручную в Task 9.)
- [ ] **Step 2:** Запустить, убедиться что падает. Expected: FAIL.
- [ ] **Step 3:** Реализовать адаптер транспорта и `RfbServer::run`; `main.cpp` читает путь к `config.json` (рядом с exe по умолчанию), создаёт `RfbServer`, запускает. Ввод через `SendInput`. Несколько клиентов — libvncserver рассылает один framebuffer всем.
- [ ] **Step 4:** Запустить юнит-тест адаптера. Expected: PASS.
- [ ] **Step 5: Commit** — `feat(agent): RFB server over mTLS with capture and input`

---

### Task 8: Просмотрщик — окно Win32, RFB-клиент поверх mTLS

**Files:**
- Create: `viewer/src/main.cpp`, `viewer/src/rfb_client.h`, `viewer/src/rfb_client.cpp`, `viewer/src/window.h`, `viewer/src/window.cpp`
- Test: `viewer/test/test_rfb_client.cpp` (юнит на адаптер транспорта)

**Interfaces:**
- Consumes: `TlsContext/TlsConn` (2), `clientHandshake/Credentials` (3).
- Produces:
  - `class RfbClient { RfbClient(host, port, TlsContext, Credentials); bool connectAndRun(FrameSink&); };` — TCP → `TlsContext::connect` → `clientHandshake(creds)`; при отказе возвращает код; иначе libvncclient поверх `TlsConn`-адаптера; обновления экрана → `FrameSink`.
  - `window.*` — окно Win32, рисует кадры (`StretchDIBits`), шлёт мышь/клавиатуру обратно в `RfbClient`.
  - CLI `main.cpp`: аргументы `--host --port --ca --cert --key --user --domain` (пароль — через prompt/stdin, не в argv).
- Примечание: тот же адаптер транспорта, что в Task 7, но для libvncclient.

- [ ] **Step 1:** Написать юнит-тест `test_rfb_client.cpp` на транспортный адаптер клиента (симметрично Task 7).
- [ ] **Step 2:** Запустить, убедиться что падает. Expected: FAIL.
- [ ] **Step 3:** Реализовать `RfbClient`, адаптер, окно, разбор аргументов.
- [ ] **Step 4:** Запустить юнит-тест. Expected: PASS.
- [ ] **Step 5: Commit** — `feat(viewer): windowed RFB client over mTLS`

---

### Task 9: Сквозная ручная проверка и скрипт запуска

**Files:**
- Create: `tools/run-local-slice.ps1` (генерит сертификаты при отсутствии, запускает агент с dev-конфигом, печатает команду запуска просмотрщика)
- Create: `docs/superpowers/plans/slice-acceptance.md` (чек-лист ручной приёмки)

**Interfaces:**
- Consumes: всё выше.

- [ ] **Step 1:** `run-local-slice.ps1`: `make-dev-certs.ps1` при отсутствии `certs/`, сгенерировать `certs/agent-config.json` (listen 127.0.0.1:7654, allowedClients `["127.0.0.1"]`, пути к сертификатам), запустить `pvnc_agent`.
- [ ] **Step 2:** Запустить агент скриптом; запустить `pvnc_viewer` против `127.0.0.1:7654` с текущей учётной записью Windows (должна быть админом). Проверить по чек-листу:
  - Видно собственный рабочий стол, обновляется.
  - Движение мыши и ввод с клавиатуры из просмотрщика действуют на рабочем столе.
  - Запуск просмотрщика с **чужим** сертификатом (самоподписанным) → соединение отклонено.
  - Вход **не-админом** (если доступна такая учётка) → отказ `AUTH_NOT_ADMIN`.
  - Подключение с адреса не из allowlist (изменить конфиг на пустой список) → отказ.
- [ ] **Step 3:** Записать результаты в `slice-acceptance.md` (факт прохождения каждого пункта с выводом).
- [ ] **Step 4: Commit** — `test: end-to-end local slice acceptance + run script`

---

## Самопроверка (выполнена при написании)

- **Покрытие спецификации (для среза):** mTLS + отсев чужих (T2), рукопожатие/версии (T1,T3), SID-авторизация на любой локали (T4), конфиг + IP-фильтр + порт 7654 + значок-флаг (T5), захват (T6), сервер с несколькими клиентами и вводом (T7), просмотрщик (T8), сквозная приёмка (T9). Вне среза (следующие планы): служба, Secure Desktop/UAC, терминальный сервер/мультисессия, шлюз портала + браузер, MSI.
- **Результат спайка (2026-10-22):** в текущем vcpkg **нет порта `libvncserver`**, а адаптация libvncserver/libvncclient под произвольный транспорт дорога. Решение: реализуем **минимальный RFB 3.8 сами** поверх `TlsConn` (Task 7/8), сжатие — через `zlib` (encoding `zlib`/ZRLE; при необходимости позже libjpeg-turbo/Tight). Это убирает тяжёлую непакетированную зависимость и снимает риск «RFB поверх кастомного транспорта»: наш RFB-цикл пишет и читает напрямую через `TlsConn`. Внешний контракт не меняется — на проводе стандартный RFB, совместимый с noVNC. Зависимости vcpkg: `mbedtls`, `zlib`, `nlohmann-json`, `catch2`.
- **Типы согласованы:** `Credentials`, `TlsConn`, `AgentConfig`, `Frame`, `RefuseCode` — определены один раз и переиспользуются по ссылкам в Interfaces.
