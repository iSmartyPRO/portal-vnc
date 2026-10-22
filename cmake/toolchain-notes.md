# Сборка PortalVNC — заметки по окружению

## Что нужно

- **Visual Studio Build Tools 2022** с рабочей нагрузкой C++ (MSVC 14.44 проверен).
- **Windows SDK 10** (10.0.26100 проверен).
- **CMake ≥ 3.25** (проверен 4.3).
- **vcpkg** — лежит в `./vcpkg` (клон, не коммитится). Bootstrap:
  `./vcpkg/bootstrap-vcpkg.bat -disableMetrics`.

## Зависимости (vcpkg, триплет `x64-windows-static`)

Описаны в `vcpkg.json` (manifest mode): `mbedtls`, `zlib`, `nlohmann-json`,
`catch2`. Ставятся автоматически при конфигурации CMake, либо вручную:

```
./vcpkg/vcpkg.exe install --triplet x64-windows-static --x-manifest-root=.
```

> Порта `libvncserver` в vcpkg нет — RFB реализуется в проекте самостоятельно
> (см. план №1, раздел «Результат спайка»).

## Конфигурация и сборка

```powershell
cmake --preset default
cmake --build --preset default
```

Пресет `default` (см. `CMakePresets.json`): генератор Ninja, `Release`,
toolchain-файл vcpkg, `VCPKG_TARGET_TRIPLET=x64-windows-static`,
`CMAKE_MSVC_RUNTIME_LIBRARY=MultiThreaded` (статический CRT — один exe без
зависимостей от vcredist).

Результаты: `build/protocol/pvnc_protocol_tests.exe`,
`build/agent/pvnc-agent.exe`, `build/viewer/pvnc-viewer.exe`.

## Запуск из командной строки без CMake-пресета

Открыть «x64 Native Tools Command Prompt for VS 2022», затем `cmake --preset default`.
Либо из обычной оболочки CMake сам находит MSVC через toolchain vcpkg.

## Тесты

```powershell
ctest --preset default --output-on-failure
```

Интеграционные тесты mTLS/handshake требуют dev-сертификатов и переменной
окружения `PVNC_TEST_CERTS`, указывающей на папку с ними:

```powershell
powershell -ExecutionPolicy Bypass -File tools/make-dev-certs.ps1
$env:PVNC_TEST_CERTS = "$PWD\certs"
ctest --preset default --output-on-failure
```

Живой тест входа (`[auth][live]`) требует `PVNC_TEST_USER` / `PVNC_TEST_PASS`
(и опц. `PVNC_TEST_DOMAIN`); без них он пропускается.
