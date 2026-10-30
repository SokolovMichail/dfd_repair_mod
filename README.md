# DFDRepairMod — моды для Terminator: Dark Fate – Defiance

Проект целиком: нативные плагины (C++), сборка данных модов (Python), графика, упаковка релиза
и инструменты разбора движка. Результат:

- **SparesSupply & Repair Framework** — фреймворк: механики зоны запчастей и зоны ремонта в режиме F4 для любой
  машины, которой мод даст нужные способности;
- **HEMTT Spares** — мод-пример на фреймворке с двумя машинами снабжения: HEMTT Spares (запчасти) и
  HEMTT Repair (ремонт и броня, пока тестовая).

| Документ | О чём |
|---|---|
| [`INSTALL.md`](INSTALL.md) | установка, проверка, удаление (RU/EN) — для игроков |
| [`docs/MODDING.md`](docs/MODDING.md) | как сделать свои машины на фреймворке (EN) — для моддеров; идёт во все комплекты |
| [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md) | как устроен мод: данные, загрузчик, плагин, сборка |
| [`docs/engine_notes.md`](docs/engine_notes.md) | что известно о движке (адреса, поля, слоты) |
| [`re/README.md`](re/README.md) | инструменты разбора DLL игры |

## Машины

Обе — копии HEMTT Supply (та же 3D-модель), разворачиваются кнопкой F4 (режим снабжения) и работают по зоне.
Патроны не раздают. Основная кампания и DLC Canada: обе — во всех магазинах (по одной). Скирмиш: Founders. Кампания Resistance — см. ниже.

| Машина | sysname | Что делает | Круг зоны | Кампания |
|---|---|---|---|---|
| **HEMTT Spares** | `Fnd_HEMTT_spares` | пополняет запчасти всем машинам в зоне; или приказ на конкретную машину | гаечные ключи | 1 шт. в магазине первого сектора + в тех же наградах, что HEMTT Supply |
| **HEMTT Repair** (тест) | `Fnd_HEMTT_repairer` | раз в 0,5 с чинит своим и союзным машинам в зоне все повреждённые модули (+2% максимума) и восстанавливает броню (+2% исходной толщины каждого слоя); себя и уничтоженных не чинит; платит своими запчастями (10000): 1 за единицу здоровья, 1 за 1 мм брони, у цели ничего не списывает | ключ и молоток | 1 шт. в магазине первого сектора |

Три части; первые две — фреймворк, третья — пример:

- **мини-мод фреймворка** — `mods/SparesSupplyRepairFramework`: только материалы и текстуры кругов зоны
  (ключи; ключ и молоток) и иконка мода. Таблиц игры в нём нет, поэтому он ни с чем не конфликтует;
- **данные примера** — `mods/HEMTT_Spares`: обе машины (копии `Fnd_HEMTT_ammunition`; Spares —
  `abilities=ammunition_recharge,support`, запчасти 10000; Repair — `abilities=ammunition_recharge,repair`;
  обе — патроны 500, лёгкая техника), магазин, армия скирмиша, локализация (en), значки, иконка мода;
- **плагин фреймворка** — машин мода он не знает: поведение включают способности машины в таблице. Без него
  движок так не умеет (подробно — в начале
  [`logic_dll/common.h`](plugins/spares_supply/logic_dll/common.h), [`logic_dll/spares.h`](plugins/spares_supply/logic_dll/spares.h),
  [`logic_dll/repair.h`](plugins/spares_supply/logic_dll/repair.h) и в
  [`docs/ARCHITECTURE.md`](docs/ARCHITECTURE.md)):
  разрешает `ammunition_recharge` рядом с `support` / `repair` (кнопка F4), в режиме F4 тикает компонент
  запчастей (Spares) или сам чинит модули и броню машинам в зоне (Repair — движковый ремонт недоделан),
  запрещает таким машинам раздавать патроны, рисует круги материалами мини-мода.

Релиз — четыре комплекта на одной странице мода: фреймворк отдельно или вместе с HEMTT Spares, каждый — в
отдельной версии (наш `winhttp.dll`) или под DefianceLoader (см. «Цели сборки»).

Проверено на GOG 1.09.1221. Адреса в `logic.dll` не зашиты — плагин ищет их по байтовым шаблонам и RTTI,
поэтому должен работать и на Steam. Не нашлось/не совпало хоть что-то — ничего не меняет и пишет в лог.
Только одиночная игра.

## Структура

```
CMakeLists.txt         цели сборки (см. ниже), версия мода MOD_VERSION
.env.example           шаблон локальных настроек -> скопировать в .env
.run/                  готовые конфигурации запуска CLion: Build release / Deploy to game / Build mod data,
                       Build release (DefianceLoader) / Deploy to game (DefianceLoader),
                       Build SparesSupply (standalone) / Build spares_repair (DefianceLoader)
common/patch.h         общее для плагинов: лог, поиск по шаблонам и RTTI, набор правок, ожидание загрузки DLL
loader/                winhttp.dll: прокси системной winhttp.dll + загрузчик bin\plugins\*.dll
plugins/spares_supply/ SparesSupply.dll (отдельно) и spares_repair.dll (плагин DefianceLoader) - из одних исходников:
  core/                правила мода без привязки к игре: supply.* (режим F4), spares.* (HEMTT Spares),
                       repair.* (HEMTT Repair); с игрой говорят только через интерфейсы-порты
  logic_dll/           привязка к нынешней logic.dll: common.* (модель объектов, перехваты боеприпасов,
                       установка правок), spares.*, repair.* (перехваты и реализация портов);
                       standalone.cpp - точка входа SparesSupply.dll, defiance.cpp - точка входа spares_repair.dll
  spares_repair.plugin.json.in  манифест плагина DefianceLoader (CMake подставляет версию)
third_party/defiance/  defiance.h - C-заголовок API DefianceLoader (v1.9.0, MIT)
tools/                 gen_winhttp_proxy.py - генерация переходников winhttp (нужен редко)
mod/
  build_mod.py         сборка обоих модов из актуальных файлов игры: мини-мод фреймворка и HEMTT Spares
                       (что и откуда - в начале файла)
  package.py           комплекты релиза + обложка (README для игроков - docs/Dist-readme.md)
  art/                 графика: круги (make_circle_spares, make_circle_repair), значки машин
                       (make_unit_icons --kind spares|repair), иконки модов (make_mod_icon [--framework])
  tools/               gamefiles.py (поиск актуальной версии файла в слоях игры), patch_table.py (правка
                       Excel-XML таблиц), patch_army_toml.py, pak_extract.py (ручная распаковка), table_grep.py
re/                    инструменты разбора DLL игры (см. re/README.md)
tests/                 тесты плагина (doctest + CTest), игра для них не нужна - см. «Тесты»
docs/                  engine_notes.md - что известно о движке; Dist-readme.md - README для игроков в архиве;
                       MODDING.md - руководство для моддеров (в архиве); исходный план HEMTT Spares
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
| `spares_repair` | Тот же плагин для DefianceLoader: `spares_repair.dll` + `spares_repair.plugin.json` в папке сборки. |
| `mod` | Собирает оба мода в `<build>/stage/mods/`: мини-мод фреймворка `SparesSupplyRepairFramework` и пример `HEMTT_Spares`. Файлы игры каждый раз берутся заново — после обновления игры достаточно пересобрать. |
| `deploy` | Ставит в игру: `bin\winhttp.dll`, `bin\plugins\SparesSupply.dll`, `mods\SparesSupplyRepairFramework`, `mods\HEMTT_Spares`. Убирает плагин DefianceLoader-версии (`DefianceLoader\plugins\spares_repair.*`), если он там был. Игра должна быть закрыта; оба мода нужно один раз включить в меню MODS. |
| `deploy_defiance` | То же для DefianceLoader: `DefianceLoader\plugins\spares_repair.dll` + манифест, оба мода; убирает `bin\plugins\SparesSupply.dll` (`bin\winhttp.dll`, сам DefianceLoader и чужие плагины остаются). Так `deploy` / `deploy_defiance` переключают игру между двумя версиями мода — удобно для проверки (см. «DefianceLoader»). |
| `release` | **Собрать релиз**: отдельная Release-сборка DLL (оптимизация, без отладочной информации) + оба мода + два комплекта отдельной версии: `dist/SparesSupplyRepairFramework_v<версия>.zip` (только фреймворк: DLL и мини-мод) и `dist/HEMTT_Spares_v<версия>.zip` (фреймворк + HEMTT Spares), а также `dist/HEMTT_Spares_v<версия>_cover.png` (1920×1080) и `_banner.png` (1300×372). |
| `release_defiance` | То же для DefianceLoader: `dist/SparesSupplyRepairFramework_DefianceLoader_v<версия>.zip` и `dist/HEMTT_Spares_DefianceLoader_v<версия>.zip` (`DefianceLoader/plugins/spares_repair.dll` + манифест вместо `bin/...`); обложка и превью — общие с `release`. Упаковка — `mod/package.py [--defiance] [--mod ...]`; README игрока — один `docs/Dist-readme.md`, блоки `{IF STANDALONE}` / `{IF DEFIANCE}` и `{IF FRAMEWORK}` / `{IF EXAMPLE}` (могут быть вложены) попадают только в свой комплект; `docs/MODDING.md` идёт во все. |

В CLion: выбрать конфигурацию **Build release** (или **Deploy to game**; для DefianceLoader — те же с пометкой
**(DefianceLoader)**) и нажать Run — либо Build → Build Target.
Из консоли:

```bash
cmake --build cmake-build-debug --target release
```

Версия — `MOD_VERSION` в `CMakeLists.txt`: попадает в имя архива, описание мода и лог плагина.

## Тесты

Тесты закрепляют поведение плагина `SparesSupply.dll` и общего `common/patch.h`, чтобы рефакторинг ничего
не сломал незаметно. Игра для них не нужна: вместо `logic.dll` и объектов движка — подделки
(`tests/support/`), построенные по тем же предположениям, что зашиты в плагин. Поэтому обновление игры
тесты не ломает.

| Программа | Что проверяет |
|---|---|
| `patch_h_test` | поиск по шаблонам и RTTI, набор правок «всё или ничего», лог, ожидание загрузки DLL |
| `plugin_test` | режим F4 у HEMTT Spares / Repair / Supply, круг зоны, ремонт модулей и брони, оплата запчастями |
| `plugin_install_test` | поиск адресов и установка правок в поддельной `logic.dll`; при любом несовпадении — не меняется ничего |
| `plugin_defiance_test` | плагин DefianceLoader на поддельном загрузчике: просит ровно те же правки, сам не пишет, контракт совпадает, отказ при несовпадении, обе DLL сразу, `supply-radius`, настройка `log` |
| `core_test` | правила ядра (`core/`) на простых тестовых портах, без подделки движка; ядро собирается без `windows.h` |

Тесты собираются вместе с остальным (`-DDFD_TESTS=OFF` — отключить). В CLion — конфигурация **All CTest**; из
консоли:

```bash
cmake --build cmake-build-debug && ctest --test-dir cmake-build-debug --output-on-failure
```

Пометка `QUIRK:` в тестах — текущее поведение, которое выглядит странно; тест фиксирует его, чтобы
оно не изменилось случайно.

## Проверка в игре

`bin\plugins\loader.log` — `loaded SparesSupply.dll`;
`bin\plugins\SparesSupply.log` — `SparesSupply <версия> loaded` … `SparesSupply active`; во время работы
HEMTT Repair — строки `repair: target=… +N hp, M armor layers`.
Если там `not supported` — у игры другой код: смотреть, какой шаблон не нашёлся, и обновлять его через
`re/mksig.py` (см. `docs/engine_notes.md`).

С DefianceLoader — `DefianceLoader\logs\defiance-loader.log`, строки `sokolov.spares-repair`:
`SparesSupply <версия> as a DefianceLoader plugin` … `SparesSupply staged`, затем состояние плагина `active`.

## DefianceLoader

Мод работает и сам (наш `winhttp.dll`), и как плагин [DefianceLoader](https://github.com/DefianceLoader/DefianceLoader)
(`bin\dxgi.dll`). Правила и перехваты одни и те же; плагин просто отдаёт свои правки загрузчику, а тот
следит, чтобы плагины не писали в одно место (подробно — `docs/ARCHITECTURE.md`, 5.7).

Установка с DefianceLoader: `spares_repair.dll` и `spares_repair.plugin.json` — в `DefianceLoader\plugins\`,
моды — в `mods\SparesSupplyRepairFramework` и `mods\HEMTT_Spares` (цель `deploy_defiance`). `bin\winhttp.dll` и `bin\plugins\SparesSupply.dll` при
этом **не нужны** — если они остались, работает отдельная DLL, а плагин загрузчика отказывается и пишет в лог,
что удалить. Нужен плагин загрузчика `defiance.core` (идёт с ним). Только одиночная игра: загрузчик не пустит
в сетевую игру, пока плагин включён. Выключить — `enabled = false` в разделе `[sokolov.spares-repair]` файла
`DefianceLoader\config\settings.ini`; `log = false` — без строк событий в логе.

Если включён плагин загрузчика `defiance.supply-radius`, радиус зоны запчастей задаёт он (правку радиуса мы
пропускаем).

**Куда пишет плагин** (всё — `logic.dll`, байтовые правки `patch_bytes`; RVA — GOG 2026-09-25, на других
сборках адреса другие, плагин находит их при запуске):

| Место | Правка | RVA (GOG 2026-09-25) |
| --- | --- | --- |
| сборщик описаний машин: флаг после `support` | `mov r15b,1` → `nop` (3 байта) | 0x51ce31 |
| сборщик описаний танков: флаг после `support` | то же | 0x52238f |
| сборщик описаний машин: флаг после `repair` | то же | 0x51ced2 |
| сборщик описаний танков: флаг после `repair` | то же | 0x522432 |
| поиск целей запчастей: `movss xmm2, [20.0]` | → `movss xmm2, [supplyRadius]` (8 байт; пропускается при `defiance.supply-radius`) | 0x439c67 |
| `AmmoRechargerFacet` слоты 10 / 13 / 14 / 17 (`update`, `deactivate`, `activate`, `canRecharge`) | слот vtable, 8 байт | 0x710900, 0x710918, 0x710920, 0x710938 |
| `RefuellerFacet` слоты 10 / 13 / 14 | слот vtable | 0x733d70, 0x733d88, 0x733d90 |
| `SparesRechargerFacet` слоты 10 / 13 | слот vtable | 0x73d9a8, 0x73d9c0 |
| `RepairerFacet` слоты 10 / 13 / 14 | слот vtable | 0x734028, 0x734040, 0x734048 |
| функция круга `AmmoRechargerFacet`: `lea r8,[rax+0x120]` (материал) | заявлена без изменений; плагин меняет её сам на время перерисовки круга (7 байт) | 0x120687 |

## Новый плагин

1. `plugins/<имя>/<имя>.cpp`, подключить `common/patch.h`.
2. В `DllMain` — открыть лог и `DllWatcher::watch(L"logic.dll", cb)`. В `cb`: `dfd::Image img(base)` →
   найти адреса (`img.find(шаблон)`, `img.vtableOf("Класс@Leonardo")`) → `PatchSet` (`bytes` / `vslot`
   с шаблоном начала функции) → `apply`.
3. В `CMakeLists.txt`: `add_library(<имя> SHARED ...)`, `dfd_dll(<имя>)`, копирование в `deploy` и в `package.py`.

`DllMain` плагина выполняется под loader lock (его вызывает `DllMain` загрузчика): никаких `LoadLibrary` и
ожиданий других потоков. Колбэк `DllWatcher` вызывается после отображения `logic.dll`, до её кода.

## Удаление из игры

Выключить оба мода в MODS, удалить `mods\HEMTT_Spares`, `mods\SparesSupplyRepairFramework`, `bin\winhttp.dll` и
`bin\plugins`. С DefianceLoader — вместо двух последних удалить `DefianceLoader\plugins\spares_repair.dll` и
`spares_repair.plugin.json`.
