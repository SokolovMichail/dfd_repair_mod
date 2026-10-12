HEMTT Spares {VERSION} - mod for Terminator: Dark Fate - Defiance
=============================================================

[EN]

## Description

This mod adds two new supply vehicles based on the HEMTT. Both deploy with F4 (Supply Mode).

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


## REQUIREMENTS
* Terminator: Dark Fate - Defiance 1.09.1221 or newer, Windows 10/11 64-bit.
  Tested on the GOG version. The Steam version should work too (see CHECK below).
* Resistance DLC optional.
* Evolution DLC optional.

## Installation instructions
1. Extract the archive into the game folder (the one that contains "bin" and "mods").
   You should get:
   mods\HEMTT_Spares\...
   bin\winhttp.dll
   bin\plugins\SparesSupply.dll
2. Start the game, open MODS and enable "HEMTT Spares".
3. Start a new mission (or a new campaign to get the shop rewards).
   Loading a save made after a mission works too - the next shop will have the vehicles.

## CHECK
After the game has started, open bin\plugins\SparesSupply.log.
The last line should be "SparesSupply active".
If it says "not supported", your game build differs: HEMTT Spares only resupplies
spare parts on direct order (no F4 zone) and HEMTT Repair does not repair.
Please report it together with the log.

## WHAT THE DLLs DO
Zone resupply and repair need a small native plugin, because the game engine cannot do it from data.
bin\winhttp.dll only forwards everything to the Windows winhttp.dll and loads
bin\plugins\*.dll. SparesSupply.dll changes the game's logic.dll IN MEMORY while the game
runs. No game file on disk is modified. Before changing anything it checks that the game
code is exactly what it expects; otherwise it changes nothing.

## NOTES
* Single player only. Do not use it in multiplayer.
* Conflicts with mods that replace the same game files: scripts/species/cars.xml,
  shop_presets.xml, encyclopedia.xml, encyclopedia_dlc.xml, scripts/default_army.toml,
  materials/rounds/sparesRecharge.material, locale/cars.xml, locale/encyclopedia_loc.xml,
  locale/encyclopedia_dlc_loc.xml.
* Conflicts with other tools that also use bin\winhttp.dll (some trainers).
* Game updates may replace those files; then the mod needs an update too.

## KNOWN ISSUES
* Both vehicles temporarily use the model of HEMTT Ammo transporter.
* Armor repair doesn't show in real time when clicked on a unit being repaired. Reselect the unit to see the progress.
* The mouse pointer changes to yellow when selecting the modded trucks.

## UNINSTALL
Disable the mod in MODS, then delete mods\HEMTT_Spares, bin\winhttp.dll and bin\plugins.
Campaign saves that already contain HEMTT Spares or HEMTT Repair may fail to load without the mod.

## SHOUT_OUTS
Many thanks to:
- Claude, for helping me create this mod
- My family for endless support


[RU]

## Описание

Мод добавляет две новые машины снабжения на базе HEMTT. Обе разворачиваются кнопкой F4 (режим снабжения).

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


## ТРЕБОВАНИЯ
* Terminator: Dark Fate - Defiance 1.09.1221 или новее, Windows 10/11 64-bit.
  Проверено на GOG-версии. Steam-версия тоже должна работать (см. ПРОВЕРКА).
* DLC Resistance - не обязательно.
* DLC Evolution - не обязательно.

## УСТАНОВКА
1. Распакуйте архив в папку игры (ту, где лежат "bin" и "mods"). Должно получиться:
   mods\HEMTT_Spares\...
   bin\winhttp.dll
   bin\plugins\SparesSupply.dll
2. Запустите игру, в меню MODS включите "HEMTT Spares".
3. Начните новую миссию (или новую кампанию, чтобы получить машины в наградах).
   Можно и загрузить сохранение после миссии - машины появятся в следующем магазине.

## ПРОВЕРКА
После запуска игры откройте bin\plugins\SparesSupply.log.
Последняя строка должна быть "SparesSupply active".
Если там "not supported" - у вас другая сборка игры: HEMTT Spares пополняет запчасти
только по приказу (без зоны F4), HEMTT Repair не чинит. Сообщите об этом и приложите лог.

## ЧТО ДЕЛАЮТ DLL
Для раздачи по зоне и ремонта нужен маленький нативный плагин: по дефолту движок так не умеет.
bin\winhttp.dll только пересылает вызовы в системную winhttp.dll и загружает
bin\plugins\*.dll. SparesSupply.dll меняет logic.dll игры В ПАМЯТИ, пока игра запущена;
файлы игры на диске не меняются. Перед любой правкой плагин проверяет, что код игры
в точности тот, который он ожидает; иначе ничего не трогает.

## ЗАМЕЧАНИЯ
* Только одиночная игра. В мультиплеере не использовать.
* Конфликтует с модами, которые заменяют те же файлы игры: scripts/species/cars.xml,
  shop_presets.xml, encyclopedia.xml, encyclopedia_dlc.xml, scripts/default_army.toml,
  materials/rounds/sparesRecharge.material, locale/cars.xml, locale/encyclopedia_loc.xml,
  locale/encyclopedia_dlc_loc.xml.
* Конфликтует с другими программами, использующими bin\winhttp.dll (некоторые трейнеры).
* Обновление игры может заменить эти файлы - тогда понадобится обновить и мод.

## ИЗВЕСТНЫЕ ПРОБЛЕМЫ
* Обе машины пока используют модель HEMTT Supply (транспорта боеприпасов).
* Восстановление брони не отображается в реальном времени, если ремонтируемая машина выбрана.
  Чтобы увидеть прогресс, выберите её заново.
* При выборе машин мода курсор мыши становится жёлтым.

## УДАЛЕНИЕ
Выключите мод в MODS, затем удалите mods\HEMTT_Spares, bin\winhttp.dll и папку bin\plugins.
Сохранения кампании, где уже есть HEMTT Spares или HEMTT Repair, без мода могут не загрузиться.

## БЛАГОДАРНОСТИ
Огромное спасибо:
- Claude - за помощь в создании мода
- Моей семье - за бесконечную поддержку
