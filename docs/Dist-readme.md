{IF FRAMEWORK}
SparesSupply & Repair Framework {VERSION} - for Terminator: Dark Fate - Defiance
=============================================================
{END}
{IF EXAMPLE}
HEMTT Spares {VERSION} - mod for Terminator: Dark Fate - Defiance
=============================================================
{END}

[EN]

## Description

{IF FRAMEWORK}
SparesSupply & Repair Framework gives the game two new supply mechanics that work in F4 (Supply Mode),
for any vehicle a mod gives them to:

* Spare parts zone - a deployed vehicle with spare parts (ability "support") refills the spare parts
  of every vehicle inside its zone, the way HEMTT Supply does with ammunition.
  The zone is marked with wrench icons.
* Repair zone - a deployed vehicle with the "repair" ability repairs your and allied vehicles inside
  its zone: all damaged modules and the worn armor, paying with its own spare parts
  (1 per module health point, 1 per mm of armor). The zone is marked with wrench-and-hammer icons.

This package adds NO vehicles by itself. It is for mods built on it, such as HEMTT Spares
(available as a separate download on the same page, with the framework included).
{END}
{IF EXAMPLE}
This mod adds two new supply vehicles based on the HEMTT. Both deploy with F4 (Supply Mode).
The zone mechanics come from the SparesSupply & Repair Framework, included in this package.

HEMTT Spares - resupplies SPARE PARTS to your vehicles, the way HEMTT Supply does with ammunition.
* When deployed, every vehicle inside the zone gets its spare parts refilled.
  The zone is marked with wrench icons.
* It is possible to order it to resupply a single vehicle, as with any supply truck.
* It carries 10000 spare parts. It does not hand out ammunition or fuel.
* Resistance campaign: one in the first sector shop, more in the same sector rewards
  as HEMTT Supply.
* Main campaign: one in every shop.
* Evolution campaign: one in every shop.

HEMTT Repair - a mobile repair workshop.
* When deployed, it repairs your and allied vehicles inside the zone: all damaged modules
  and the worn armor. The zone is marked with wrench-and-hammer icons.
* It pays with its own spare parts (carries 10000): 1 per module health point, 1 per mm
  of armor. The repaired vehicle pays nothing. When its stock runs out, it stops repairing.
* It does not repair itself or destroyed vehicles.
* Resistance campaign: one in the first sector shop.
* Main campaign: one in every shop.
* Evolution campaign: one in every shop.

Skirmish: both are available to Founders.
{END}


## REQUIREMENTS
* Terminator: Dark Fate - Defiance 1.09.1221 or newer, Windows 10/11 64-bit.
  Tested on the GOG version. The Steam version should work too (see CHECK below).
* Resistance DLC optional.
* Evolution DLC optional.
{IF DEFIANCE}
* DefianceLoader 1.9.0 or newer, installed in the game folder:
  https://github.com/DefianceLoader/DefianceLoader
{END}

## Installation instructions
{IF STANDALONE}
1. Extract the archive into the game folder (the one that contains "bin" and "mods").
   You should get:
   mods\SparesSupplyRepairFramework\...
{IF EXAMPLE}
   mods\HEMTT_Spares\...
{END}
   bin\winhttp.dll
   bin\plugins\SparesSupply.dll
{END}
{IF DEFIANCE}
1. Install DefianceLoader first (see its INSTALL). Then extract this archive into the game
   folder (the one that contains "bin" and "mods"). You should get:
   mods\SparesSupplyRepairFramework\...
{IF EXAMPLE}
   mods\HEMTT_Spares\...
{END}
   DefianceLoader\plugins\spares_repair.dll
   DefianceLoader\plugins\spares_repair.plugin.json
   If you had the standalone version of this mod, delete bin\winhttp.dll and
   bin\plugins\SparesSupply.dll: with both installed, the DefianceLoader plugin stays off.
{END}
{IF FRAMEWORK}
2. Start the game, open MODS and enable "SparesSupply & Repair Framework" (the zone circles),
   together with the mods that use it.
{END}
{IF EXAMPLE}
2. Start the game, open MODS and enable both "SparesSupply & Repair Framework" (the zone circles)
   and "HEMTT Spares" (the vehicles).
3. Start a new mission (or a new campaign to get the shop rewards).
   Loading a save made after a mission works too - the next shop will have the vehicles.
{END}

## CHECK
{IF STANDALONE}
After the game has started, open bin\plugins\SparesSupply.log.
The last line should be "SparesSupply active".
{END}
{IF DEFIANCE}
After the game has started, open DefianceLoader\logs\defiance-loader.log and look for the
lines of sokolov.spares-repair: "SparesSupply staged", and the plugin listed as active.
{END}
{IF FRAMEWORK}
If it says "not supported", your game build differs: vehicles built on the framework only
resupply spare parts on direct order (no F4 zone) and do not repair.
{END}
{IF EXAMPLE}
If it says "not supported", your game build differs: HEMTT Spares only resupplies
spare parts on direct order (no F4 zone) and HEMTT Repair does not repair.
{END}
Please report it together with the log.
{IF DEFIANCE}
Settings: section [sokolov.spares-repair] in DefianceLoader\config\settings.ini
("enabled = false" turns the plugin off, "log = false" keeps its events out of the log).
{END}

## WHAT THE DLLs DO
Zone resupply and repair need a small native plugin, because the game engine cannot do it out of the box.
{IF STANDALONE}
bin\winhttp.dll only forwards everything to the Windows winhttp.dll and loads
bin\plugins\*.dll. SparesSupply.dll changes the game's logic.dll IN MEMORY while the game
runs. No game file on disk is modified. Before changing anything it checks that the game
code is exactly what it expects; otherwise it changes nothing.
{END}
{IF DEFIANCE}
spares_repair.dll is a DefianceLoader plugin. It finds the places in the game's logic.dll
it needs and asks DefianceLoader to change them IN MEMORY while the game runs; the loader
checks that no other plugin changes the same places. No game file on disk is modified.
Before asking for anything it checks that the game code is exactly what it expects;
otherwise it changes nothing. If the loader's "supply radius" plugin is on, it sets the
spare parts zone radius.
{END}

## NOTES
* Single player only. Do not use it in multiplayer.
{IF DEFIANCE}
  DefianceLoader does not let the game go online while this plugin is enabled.
{END}
{IF FRAMEWORK}
* Conflicts with mods that replace materials/rounds/sparesRecharge.material.
{END}
{IF EXAMPLE}
* Conflicts with mods that replace the same game files: scripts/species/cars.xml,
  shop_presets.xml, encyclopedia.xml, encyclopedia_dlc.xml, scripts/default_army.toml,
  materials/rounds/sparesRecharge.material, locale/cars.xml, locale/encyclopedia_loc.xml,
  locale/encyclopedia_dlc_loc.xml.
{END}
{IF STANDALONE}
* Conflicts with other tools that also use bin\winhttp.dll (some trainers).
{END}
* Game updates may replace those files; then the mod needs an update too.

## KNOWN ISSUES
{IF EXAMPLE}
* Both vehicles temporarily use the model of HEMTT Supply (the ammunition truck).
{END}
* Armor repair doesn't show in real time when clicked on a unit being repaired. Reselect the unit to see the progress.
* The mouse pointer changes to yellow when selecting the modded trucks.

## UNINSTALL
{IF FRAMEWORK}
{IF STANDALONE}
Disable the mod in MODS, then delete mods\SparesSupplyRepairFramework, bin\winhttp.dll
and bin\plugins.
{END}
{IF DEFIANCE}
Disable the mod in MODS, then delete mods\SparesSupplyRepairFramework and, in
DefianceLoader\plugins, spares_repair.dll and spares_repair.plugin.json.
{END}
Mods built on the framework stop working without it.
{END}
{IF EXAMPLE}
{IF STANDALONE}
Disable both mods in MODS, then delete mods\HEMTT_Spares, mods\SparesSupplyRepairFramework,
bin\winhttp.dll and bin\plugins.
{END}
{IF DEFIANCE}
Disable both mods in MODS, then delete mods\HEMTT_Spares, mods\SparesSupplyRepairFramework and,
in DefianceLoader\plugins, spares_repair.dll and spares_repair.plugin.json.
{END}
Campaign saves that already contain HEMTT Spares or HEMTT Repair may fail to load without the mod.
{END}

## SHOUT_OUTS
Many thanks to:
- Claude, for helping me create this mod
- My family for endless support


[RU]

## Описание

{IF FRAMEWORK}
SparesSupply & Repair Framework добавляет в игру две новые механики снабжения, которые работают в режиме
F4 (режим снабжения) у любой машины, которой их даст мод:

* Зона запчастей - развёрнутая машина с запчастями (способность "support") пополняет запчасти всей
  технике в своей зоне, так же как HEMTT Supply - боеприпасы. Зона отмечена гаечными ключами.
* Зона ремонта - развёрнутая машина со способностью "repair" чинит вашу и союзную технику в своей
  зоне: все повреждённые модули и поврежденную броню, платя своими запчастями (1 за единицу
  здоровья модуля, 1 за 1 мм брони). Зона отмечена ключами и молотками.

Сам этот комплект машин НЕ добавляет. Он нужен модам, построенным на нём, например HEMTT Spares
(отдельный файл на той же странице, фреймворк в нём уже есть).
{END}
{IF EXAMPLE}
Мод добавляет две новые машины снабжения на базе HEMTT. Обе разворачиваются кнопкой F4 (режим снабжения).
Механику зон даёт SparesSupply & Repair Framework - он входит в этот комплект.

HEMTT Spares - пополняет вашей технике ЗАПЧАСТИ, так же как HEMTT Supply - боеприпасы.
* Когда развернута, пополняет запчасти всей технике в зоне. Зона отмечена гаечными ключами.
* Можно приказать ей пополнить конкретную машину, как любому транспорту снабжения.
* 10000 запчастей на борту. Боеприпасы и топливо не раздаёт.
* Кампания Resistance: одна в магазине первого сектора, ещё - в тех же наградах за секторы,
  что и HEMTT Supply.
* Основная кампания: по одной в каждом магазине.
* Кампания Evolution: по одной в каждом магазине.

HEMTT Repair - передвижная ремонтная мастерская.
* Когда развернута, чинит вашу и союзную технику в зоне: все повреждённые модули и поврежденную
  броню. Зона отмечена ключами и молотками.
* Платит своими запчастями (везёт 10000): 1 за единицу здоровья модуля и за 1 мм брони.
  У ремонтируемой машины ничего не списывается. Запас кончился - ремонт останавливается.
* Себя и уничтоженную технику не чинит.
* Кампания Resistance: одна в магазине первого сектора.
* Основная кампания: по одной в каждом магазине.
* Кампания Evolution: по одной в каждом магазине.

Скирмиш: обе доступны Основателям (Founders).
{END}


## ТРЕБОВАНИЯ
* Terminator: Dark Fate - Defiance 1.09.1221 или новее, Windows 10/11 64-bit.
  Проверено на GOG-версии. Steam-версия тоже должна работать (см. ПРОВЕРКА).
* DLC Resistance - не обязательно.
* DLC Evolution - не обязательно.
{IF DEFIANCE}
* DefianceLoader 1.9.0 или новее, установленный в папку игры:
  https://github.com/DefianceLoader/DefianceLoader
{END}

## УСТАНОВКА
{IF STANDALONE}
1. Распакуйте архив в папку игры (ту, где лежат "bin" и "mods"). Должно получиться:
   mods\SparesSupplyRepairFramework\...
{IF EXAMPLE}
   mods\HEMTT_Spares\...
{END}
   bin\winhttp.dll
   bin\plugins\SparesSupply.dll
{END}
{IF DEFIANCE}
1. Сначала установите DefianceLoader (см. его INSTALL). Затем распакуйте этот архив в папку
   игры (ту, где лежат "bin" и "mods"). Должно получиться:
   mods\SparesSupplyRepairFramework\...
{IF EXAMPLE}
   mods\HEMTT_Spares\...
{END}
   DefianceLoader\plugins\spares_repair.dll
   DefianceLoader\plugins\spares_repair.plugin.json
   Если стояла отдельная версия мода - удалите bin\winhttp.dll и bin\plugins\SparesSupply.dll:
   если стоят обе, плагин DefianceLoader не включится.
{END}
{IF FRAMEWORK}
2. Запустите игру, в меню MODS включите "SparesSupply & Repair Framework" (круги зон) вместе
   с модами, которые на нём построены.
{END}
{IF EXAMPLE}
2. Запустите игру, в меню MODS включите оба мода: "SparesSupply & Repair Framework" (круги зон)
   и "HEMTT Spares" (машины).
3. Начните новую миссию (или новую кампанию, чтобы получить машины в наградах).
   Можно и загрузить сохранение после миссии - машины появятся в следующем магазине.
{END}

## ПРОВЕРКА
{IF STANDALONE}
После запуска игры откройте bin\plugins\SparesSupply.log.
Последняя строка должна быть "SparesSupply active".
{END}
{IF DEFIANCE}
После запуска игры откройте DefianceLoader\logs\defiance-loader.log и найдите строки
sokolov.spares-repair: "SparesSupply staged", а сам плагин - в списке активных.
{END}
{IF FRAMEWORK}
Если там "not supported" - у вас другая сборка игры: машины на фреймворке пополняют запчасти
только по приказу (без зоны F4) и не чинят. Сообщите об этом и приложите лог.
{END}
{IF EXAMPLE}
Если там "not supported" - у вас другая сборка игры: HEMTT Spares пополняет запчасти
только по приказу (без зоны F4), HEMTT Repair не чинит. Сообщите об этом и приложите лог.
{END}
{IF DEFIANCE}
Настройки: раздел [sokolov.spares-repair] в DefianceLoader\config\settings.ini
("enabled = false" - выключить плагин, "log = false" - не писать его события в лог).
{END}

## ЧТО ДЕЛАЮТ DLL
Для раздачи по зоне и ремонта нужен маленький нативный плагин: по дефолту движок так не умеет.
{IF STANDALONE}
bin\winhttp.dll только пересылает вызовы в системную winhttp.dll и загружает
bin\plugins\*.dll. SparesSupply.dll меняет logic.dll игры В ПАМЯТИ, пока игра запущена;
файлы игры на диске не меняются. Перед любой правкой плагин проверяет, что код игры
в точности тот, который он ожидает; иначе ничего не трогает.
{END}
{IF DEFIANCE}
spares_repair.dll - плагин DefianceLoader. Он находит нужные места в logic.dll игры и просит
DefianceLoader изменить их В ПАМЯТИ, пока игра запущена; загрузчик следит, чтобы те же места
не менял другой плагин. Файлы игры на диске не меняются. Прежде чем что-то просить, плагин
проверяет, что код игры в точности тот, который он ожидает; иначе ничего не трогает. Если
включён плагин загрузчика "supply radius", радиус зоны запчастей задаёт он.
{END}

## ЗАМЕЧАНИЯ
* Только одиночная игра. В мультиплеере не использовать.
{IF DEFIANCE}
  DefianceLoader не пустит игру в сеть, пока этот плагин включён.
{END}
{IF FRAMEWORK}
* Конфликтует с модами, которые заменяют materials/rounds/sparesRecharge.material.
{END}
{IF EXAMPLE}
* Конфликтует с модами, которые заменяют те же файлы игры: scripts/species/cars.xml,
  shop_presets.xml, encyclopedia.xml, encyclopedia_dlc.xml, scripts/default_army.toml,
  materials/rounds/sparesRecharge.material, locale/cars.xml, locale/encyclopedia_loc.xml,
  locale/encyclopedia_dlc_loc.xml.
{END}
{IF STANDALONE}
* Конфликтует с другими программами, использующими bin\winhttp.dll (некоторые трейнеры).
{END}
* Обновление игры может заменить эти файлы - тогда понадобится обновить и мод.

## ИЗВЕСТНЫЕ ПРОБЛЕМЫ
{IF EXAMPLE}
* Обе машины пока используют модель HEMTT Supply (транспорта боеприпасов).
{END}
* Восстановление брони не отображается в реальном времени, если ремонтируемая машина выбрана.
  Чтобы увидеть прогресс, выберите её заново.
* При выборе машин мода курсор мыши становится жёлтым.

## УДАЛЕНИЕ
{IF FRAMEWORK}
{IF STANDALONE}
Выключите мод в MODS, затем удалите mods\SparesSupplyRepairFramework, bin\winhttp.dll
и папку bin\plugins.
{END}
{IF DEFIANCE}
Выключите мод в MODS, затем удалите mods\SparesSupplyRepairFramework и в DefianceLoader\plugins -
spares_repair.dll и spares_repair.plugin.json.
{END}
Моды, построенные на фреймворке, без него перестанут работать.
{END}
{IF EXAMPLE}
{IF STANDALONE}
Выключите оба мода в MODS, затем удалите mods\HEMTT_Spares, mods\SparesSupplyRepairFramework,
bin\winhttp.dll и папку bin\plugins.
{END}
{IF DEFIANCE}
Выключите оба мода в MODS, затем удалите mods\HEMTT_Spares, mods\SparesSupplyRepairFramework
и в DefianceLoader\plugins - spares_repair.dll и spares_repair.plugin.json.
{END}
Сохранения кампании, где уже есть HEMTT Spares или HEMTT Repair, без мода могут не загрузиться.
{END}

## БЛАГОДАРНОСТИ
Огромное спасибо:
- Claude - за помощь в создании мода
- Моей семье - за бесконечную поддержку
