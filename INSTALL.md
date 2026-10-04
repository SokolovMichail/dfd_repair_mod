# HEMTT Spares — установка и удаление

[Русский](#русский) · [English](#english)

---

## Русский

### Что это

Новая машина **HEMTT Spares** — HEMTT, который пополняет технике запчасти, как HEMTT Supply — боеприпасы.
Нажмите **F4** (режим снабжения): вся техника в зоне (круг из гаечных ключей) получает запчасти. Или прикажите
пополнить конкретную машину. Кампания Resistance: одна машина в магазине первого сектора, ещё — в тех же наградах,
что и HEMTT Supply. Скирмиш: Основатели (Founders).

### Требования

- Terminator: Dark Fate – Defiance **1.09.1221** или новее, Windows 10/11 64-bit.
  Проверено на GOG-версии; Steam-версия должна работать (см. «Проверка»).
- DLC Resistance — для кампании. Скирмиш работает и без него.

### Установка

1. Скачайте `HEMTT_Spares_v<версия>.zip`.
2. Распакуйте архив **в папку игры** — ту, где лежат папки `bin` и `mods`
   (GOG по умолчанию: `C:\Games\Terminator Dark Fate - Defiance`;
   Steam: ПКМ по игре → Управление → Просмотреть локальные файлы). Должно получиться:
   ```
   <папка игры>\mods\HEMTT_Spares\...
   <папка игры>\bin\winhttp.dll
   <папка игры>\bin\plugins\SparesSupply.dll
   ```
3. Запустите игру → меню **MODS** → включите **HEMTT Spares**.
4. Начните новую миссию. Чтобы получить машину в магазине и наградах кампании, нужна новая кампания Resistance.

### Проверка

После запуска игры откройте `<папка игры>\bin\plugins\SparesSupply.log`. Последняя строка должна быть
**`SparesSupply active`**.

| В логе | Что значит |
|---|---|
| `SparesSupply active` | всё работает |
| `... not supported - nothing patched` | у вас другая сборка игры: плагин ничего не менял. Машина есть, но пополняет запчасти только по приказу, без кнопки F4. Сообщите автору и приложите лог |
| файла нет | не загрузился `bin\winhttp.dll`: проверьте, что файлы лежат именно в `bin\` и `bin\plugins\`; посмотрите `bin\plugins\loader.log` |

### Что делают DLL

Раздача по зоне требует небольшого нативного плагина — одними данными движок так не умеет.
`bin\winhttp.dll` только пересылает вызовы в системную `winhttp.dll` Windows и загружает `bin\plugins\*.dll`.
`SparesSupply.dll` меняет `logic.dll` игры **в памяти**, пока игра запущена; файлы игры на диске не меняются.
Перед любой правкой плагин проверяет, что код игры в точности такой, какой он ожидает, иначе ничего не трогает.

### Важно

- **Только одиночная игра.** В мультиплеере не использовать — будет рассинхрон.
- Конфликтует с модами, которые заменяют те же файлы игры:
  `scripts/species/cars.xml`, `shop_presets.xml`, `encyclopedia_dlc.xml`, `scripts/default_army.toml`,
  `materials/rounds/sparesRecharge.material`, `locale/cars.xml`, `locale/encyclopedia_loc.xml`,
  `locale/encyclopedia_dlc_loc.xml`. Из нескольких таких модов в игре окажется файл только одного
  (по приоритету в меню MODS) — машина или чужие правки пропадут.
- Конфликтует с другими программами, которые тоже используют `bin\winhttp.dll` (некоторые трейнеры).
- Обновление игры может заменить эти файлы — тогда понадобится новая версия мода.
- Антивирус может насторожиться на подмену `winhttp.dll` — это ожидаемо для такого способа подключения.

### Удаление

1. В меню **MODS** выключите HEMTT Spares (желательно — не находясь в кампании, где он используется).
2. Удалите:
   ```
   <папка игры>\mods\HEMTT_Spares
   <папка игры>\bin\winhttp.dll
   <папка игры>\bin\plugins
   ```
   Если в `bin\plugins` есть плагины других модов — удалите только `SparesSupply.dll` и его `.log`,
   а `winhttp.dll` оставьте.

Сохранения кампании, в которых уже есть HEMTT Spares, без мода могут не загрузиться — в машине используется
юнит, которого нет в обычной игре. Перед удалением лучше закончить или начать новую кампанию.

### Для разработки

Установить из исходников в свою игру: цель CMake **`deploy`** (в CLion — конфигурация **Deploy to game**).
Она ставит DLL и мод в `GAME_DIR`; мод нужно один раз включить в MODS. Подробности — [`README.md`](README.md).

---

## English

### What it is

New vehicle **HEMTT Spares** — a HEMTT that resupplies spare parts to your vehicles, the way HEMTT Supply does
with ammunition. Press **F4** (Supply Mode): every vehicle inside the zone (marked with wrench icons) gets its
spare parts refilled. Or order it to resupply a single vehicle. Resistance campaign: one in the first sector shop,
more in the same sector rewards as HEMTT Supply. Skirmish: Founders.

### Requirements

- Terminator: Dark Fate – Defiance **1.09.1221** or newer, Windows 10/11 64-bit.
  Tested on GOG; the Steam version should work too (see "Check").
- Resistance DLC for the campaign. Skirmish works without it.

### Install

1. Download `HEMTT_Spares_v<version>.zip`.
2. Extract it **into the game folder** — the one containing `bin` and `mods`
   (GOG default: `C:\Games\Terminator Dark Fate - Defiance`;
   Steam: right-click the game → Manage → Browse local files). You should get:
   ```
   <game>\mods\HEMTT_Spares\...
   <game>\bin\winhttp.dll
   <game>\bin\plugins\SparesSupply.dll
   ```
3. Start the game → **MODS** → enable **HEMTT Spares**.
4. Start a new mission. For the shop and sector rewards, start a new Resistance campaign.

### Check

After the game has started, open `<game>\bin\plugins\SparesSupply.log`. The last line should be
**`SparesSupply active`**.

| Log says | Meaning |
|---|---|
| `SparesSupply active` | everything works |
| `... not supported - nothing patched` | different game build: the plugin changed nothing. The vehicle exists but only resupplies on direct order (no F4). Please report it with the log |
| no log file | `bin\winhttp.dll` was not loaded: make sure the files are in `bin\` and `bin\plugins\`; see `bin\plugins\loader.log` |

### What the DLLs do

Zone resupply needs a small native plugin — the engine cannot do it from data alone.
`bin\winhttp.dll` only forwards calls to the Windows `winhttp.dll` and loads `bin\plugins\*.dll`.
`SparesSupply.dll` changes the game's `logic.dll` **in memory** while the game runs; no game file on disk is
modified. Before changing anything it verifies that the game code is exactly what it expects; otherwise it
changes nothing.

### Important

- **Single player only.** Do not use it in multiplayer (desync).
- Conflicts with mods that replace the same game files: `scripts/species/cars.xml`, `shop_presets.xml`,
  `encyclopedia_dlc.xml`, `scripts/default_army.toml`, `materials/rounds/sparesRecharge.material`,
  `locale/cars.xml`, `locale/encyclopedia_loc.xml`, `locale/encyclopedia_dlc_loc.xml`.
- Conflicts with other tools that also use `bin\winhttp.dll` (some trainers).
- A game update may replace those files; a new mod version will then be needed.
- An antivirus may flag the `winhttp.dll` replacement — expected for this loading technique.

### Uninstall

1. Disable HEMTT Spares in **MODS**.
2. Delete `<game>\mods\HEMTT_Spares`, `<game>\bin\winhttp.dll` and `<game>\bin\plugins`
   (if other mods' plugins are in `bin\plugins`, delete only `SparesSupply.dll` and its log and keep `winhttp.dll`).

Campaign saves that already contain HEMTT Spares may fail to load without the mod. Finish the campaign or start
a new one before uninstalling.
