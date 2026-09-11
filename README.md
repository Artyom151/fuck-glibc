# fglibc (Fast Glibc Compatibility Layer & Wine-like Runner)

<div align="center">

[![Linux](https://img.shields.io/badge/Target_OS-Alpine_Linux_(musl)-blue.svg?style=for-the-badge&logo=alpinelinux)](https://alpinelinux.org/)
[![C++17](https://img.shields.io/badge/C%2B%2B-17_Standard-orange.svg?style=for-the-badge&logo=c%2B%2B)](https://en.wikipedia.org/wiki/C%2B%2B17)
[![Overhead](https://img.shields.io/badge/CPU_Overhead-0%25_(Native)-brightgreen.svg?style=for-the-badge)](https://kernel.org)
[![Security](https://img.shields.io/badge/Root_Required-NO_(Unprivileged)-red.svg?style=for-the-badge)](https://man7.org/linux/man-pages/man7/user_namespaces.7.html)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg?style=for-the-badge)](LICENSE)

**«I hate glibc, but the world forces us to run glibc binaries.»**  
*Запускайте любые Linux glibc-игры, Electron-приложения (Discord, Spotify, Obsidian), Steam и Windows .exe на Alpine Linux и musl-дистрибутивах без оверхеда, без root и без Docker!*

</div>

---

## 💥 Философия: Почему «I hate glibc»?

Мы выбираем **Alpine Linux**, **Void musl**, **Chimera Linux** или **postmarketOS** ради их невероятной лёгкости, чистоты, минимализма, быстродействия и безопасности. В них используется простая и соответствующая стандартам библиотека **musl libc**.

Однако проприетарный мир Linux (игры в Steam, Discord, Spotify, Obsidian, VS Code, коммерческий софт) упрямо компилируется исключительно под тяжеловесный **GNU libc (glibc)** с его раздутыми версиями символов (`memcpy@GLIBC_2.14`, `pow@GLIBC_2.29`), внутренними несовместимыми структурами и нестандартными расширениями.

### В чём проблема традиционных решений?
- **gcompat / libc6-compat**: Падают на сложных многопоточных программах, C++ STL, сокетах, динамическом линковании или специфичных макросах glibc.
- **Docker / Podman**: Колоссальный оверхед по памяти, изоляция процессов мешает нормальной интеграции с рабочим столом, проброс PulseAudio, PipeWire, Wayland и GPU требует кучи флагов и привилегий.
- **Chroot**: Требует прав `root`/`sudo`, засоряет хост и ломает системные пути.

### Решение — `fglibc`:
**`fglibc` работает по концепции Wine или Proton, но для GNU libc.**  
Он создаёт изолированный легковесный префикс пользователя (`~/.fglibc`) через непривилегированные пользовательские пространства имён (`CLONE_NEWUSER`), монтирует в него чистый glibc рантайм, прозрачно пробрасывает хостовый GPU (`/dev/dri`), дисплей (Wayland/X11) и звук (PipeWire/PulseAudio/ALSA).

**Результат — 0% CPU оверхеда: процесс выполняется ядром Linux абсолютно нативно!**

---

## 🏗 Как это устроено (Архитектура)

```
                              [ fglibc CLI ]
                                     │
      ┌──────────────────────────────┴──────────────────────────────┐
      ▼                                                             ▼
[ Linux User & Mount Namespaces ]                       [ Glibc Prefix (~/.fglibc) ]
  • unshare(CLONE_NEWUSER | CLONE_NEWNS)                  • GNU C Library (glibc 2.44+)
  • Сохранение UID/GID (1000:1000, не root)               • GCC C++ Runtime (libstdc++, libgcc)
  • pivot_root на чистый tmpfs rootfs                     • Vulkan ICD Loader & Mesa (OpenGL/DRI)
  • Overlayfs для /bin (родной glibc bash)                • SDL2 / SDL3 / OpenAL / Audio Stack
  • Прямой проброс GPU (/dev/dri/renderD128)              • Независимый кэш /etc/ld.so.cache
  • Прямой проброс Wayland / X11 (/tmp/.X11-unix)         • Мультиархитектура (x86_64 & 32-bit lib32)
  • Прямой проброс PipeWire / PulseAudio / ALSA           • Манифесты пакетов & ELF-сканер
```

### Ключевые архитектурные фичи:
1. **Unprivileged Namespace**: Работает от обычного пользователя без `sudo` или SUID-бинарников.
2. **UID/GID Mapping**: Процессы внутри видят реальный UID пользователя (1000:1000). Electron-приложения (Discord, Spotify, Obsidian) не ругаются на запуск из-под root.
3. **Overlayfs на `/bin`**: Скрипты с шебангом `#!/bin/bash` или `#!/bin/sh` прозрачно запускают родной glibc `bash`, что исключает конфликты хостового musl bash с glibc библиотеками (`readline`/`ncurses`).
4. **Автоматический Binary ELF Scanner**: При установке любого приложения проверяются все бинарные зависимости `DT_NEEDED`. Если не хватает хоть одной библиотеки, она вычисляется и скачивается в том же запуске. Больше никаких ошибок `cannot open shared object file`!
5. **Мультиархитектурность 32-bit & 64-bit**: Поддерживает как 64-битный софт, так и 32-битные библиотеки (`/usr/lib32`, `ld-linux.so.2`), необходимые для Steam и старых игр.

---

## 🚀 Быстрый старт

### 1. Сборка и установка:
```bash
# Клонируем и собираем
make -j$(nproc)

# Устанавливаем в ~/.local/bin или /usr/local/bin
./scripts/install.sh
```

### 2. Инициализация окружения:
```bash
# Базовый glibc + C++ runtime:
fglibc init

# Или сразу с полным игровым стеком (Mesa, Vulkan, OpenGL, SDL2/3):
fglibc init --gaming
```

### 3. Запуск приложений (в стиле Wine):
```bash
# Запуск любой Linux glibc программы или игры:
fglibc ./my_glibc_game

# Запуск установленных приложений:
fglibc discord
fglibc spotify
fglibc obsidian

# Автоматический запуск Windows .exe через Steam Proton или Wine:
fglibc ~/.steam/steam/steamapps/common/GeometryDash/GeometryDash.exe
```

---

## 📦 Встроенный менеджер пакетов

`fglibc` включает в себя полноценный пакетный менеджер, подключённый к репозиториям Arch Linux (Core, Extra, Multilib) и Chaotic-AUR (**более 18 600 готовых пакетов**).

### Интерактивный TUI:
```bash
fglibc install
```
Открывает полноэкранный интерфейс с поиском, категориями и переключением пакетов клавишей `[Space]`.

### Пакетные бандлы (Meta-packages):
Вам не нужно вручную подбирать сотни библиотек. Доступны готовые наборы:
- `fglibc install electron` — полный стек для Chromium/Electron (Discord, VS Code, Obsidian, Spotify).
- `fglibc install gaming` — Vulkan, Mesa (Radeon, Intel, Software), libglvnd OpenGL, SDL2/3, OpenAL, Wayland/X11.
- `fglibc install media` — FFmpeg, PipeWire, PulseAudio, ALSA, кодеки (Vorbis, FLAC, Opus, MP3 LAME).
- `fglibc install steam` — 32-битный и 64-битный рантайм для клиента Valve Steam.
- `fglibc install wine-deps` — графические и аудио библиотеки для Wine/Proton.

### Установка с авто-разрешением:
```bash
# Установка конкретного пакета (все зависимости подтянутся автоматически):
fglibc install fastfetch screenfetch

# Принудительная установка без использования кэша:
fglibc install --no-cache which
```

### Деинсталляция пакетов:
```bash
# Удалить пакет и все установленные им файлы:
fglibc uninstall screenfetch

# Посмотреть список всех установленных пакетов:
fglibc list
```

### Очистка дискового кэша:
```bash
# Удалить скачанные архивы .pkg.tar.zst из кэша (~/.fglibc/cache):
fglibc clean

# Или краткий синоним:
fglibc --no-cache
```

---

## ⚙️ Пост-установка и интеграция с рабочим столом (`fglibc setup`)

`fglibc setup` написан полностью на C++ и включает интерактивный TUI-селектор для настройки зеркал, пакетных менеджеров и окружения рабочего стола:

```bash
# Интерактивный TUI с чекбоксами [x] / [ ]:
fglibc setup
```

### Возможности `fglibc setup`:
- **Поиск и ранжирование самых быстрых зеркал (`fglibc setup mirrors` / `fglibc mirrors`)**:
  Параллельно замеряет пинг и задержку к мировым зеркалам (Fastly, Geo, Yandex, Leaseweb, Kernel.org), сортирует их и записывает активный `mirrorlist`.
- **Интеграция с рабочими столами (`fglibc sync-desktop`)**:
  Автоматически сканирует `.desktop` файлы и иконки приложений (Spotify, Discord, Yandex Music, Obsidian и др.) внутри префикса, переписывает `Exec=` на `fglibc <app>` и регистрирует их в **KDE Plasma, GNOME, Hyprland, Rofi, Wofi**. Приложения появляются в системном меню и запускаются в фоне в 1 клик без открытого терминала!
  *(Интеграция также срабатывает автоматически при любой установке через `fglibc install`).*
- **Поддержка пакетных менеджеров**:
  - `pacman` — нативный пакетный менеджер Arch Linux с локальной базой ALPM v16.
  - `yay` — Go-хелпер для сборки и установки из Arch User Repository (AUR).
  - `paru` — Rust-хелпер для AUR.
  - `apt` & `dpkg` — инструменты управления deb-пакетами Debian/Ubuntu.
  - `apk` — поддержка пакетов Alpine Linux внутри glibc-префикса.
  - `devel` — стек компиляции (fakeroot, gcc, make, patch, binutils, git).

---

## 🛠 Команды интерфейса (`fglibc --help`)

| Команда | Описание |
|---|---|
| `fglibc setup [component]` | Интерактивная настройка пакетных менеджеров (pacman, yay, paru, apt, apk), зеркал и ярлыков |
| `fglibc sync-desktop` | Экспорт и синхронизация ярлыков и иконок в меню KDE, GNOME, Hyprland, Rofi |
| `fglibc mirrors` | Параллельный тест задержки и выбор самых быстрых зеркал Arch Linux |
| `fglibc <binary> [args...]` | Запустить программу или Windows `.exe` напрямую |
| `fglibc run <binary> [args...]` | Явный запуск программы в окружении glibc |
| `fglibc win-setup <component>` | Настройка Windows-сервисов (webview2, vcrun, dxvk, fonts, all) |
| `fglibc winetricks [verb...]` | Запуск Winetricks для установки любых компонентов Windows |
| `fglibc shell` | Интерактивный glibc bash-шелл внутри префикса |
| `fglibc check <binary>` | Глубокий анализ ELF/PE заголовков, версий glibc и зависимостей |
| `fglibc init [--gaming]` | Инициализация или сброс рантайм-префикса |
| `fglibc install` | Интерактивный TUI-менеджер пакетов с авто-поиском в AUR |
| `fglibc install [--no-cache] <pkg...>` | Установка пакетов с авто-разрешением soname-зависимостей и fallback в AUR |
| `fglibc install gaming` | Установка полного игрового стека (Mesa, Vulkan, SDL, OpenGL) |
| `fglibc install <archive.pkg.tar.zst>` | Ручная распаковка любого Arch-пакета или tar-архива в префикс |
| `fglibc uninstall <pkg1> [pkg2...]` | Деинсталляция пакета по файловому манифесту |
| `fglibc clean` / `fglibc --no-cache` | Очистка архивов из дискового кэша (`~/.fglibc/cache`) |
| `fglibc list` | Список всех установленных пакетов в префиксе |
| `fglibc test` | Запуск полного комплекса стресс-тестов (12 проверок) |
| `fglibc status` | Отчёт о статусе префикса, версии glibc, DRI GPU и звуке |
| `fglibc version` | Вывод информации о версии |
| `fglibc help` | Вывод подробной справки |

---

## 🔍 Инспектор бинарников (`fglibc check`)

Встроенный парсер ELF анализирует любой бинарник перед запуском:
```bash
fglibc check ./my_game
```
Пример отчёта:
```
=== fglibc ELF Inspection Report ===
File: /usr/lib/electron43/electron
Arch: x86_64 (64-bit)
Type: Shared Object / PIE Executable (ET_DYN)
Interpreter (PT_INTERP): /lib64/ld-linux-x86-64.so.2 [GLIBC - Target binary requires fglibc!]

Required Symbol Versions:
  - GLIBC_2.14
  - GLIBC_2.34
  - GLIBC_2.44
  - GCC_4.2.0

Shared Library Dependencies (48 total):
  ✔ libgtk-3.so.0      -> ~/.fglibc/sysroot/usr/lib/libgtk-3.so.0
  ✔ libnss3.so         -> ~/.fglibc/sysroot/usr/lib/libnss3.so
  ✔ libasound.so.2     -> ~/.fglibc/sysroot/usr/lib/libasound.so.2
  ✔ libpulse.so.0      -> ~/.fglibc/sysroot/usr/lib/libpulse.so.0
  ✔ libasyncns.so.0    -> ~/.fglibc/sysroot/usr/lib/libasyncns.so.0
  ✔ libmp3lame.so.0    -> ~/.fglibc/sysroot/usr/lib/libmp3lame.so.0

Status: All dependencies satisfied in fglibc prefix! Ready to run.
```

---

## 🪟 Windows Сервисы и Рантаймы (WebView2, VC++, DXVK, Шрифты, Winetricks)

`fglibc` включает автоматический слой поддержки для запуска сложных Windows-приложений, лаунчеров и игр, требующих специфических системных сервисов Windows:

### 1. Microsoft Edge WebView2 (`fglibc win-setup webview2`)
Многие современные лаунчеры (лавунчеры игр, Minecraft-клиенты, Sk3dGuard, EnergyLauncher, кастомные клиенты) используют компонент **Microsoft Edge WebView2** для отрисовки интерфейса.  
При обычном запуске под Wine стандартный онлайн-установщик `MicrosoftEdgeWebView2Setup.exe` падает или зависает из-за отсутствия служб EdgeUpdate/BITS.

**Как это решено в `fglibc`:**
- Команда `fglibc win-setup webview2` развёртывает полноценный Evergreen WebView2 Runtime в Wine-префикс.
- Автоматически прописываются необходимые ключи реестра в `HKLM\Software\WOW6432Node\Microsoft\EdgeUpdate\Clients\{F3017226-FE2A-4295-8BDF-00C3A9A7E4C5}`.
- При запуске любых `.exe` через `fglibc` автоматически пробрасывается флаг `WEBVIEW2_ADDITIONAL_BROWSER_ARGUMENTS="--no-sandbox"`, предотвращающий краши песочницы Chromium в не-Windows средах.

### 2. Visual C++ Redistributables (`fglibc win-setup vcrun`)
Автоматически скачивает и устанавливает последние официальные пакеты Microsoft Visual C++ 2015-2022 (`vc_redist.x64.exe` и `vc_redist.x86.exe`):
- `vcruntime140.dll`, `vcruntime140_1.dll`
- `msvcp140.dll`, `concrt140.dll`, `vccorlib140.dll`

### 3. Core Fonts (`fglibc win-setup fonts`)
Настраивает базовый набор шрифтов TrueType в `drive_c/windows/Fonts/` (Arial, Times New Roman, Courier New, Tahoma, Segoe UI, Verdana) и регистрирует их алиасы в реестре `FontSubstitutes`. Это полностью устраняет падения интерфейсов из-за отсутствия шрифтов.

### 4. DXVK и VKD3D (`fglibc win-setup dxvk` / `vkd3d`)
Устанавливает трансляцию DirectX 9/10/11 и DirectX 12 в Vulkan с настройкой `native` переопределений DLL в реестре Wine.

### 5. Интеграция с Winetricks (`fglibc winetricks`)
Быстрый доступ к библиотеке скриптов Winetricks:
```bash
# Установка любого компонента Windows в активный префикс:
fglibc winetricks dotnet48
fglibc winetricks d3dcompiler_47
fglibc winetricks sound=pulse
```

### Проверка статуса Windows-окружения:
```bash
fglibc win-setup status
```
Выведет наглядный отчёт о наличии WebView2, версии Wine, шрифтах, VC++ и DXVK.

---

## 🧪 Комплексные стресс-тесты (`fglibc test`)

В репозиторий встроен исчерпывающий набор стресс-тестов для верификации стабильности:
```bash
make test
# или
fglibc test
```

### 12 этапов тестирования:
1. **CLI Version & Environment Status** — проверка целостности конфигурации и системных статусов.
2. **ELF Parser & Dependency Inspection** — проверка разбора 64-bit / 32-bit ELF заголовков.
3. **Native C Glibc Execution** — запуск glibc 2.44, сверка UID/GID (1000/1000), доступ к `/dev/dri`.
4. **C++11 Threading & STL Compatibility** — проверка `std::thread`, POSIX pthreads под glibc, математики.
5. **SDL2 & OpenGL Context Initialization** — тест графического контекста (Mesa llvmpipe / GPU).
6. **Shebang Execution** — запуск вложенных скриптов `#!/bin/bash` через overlayfs `/bin`.
7. **Concurrency Stress Test** — **30 одновременных параллельных процессов `fglibc`** без коллизий монтирования и дедлоков tmpfs.
8. **Exit Code & Signal Preservation** — сохранение кодов завершения (0, 42, 127) и сигналов (SIGINT/SIGTERM).
9. **I/O Streaming & Pipe Throughput** — потоковая передача 100 000 строк через pipes без задержек.
10. **Package Manager Lifecycle** — установка с `--no-cache`, генерация манифеста, запуск, удаление через `uninstall`.
11. **Real-World Apps Check** — проверка запуска реальных приложений (Spotify, Discord, Obsidian).
12. **Windows Compatibility Services & Tools** — проверка статуса `win-setup` (WebView2, VC++, DXVK) и вызова `winetricks`.

---

## ⚙️ Переменные окружения

| Переменная | Значение по умолчанию | Описание |
|---|---|---|
| `FGLIBC_PREFIX` | `~/.fglibc` | Путь к изолированному префиксу с sysroot и кэшем |
| `FGLIBC_DEBUG` | `0` | Включить подробный вывод отладки и точек монтирования (`1`) |
| `FGLIBC_NO_DRI` | `0` | Отключить проброс видеокарты `/dev/dri` |
| `FGLIBC_NO_AUDIO` | `0` | Отключить проброс звука (PulseAudio, ALSA) |
| `FGLIBC_LIBRARY_PATH` | *(пусто)* | Дополнительные пути поиска библиотек |

---

## 📄 Лицензия

Проект распространяется под свободной лицензией **MIT**. Используйте, модифицируйте и наслаждайтесь чистым Alpine Linux без необходимости терпеть системный glibc!
