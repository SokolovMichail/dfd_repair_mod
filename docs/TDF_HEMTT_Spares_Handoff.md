# Terminator: Dark Fate – Defiance — мод «HEMTT Repair» (снабжение запчастями): итоги и план патча

Документ для следующего агента. Цель следующего этапа: **сделать так, чтобы HEMTT раздавал запчасти
по зоне в режиме снабжения (кнопка F4 «SUPPLY MODE»), как танкер — топливо и HEMTT Supply — боеприпасы.**
Сейчас пополнение запчастей работает только по клику на конкретную машину.

Дата: 2026-10-03. Всё ниже проверено на этой установке, если не помечено «гипотеза».

---

## 1. Окружение

| Что | Где / значение |
|---|---|
| Игра | `C:\Games\Terminator Dark Fate - Defiance` (GOG), версия **1.09.1221** |
| SHA-256 `bin\logic.dll` | `1216D627C7288C7DB6940168363BE582232ED4D3CB860B8B8C8D7489652ECA74` |
| SHA-256 `bin\game.dll` | `8EC30A0B59AEBF2240F00229D54A0F58E2E338F9AB3511046C9FF36970DD1489` |
| «Документы» пользователя | **OneDrive**: `C:\Users\sokol\OneDrive\Документы\My Games\Terminator Dark Fate\` (старая копия в обычных «Документах» не используется) |
| Лог игры | `...\My Games\Terminator Dark Fate\logs\game.log` (строки `FsMounter ... Mounted` показывают подключённые слои) |
| Включённые моды | `...\My Games\Terminator Dark Fate\mods\mods.json` |
| Дампы падений | `...\My Games\Terminator Dark Fate\FATAL\*.dmp` (minidump, разбирался вручную) |
| Пресеты армий скирмиша | `...\profiles\<id>\army_presets\*.preset` (обычный JSON) |
| Язык пользователя | английский (локализация — `localization\en\basis_en.pak`) |
| Инструменты | Python 3.12, установлен `capstone` (дизассемблер) |

Все адреса в документе — **RVA** внутри DLL (база образа `0x180000000`).
Для секции `.text` обеих DLL: **файловое смещение = RVA − 0xC00** (`.text`: VA 0x1000, raw 0x400).

---

## 2. Мод: что есть сейчас

```
mods/HEMTT_Repair_Test/
├── mod.json                      (icon: "basis/mod_icon.dds" — путь относительно папки мода!)
├── basis/mod_icon.dds
├── basis/scripts/default_army.toml          ← скирмиш: Fnd_HEMTT_repair разрешён Founders
├── basis/scripts/species/cars.xml           ← основной слой (скирмиш), из patch_019_dlc3.pak
└── dlc/Resistance/basis/scripts/species/
    ├── cars.xml                  ← из dlc/Resistance/patch_006_dlc2.pak
    ├── encyclopedia_dlc.xml      ← из dlc/Resistance/patch_001_dlc2.pak (+ статья)
    └── shop_presets.xml          ← из dlc/Resistance/patch_004_dlc2.pak (sector_1_reward: +Fnd_HEMTT_repair:2)
```

Новый юнит **`Fnd_HEMTT_repair`** — клон строки `Fnd_HEMTT_ammunition` (HEMTT Supply). Отличия (текущее состояние):

| Поле | HEMTT Supply | HEMTT Repair |
|---|---|---|
| `abilities` | `ammunition_recharge` | **`refuel,support`** |
| `fuel_tank_capacity` | 587 | **10000** |
| `capacity_ammo_recharger` | 10000 | **0** |
| `spares` | 600 | **10000** |
| `unit_set` | `supply` | **`light_vehicle`** |

Сборка (всё воспроизводимо):
- `mod_tools/build_hemtt_repair.sh` — пересобирает мод целиком из `mod_tools/src/` (актуальные распакованные файлы игры, сверены по CRC32 с архивами).
- `mod_tools/patch_table.py` — правка ячеек Excel-XML по имени строки/колонки, `--clone` строки, сохраняет файл байт-в-байт (CRLF, `ss:Index`, `ExpandedRowCount`).
- `mod_tools/patch_army_toml.py` — правка `default_army.toml` (`add-car:фракция:образец:новый`, `limit:фракция:N`).
- `mod_tools/re/` — скрипты разбора DLL (см. `mod_tools/re/README.md`) и сохранённые листинги.
- `mod_tools/experimental/userexec_cleanup.lua` — неудачный эксперимент (см. §5), не подключён.

Тестовый пресет скирмиша: `profiles\...\army_presets\hemtt repair test.preset` (Founders + HEMTT Repair).

---

## 3. Правила данных и слоёв (проверено)

- Таблицы — Excel-XML; `.pak` — зашифрованные zip; **каждый патч содержит файл целиком** (замена, не слияние).
  Мод тоже заменяет файл целиком → брать самую свежую версию из самого нового архива.
- Слои: `basis(pak) → patch_* → basis(loose) → localization → mods/<mod>/basis (prio 24)` … `dlc → mods/<mod>/dlc/<DLC>/basis (prio 34)`.
- **Скирмиш монтирует только основной слой** (без `dlc/Resistance`), кампания Resistance — оба.
- Список техники фракций скирмиша — `scripts/default_army.toml`. Лимит `points_limit` на ИИ **не действует** (проверено).
- Колонка `abilities` → компоненты движка (logic.dll):
  `refuel`→`RefuellerFacet`, `support`→`SparesRechargerFacet`, `repair`→`RepairerFacet`, `ammunition_recharge`→`AmmoRechargerFacet`.
  **Порядок создания компонентов фиксирован кодом**: refuel → support → repair → (build) → ammunition_recharge
  (места: `logic.dll+0x509768…` для танков, `+0x50ec41…` для машин).

---

## 4. Механика снабжения — что выяснено в logic.dll / game.dll

### 4.1 Кнопка F4 «SUPPLY MODE»
- В интерфейсе это кнопка способности **`Pack`** (id **7**; таблица имён в `game.dll` около RVA `0x4f1730`;
  разметка — `scripts/ui/AbilityButtons/pack.txt`). Подсказка: «restore fuel or ammunition».
- Выдаётся в сборщике кнопок способностей **`logic.dll+0x12a3c6`** (функция-фрагмент `0x12a078`):
  ```
  Pack доступна, если  AiFacet[84] (obj) && obj->vfunc(+0x1e8)
                     && (AiFacet[87] hasAmmoRecharger || AiFacet[86] hasRefueller)
                     && !bl   (общий запрет способностей, вычисляется в 0x10ffa0)
  ```
- Запчастей в условии **нет** (по задумке разработчиков).
- **Ловушка порядка/кэша (подтверждено опытом):** `ammunition_recharge,support` → кнопки НЕТ;
  `refuel,support` → кнопка ЕСТЬ. AiFacet кэширует ответы «есть ли X» (байты `+0x1b8..+0x1bf`);
  компонент боеприпасов создаётся последним → кэш «нет боеприпасов». С заправщиком (создаётся раньше) всё ок.

### 4.2 AiFacet — методы «есть ли / можно ли» (logic.dll, vtable AiFacet `0x709fa0`)
| Слот | Смещение | RVA | Смысл | Кэш |
|---|---|---|---|---|
| 84 | +0x2a0 | 0x252a0 | объект (развёртывание?) `[this+0x190]->[+0x10]` | — |
| 86 | +0x2b0 | 0xcd9c0 | hasRefueller (через `0xd29b0`) | +0x1b8/1b9 |
| 87 | +0x2b8 | 0xce010 | hasAmmoRecharger (через `0xd2bb0`) | +0x1ba/1bb |
| 88 | +0x2c0 | 0xce070 | hasSparesRecharger (typeid `0xc61e0`) | +0x1bc/1bd |
| 89 | +0x2c8 | 0xcda20 | hasRepairer (через `0xd2ab0`) | +0x1be/1bf |
| 90–92 | +0x2d0..+0x2e0 | 0xcdc60/0xcdca0/0xcdcf0 | canRepair / canRefuel / canAmmo(target) | — |

### 4.3 SparesRechargerFacet (компонент `support`) — **уже зонный по устройству**
- vtable `0x73d958`. Поля: `+0x50` word флагов (`+0x51` = «выключен»), `+0x54` скорость, `+0x58` аккумулятор,
  `+0x28..+0x30` список целей.
- Конструкторы (фабрика) пишут **`0x0100` → выключен**:
  - `logic.dll+0x25b963` (ф. `0x25b840`): `66 C7 47 50 00 01` — файл. смещение **0x25AD63**
  - `logic.dll+0x25bb02` (ф. `0x25b9b0`): `66 C7 47 50 00 01` — файл. смещение **0x25AF02**
  - скорость `+0x54` берётся из глобала `[0x875ed8]` (вероятно `speedRechargeSpares` из `invs.xml`).
- `update` (слот 10, `0x439e00`): если `+0x51` → выход; `acc += dt*rate`; если целая часть > 0 →
  `0x439bc0`: **поиск техники в радиусе 20 м** (жёсткая константа `[0x74ff44] = 20.0f`, файл. 0x74E944,
  с фильтром-колбэком vtable `0x74c4a0` + владелец) → пополняет запчасти найденным (с учётом `+0x28` списка).
- Слоты: 11 `0x32fe10` (byte +0x50), 12 `0x4399d0` isActive (`+0x51==0`), **13 `0x4399e0` выключить (`+0x51=1`)**,
  **14 `0x4399f0` включить (`word +0x50 = 0`)**, 15 `0x439f00` «нужно ли цели» (сравнение двух величин цели).
- По клику: ИИ-приказ `AiSparesRechargeOrder` → `AiSparesRechargeUnitState` включает компонент на время
  и **выключает по окончании**: `logic.dll+0xffce3` `call [rdx+0x68]` (`FF 52 68`, файл. **0xFF0E3**) в функции `0xffca0`.
- Отличие от заправщика: у заправщика «включить» (слот 14, `0x35f1d0`) ещё и регистрирует зону/круг
  (`[this+0x80]->vfunc(+0x258)(this+0x20, 1)`), «выключить» (слот 13, `0x35f160`) — снимает (`..., 0`).
  У запчастей включение — голая запись флага, **без регистрации зоны** (вероятно, поэтому «странный» круг).

### 4.4 Как зонный режим включает заправщик/боеприпасы — **не дотрассировано**
- Зонный режим — ИИ-состояния `AiUnpackRechargeState` / `AiDoRechargeState` / `AiPackRechargeState`
  (общий интерфейс `IRechargerFacet`, typeid-геттеры в logic `0xc4f20`, `0xc5100`).
- У всех ИИ-состояний общий метод `0x101430` (слот 7): `[[state+0x18]+0x10]->vfunc(+0x70)(owner)` —
  **гипотеза:** в `+0x18` состояния лежит один `IRechargerFacet`, и включается только он
  (первый найденный — заправщик или боеприпасы). Запчасти туда не попадают.
- `AiDoRechargeState` выключает компонент через `+0x68` в `0xbfc50` / `0xc00f0`.
- Сравнение с рабочими цепочками ещё не сделано — это главная задача для «правильного» патча.

### 4.5 RepairerFacet (`repair`) — недоделан разработчиками (для справки)
- Создаётся выключенным: `logic.dll+0x261b2a` и `+0x261cc5`, байты `66 C7 47 48 00 01`
  (файл. **0x260F2A**, **0x2610C5**). Включение (`+0x70`) **не вызывает никто**.
- `update` `0x363790`: импульс раз в `[+0x4c]` с, радиус `[+0x5c]` = 20 м, один повреждённый модуль цели
  `+max(1, 1%)` через `DamageableModulesFacet::incHealth` (`0x225ba0`), **без расхода запчастей**.
- Проверено в игре: подъезжает, разворачивается, не чинит.

### 4.6 Прочее, выясненное попутно
- Lua: Lua 5.1, открыты только `base/string/table/math` (нет io/os/debug/package); `console.log` не пишет в game.log.
  Игра вызывает `onGameStarted("…")` при старте тактической карты; `autoexec.lua` выполняет `userexec.lua`.
  API `game.*` (spawn/disappear/getAllEntities/…), нет функций ремонта/компонентов/триггеров.
  **`game.disappear` по связкам тягач+прицеп/экипажам уронил игру** (ACCESS_VIOLATION в `logic.dll+0xa30fa`).
- Читы разработчиков: параметры запуска `--cheats_godmode`, `--cheats_resources` (из `autoexec.lua`).
  godMode (`logic.dll+0x52dff0`) блокирует **весь** урон по юнитам игрока, не только гибель.
- Сложность: `damageToPlayerHumanFactor` применяется только в `HumanDamageableFacet::applyDamage`
  (`0x2d0740`, проверка `0x2d0c85`): при наличии сетевой синхронизации (`+0x70`, `net::SyncHealth`)
  множитель **не применяется** → в скирмише не работает (гипотеза: скирмиш идёт через сетевой слой).
- Сторонний трейнер `bin/DarkFateTrainer` (Gutwormy 1.0.1, через `bin/winhttp.dll`) GOG-сборку
  не поддерживает (сверка SHA-256, адреса под конкретные сборки). Рекомендовано удалить.
- Путь «добавить триггеры в карту» пользователь **отклонил** — не предлагать.

---

## 5. Варианты патча (logic.dll)

> Перед любой правкой: **резервная копия `bin\logic.dll`**, проверка SHA-256 (§1), проверка исходных
> байтов в каждом месте. Патч слетит при обновлении GOG; в мультиплеере с ним играть нельзя (рассинхрон).
> Флаги компонентов сохраняются в сейвы (`PersistentClassImpl`), проверять на **новой** миссии.

### Вариант 1 — «запчасти всегда включены» (минимальный, 3 места)
1. `0x25AD63`: `66 C7 47 50 00 01` → `66 C7 47 50 00 00`
2. `0x25AF02`: `66 C7 47 50 00 01` → `66 C7 47 50 00 00`
3. `0xFF0E3`: `FF 52 68` → `90 90 90` (чтобы клик-приказ не выключал компонент по окончании)
   — **проверить**, что результат вызова дальше не используется (функция `0xffca0`, листинг рядом).

Ожидаемо: HEMTT постоянно пополняет запчасти технике в радиусе 20 м — всегда, а не только в режиме F4,
без отрисовки зоны. Неизвестно: фильтрует ли колбэк `0x74c4a0` только своих/союзников.

### Вариант 2 — «правильно»: режим F4 включает и запчасти
Найти в `AiUnpackRechargeState`/`AiDoRechargeState`, где берётся `IRechargerFacet` и вызывается включение
(`+0x70`) / выключение (`+0x68`), и добавить такие же вызовы для `ISparesRechargerFacet`
(геттер в logic: typeid `0xc61e0`, вспомогательные `0xff590`, `0xfff40`, `0x43a0a0`).
Потребуется code cave. Плюс, по желанию, регистрация зоны как у заправщика (`[+0x80]->+0x258`).

### Вариант 3 — ремонт (если вернуться к идее ремонта)
`0x260F2A` и `0x2610C5`: `66 C7 47 48 00 01` → `66 C7 47 48 00 00` — RepairerFacet всегда включён.

---

## 6. Открытые вопросы
- Раздаётся ли **топливо** по зоне у текущего HEMTT (`refuel,support`) в режиме F4 (пользователь проверяет).
- Кто именно и где включает заправщик в режиме F4 (§4.4).
- Фильтр целей у запчастей (свои/союзники/враги).
- Название/описание юнита (локализация не сделана; нужен `locale/cars.xml` из `localization/en/basis_en.pak`).
- Уборка: распакованные папки в корне игры (`patch_005`, `patch_010_dlc`, `patch_015_dlc2_org`,
  `patch_019_dlc3`, `patch_019_dlc3_org`) и `dlc/Resistance/patch_00*_org` больше не нужны.
