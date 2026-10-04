# Заметки по движку Terminator: Dark Fate – Defiance

Всё проверено на GOG 1.09.1221 (`logic.dll` TimeDateStamp `6AB513E0`). Адреса — RVA внутри DLL
(база образа `0x180000000`; для `.text` файловое смещение = RVA − `0xC00`). Плагины адреса не зашивают,
а ищут по шаблонам; адреса здесь — чтобы быстро найти место инструментами из `re/`.

`docs/TDF_HEMTT_Spares_Handoff.md` — исходный план (до реализации), оставлен для истории.

## Данные и слои

- `.pak` — zip-архивы (ZipCrypto, пароль — `DFD_PAK_PASSWORD`). Таблицы — Excel-XML.
- Каждый патч содержит файл **целиком**; мод тоже заменяет файл целиком.
- Порядок слоёв: `basis.pak → patch_NNN*.pak → basis/ (распакованное) → localization → mods/<мод>/basis (prio 24)
  → … → dlc → mods/<мод>/dlc/<DLC>/basis (prio 34) → локализация DLC`. Подключённые слои видны в `game.log`
  (строки `FsMounter ... Mounted`).
- **Скирмиш монтирует только основной слой**, кампания Resistance — оба. Список техники фракций скирмиша —
  `scripts/default_army.toml`.
- Локализация юнита: имя — `locale/cars.xml` (колонка `content`), статья — `encyclopedia_loc.xml` /
  `encyclopedia_dlc_loc.xml` (по `sysname`).
- Значки: `tech_pic` → `textures/ui/pictures/tech_pic/<имя>.dds`, варианты `_preselected` / `_selected` игра
  ищет сама (`game.dll`: `ui/pictures/tech_pic/{}_preselected`). Значки техники — DDS A8B8G8R8
  (маска R = `0xFF`, байты RGBA!), иконка мода — A8R8G8B8 (байты BGRA).
- Мод: `mods/<папка>/mod.json` (`name`, `description`, `icon` — путь относительно папки мода).
  Включённые моды — `Документы\My Games\Terminator Dark Fate\mods\mods.json` (у пользователя — OneDrive).

## Способности (колонка `abilities`) → компоненты (`logic.dll`)

| abilities | компонент | vtable (GOG) |
|---|---|---|
| `refuel` | `RefuellerFacet` | `0x733d20` |
| `ammunition_recharge` | `AmmoRechargerFacet` | `0x7108b0` |
| `support` | `SparesRechargerFacet` (запчасти) | `0x73d958` |
| `repair` | `RepairerFacet` (ремонт модулей, недоделан) | — |

- Сборщики описаний: машины `0x51c570`, танки `0x521ce0`. **`ammunition_recharge` не создаётся, если есть
  `refuel`/`support`/`repair`** (`mov r15b,1` после каждого; для support — `0x51ce31` / `0x52238f`).
- Общие слоты компонентов-«раздатчиков»: 10 `update(dt)`, 12 `isActive`, 13 `deactivate`, 14 `activate`.
  Флаг «выключен»: Ammo/Refueller — байт `+0x59`, Spares — `+0x51`. Вызываются только через vtable.
- Владелец компонента (машина) — `[facet+0x10]→[+0x10]`. Обход компонентов машины — `entity->vfunc(+0x58)`
  отдаёт итератор (`+0x18` конец?, `+0x30` текущий, `+0x20` следующий, `+0x00` удаляющий деструктор),
  см. `0x57490` / `0xce070`.

## Режим снабжения F4 («SUPPLY MODE»)

- Кнопка — способность `Pack` (id 7). Выдаётся в `0x12a3c6`, если есть боеприпасы или заправщик
  (AiFacet слоты 87 / 86, ответы кэшируются в `+0x1b8..+0x1bf`). Запчасти в условии не участвуют.
- Состояния ИИ `AiUnpackRechargeState` / `AiDoRechargeState` (ctor `0xbfad0`) / `AiPackRechargeState` берут
  **первый `IRechargerFacet`** машины (`0x57490`) и включают/выключают только его.
- Апдейты Ammo/Refueller (`0x120b90` / `0x35f3f0`) движок вызывает каждый кадр.
  **Апдейт запчастей `0x439e00` движок не вызывает** — его тикает только приказ по клику
  (`AiSparesRechargeUnitState::update` `0xffac0`, `jmp [facet+0x50]`).
- Поиск целей запчастей — `0x439bc0`, радиус — константа 20.0 (`0x74ff44`, общая с другим кодом!).
  `supplyRadius` (глобал `0x875f4c`) в игре тоже 20. `speedRechargeSpares` = 100/с (`invs.xml`).
- `AmmoRechargerFacet::canRecharge(target)` — слот 17 (`0x120900`); через него идут и зона, и приказ по клику.
- Круг зоны: `AmmoRechargerFacet` рисует его функцией `0x120610`, материал — из таблицы `0xff050`
  (`+0xe0` sparesRecharge, `+0x100` fuel, `+0x120` ammo; инструкция `lea r8,[rax+0x120]` в `0x120687`).
  Объект круга — `[facet+0x50]`. Ванильный `sparesRecharge.material` ссылается на безликий `circle_generic.dds`.

## Ремонт (`RepairerFacet`) — на будущее

- Создаётся выключенным (`66 C7 47 48 00 01` в `0x261b2a` / `0x261cc5`), включение (`+0x70`) не вызывает никто.
- `update` `0x363790`: импульс раз в `[+0x4c]` с, радиус `[+0x5c]` = 20 м, чинит один повреждённый модуль цели
  на `max(1, 1%)` через `DamageableModulesFacet::incHealth` (`0x225ba0`). Запчасти не тратит.
- Броня (толщина) в бою не восстанавливается ничем: её пишут только функция урона `0x310790` (износ) и
  `DamageableModulesFacet::initialize` `0x226280` (создание юнита).

## Прочее

- Lua 5.1, открыты только `base/string/table/math`; `console.log` в `game.log` не пишет. API `game.*` без ремонта.
  `game.disappear` по связкам тягач+прицеп роняет игру.
- `cheats.godMode` (`0x52dff0`) блокирует весь урон по юнитам игрока.
- Множитель урона по пехоте сложности применяется только без сетевой синхронизации — в скирмише не работает.
- Нативных плагинов игра не поддерживает: DLL-компоненты грузит `galileo.dll` по зашитым именам. Отсюда прокси
  `winhttp.dll` (её импортируют `game.dll` и `libcef.dll`; вспомогательные процессы CEF — с `--type=`).
