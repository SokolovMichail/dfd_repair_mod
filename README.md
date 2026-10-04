# DFDRepairMod — моды для Terminator: Dark Fate – Defiance

Проект целиком: нативные плагины (C++), сборка данных мода (Python), графика, упаковка релиза
и инструменты разбора движка. Первый результат — мод **HEMTT Spares**.

| Документ | О чём |
|---|---|
| [`INSTALL.md`](INSTALL.md) | установка, проверка, удаление (RU/EN) — для игроков |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | как устроен мод: данные, загрузчик, плагин, сборка |
| [`docs/engine_notes.md`](docs/engine_notes.md) | что известно о движке (адреса, поля, слоты) |
| [`re/README.md`](re/README.md) | инструменты разбора DLL игры |

## HEMTT Spares

Новая машина: HEMTT, который пополняет технике **запчасти** — как HEMTT Supply боеприпасы.
F4 (режим снабжения) — раздача всем машинам в зоне (круг из гаечных ключей), или приказ на конкретную машину.
Кампания Resistance: 1 шт. в магазине первого сектора + в тех же наградах, что HEMTT Supply. Скирмиш: Founders.

Состоит из двух частей:

- **данные** — `mods/HEMTT_Spares`: юнит `Fnd_HEMTT_repair` (копия `Fnd_HEMTT_ammunition`,
  `abilities=ammunition_recharge,support`, запчасти 10000, патроны 500), магазин, армия скирмиша,
  локализация (en), значки, материал и текстура круга, иконка мода;
- **плагин** — без него движок так не умеет (подробно — в начале
  [`plugins/spares_supply/spares_supply.cpp`](plugins/spares_supply/spares_supply.cpp)):
  разрешает `ammunition_recharge` рядом с `support` (кнопка F4), в режиме F4 включает и тикает компонент
  запчастей, запрещает такой машине раздавать патроны, рисует круг материалом запчастей.

Проверено на GOG 1.09.1221. Адреса в `logic.dll` не зашиты — плагин ищет их по байтовым шаблонам и RTTI,
поэтому должен работать и на Steam. Не нашлось/не совпало хоть что-то — ничего не меняет и пишет в лог.
Только одиночная игра.

## Структура

```
CMakeLists.txt         цели сборки (см. ниже), версия мода MOD_VERSION
.env.example           шаблон локальных настроек -> скопировать в .env
.run/                  готовые конфигурации запуска CLion: Build release / Deploy to game / Build mod data
common/patch.h         общее для плагинов: лог, поиск по шаблонам и RTTI, набор правок, ожидание загрузки DLL
loader/                winhttp.dll: прокси системной winhttp.dll + загрузчик bin\plugins\*.dll
plugins/spares_supply/ SparesSupply.dll
tools/                 gen_winhttp_proxy.py - генерация переходников winhttp (нужен редко)
mod/
  build_mod.py         сборка папки мода из актуальных файлов игры (что и откуда - в начале файла)
  package.py           архив релиза + обложка
  art/                 графика: круг (make_circle_spares), значки юнита (make_unit_icons), иконка мода (make_mod_icon)
  tools/               gamefiles.py (поиск актуальной версии файла в слоях игры), patch_table.py (правка
                       Excel-XML таблиц), patch_army_toml.py, pak_extract.py (ручная распаковка), table_grep.py
  dist_files/          README для игроков (кладётся в архив)
re/                    инструменты разбора DLL игры (см. re/README.md)
docs/                  engine_notes.md - что известно о движке; исходный план HEMTT Spares
dist/                  результат «собрать релиз» (в git не кладётся)
```

## Подготовка

1. **CLion** с MinGW из комплекта (GCC 15) — или любой x86_64 MinGW-w64 с UCRT.
2. **Python 3.12+** в PATH и пакеты: `pip install pillow numpy` (графика), `capstone` — только для `re/`.
3. **Игра** установлена. Путь — CMake-переменная `GAME_DIR`
   (по умолчанию `C:/Games/Terminator Dark Fate - Defiance`; поменять: Settings → Build → CMake → CMake options
   `-DGAME_DIR=...`).
4. **`.env`** в корне (скопировать из `.env.example`): `DFD_PAK_PASSWORD` — пароль архивов игры, без него
   сборка мода не достанет файлы игры. Можно задать и переменной окружения. В git `.env` не кладётся.
   Для `re/` можно добавить `DFD_GAME_DIR`.

## Цели сборки

| Цель | Что делает |
|---|---|
| `winhttp`, `SparesSupply` | DLL (профиль CLion, обычно Debug). |
| `mod` | Собирает данные мода в `<build>/stage/mods/HEMTT_Spares`. Файлы игры каждый раз берутся заново — после обновления игры достаточно пересобрать. |
| `deploy` | Ставит в игру: `bin\winhttp.dll`, `bin\plugins\SparesSupply.dll`, `mods\HEMTT_Spares`. Игра должна быть закрыта; мод нужно один раз включить в меню MODS. |
| `release` | **Собрать релиз**: отдельная Release-сборка DLL (оптимизация, без отладочной информации) + данные мода + `dist/HEMTT_Spares_v<версия>.zip` и `dist/HEMTT_Spares_v<версия>_cover.png`. |

В CLion: выбрать конфигурацию **Build release** (или **Deploy to game**) и нажать Run — либо Build → Build Target.
Из консоли:

```bash
cmake --build cmake-build-debug --target release
```

Версия — `MOD_VERSION` в `CMakeLists.txt`: попадает в имя архива, описание мода и лог плагина.

## Проверка в игре

`bin\plugins\loader.log` — `loaded SparesSupply.dll`;
`bin\plugins\SparesSupply.log` — `SparesSupply <версия> loaded` … `SparesSupply active`.
Если там `not supported` — у игры другой код: смотреть, какой шаблон не нашёлся, и обновлять его через
`re/mksig.py` (см. `docs/engine_notes.md`).

## Новый плагин

1. `plugins/<имя>/<имя>.cpp`, подключить `common/patch.h`.
2. В `DllMain` — открыть лог и `DllWatcher::watch(L"logic.dll", cb)`. В `cb`: `dfd::Image img(base)` →
   найти адреса (`img.find(шаблон)`, `img.vtableOf("Класс@Leonardo")`) → `PatchSet` (`bytes` / `vslot`
   с шаблоном начала функции) → `apply`.
3. В `CMakeLists.txt`: `add_library(<имя> SHARED ...)`, `dfd_dll(<имя>)`, копирование в `deploy` и в `package.py`.

`DllMain` плагина выполняется под loader lock (его вызывает `DllMain` загрузчика): никаких `LoadLibrary` и
ожиданий других потоков. Колбэк `DllWatcher` вызывается после отображения `logic.dll`, до её кода.

## Удаление из игры

Выключить мод в MODS, удалить `mods\HEMTT_Spares`, `bin\winhttp.dll` и `bin\plugins`.
