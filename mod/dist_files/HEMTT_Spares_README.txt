HEMTT Spares {VERSION} - mod for Terminator: Dark Fate - Defiance
=============================================================

[EN]

New vehicle: HEMTT Spares - a HEMTT that resupplies SPARE PARTS to your vehicles,
the way HEMTT Supply does with ammunition.

  * Press F4 (Supply Mode) to deploy it: every vehicle inside the zone gets its spare parts
    refilled. The zone is marked with wrench icons.
  * Or order it to resupply a single vehicle, as with any supply truck.
  * It carries 10000 spare parts. It does not hand out ammunition or fuel.
  * Resistance campaign: one in the first sector shop, more in the same sector rewards
    as HEMTT Supply.
  * Skirmish: available to Founders.

REQUIREMENTS
  * Terminator: Dark Fate - Defiance 1.09.1221 or newer, Windows 10/11 64-bit.
    Tested on the GOG version. The Steam version should work too (see CHECK below).
  * Resistance DLC for the campaign part.

INSTALL
  1. Extract the archive into the game folder (the one that contains "bin" and "mods").
     You should get:
        mods\HEMTT_Spares\...
        bin\winhttp.dll
        bin\plugins\SparesSupply.dll
  2. Start the game, open MODS and enable "HEMTT Spares".
  3. Start a new mission (or a new campaign to get the shop rewards).

CHECK
  After the game has started, open bin\plugins\SparesSupply.log.
  The last line should be "SparesSupply active".
  If it says "not supported", your game build differs: the vehicle still works,
  but only resupplies spare parts on direct order (no F4 zone). Please report it
  together with the log.

WHAT THE DLLs DO
  The zone resupply needs a small native plugin, because the game engine cannot do it from data.
  bin\winhttp.dll only forwards everything to the Windows winhttp.dll and loads
  bin\plugins\*.dll. SparesSupply.dll changes the game's logic.dll IN MEMORY while the game
  runs. No game file on disk is modified. Before changing anything it checks that the game
  code is exactly what it expects; otherwise it changes nothing.

NOTES
  * Single player only. Do not use it in multiplayer.
  * Conflicts with mods that replace the same game files: scripts/species/cars.xml,
    shop_presets.xml, encyclopedia_dlc.xml, scripts/default_army.toml,
    materials/rounds/sparesRecharge.material, locale/cars.xml, locale/encyclopedia_loc.xml,
    locale/encyclopedia_dlc_loc.xml.
  * Conflicts with other tools that also use bin\winhttp.dll (some trainers).
  * Game updates may replace those files; then the mod needs an update too.

UNINSTALL
  Disable the mod in MODS, then delete mods\HEMTT_Spares, bin\winhttp.dll and bin\plugins.
  Campaign saves that already contain HEMTT Spares may fail to load without the mod.


[RU]

Новая машина: HEMTT Spares - HEMTT, который пополняет вашей технике ЗАПЧАСТИ,
так же как HEMTT Supply пополняет боеприпасы.

  * F4 (режим снабжения) - развернуть: вся техника в зоне получает запчасти.
    Зона отмечена гаечными ключами.
  * Или прикажите пополнить конкретную машину, как обычному транспорту снабжения.
  * Везёт 10000 запчастей. Боеприпасы и топливо не раздаёт.
  * Кампания Resistance: одна машина в магазине первого сектора, ещё - в тех же наградах
    за секторы, что и HEMTT Supply.
  * Скирмиш: доступен Основателям (Founders).

ТРЕБОВАНИЯ
  * Terminator: Dark Fate - Defiance 1.09.1221 или новее, Windows 10/11 64-bit.
    Проверено на GOG-версии. Steam-версия тоже должна работать (см. ПРОВЕРКА).
  * DLC Resistance - для кампании.

УСТАНОВКА
  1. Распакуйте архив в папку игры (ту, где лежат "bin" и "mods"). Должно получиться:
        mods\HEMTT_Spares\...
        bin\winhttp.dll
        bin\plugins\SparesSupply.dll
  2. Запустите игру, в меню MODS включите "HEMTT Spares".
  3. Начните новую миссию (или новую кампанию, чтобы получить машины в наградах).

ПРОВЕРКА
  После запуска игры откройте bin\plugins\SparesSupply.log.
  Последняя строка должна быть "SparesSupply active".
  Если там "not supported" - у вас другая сборка игры: машина работает, но пополняет
  запчасти только по приказу (без зоны F4). Сообщите об этом и приложите лог.

ЧТО ДЕЛАЮТ DLL
  Для раздачи по зоне нужен маленький нативный плагин: данными движок так не умеет.
  bin\winhttp.dll только пересылает вызовы в системную winhttp.dll и загружает
  bin\plugins\*.dll. SparesSupply.dll меняет logic.dll игры В ПАМЯТИ во время игры;
  файлы игры на диске не меняются. Перед любой правкой плагин проверяет, что код игры
  в точности тот, который он ожидает; иначе ничего не трогает.

ЗАМЕЧАНИЯ
  * Только одиночная игра. В мультиплеере не использовать.
  * Конфликтует с модами, которые заменяют те же файлы (список - в английском разделе выше).
  * Конфликтует с другими программами, использующими bin\winhttp.dll (некоторые трейнеры).
  * Обновление игры может заменить эти файлы - тогда понадобится обновить и мод.

УДАЛЕНИЕ
  Выключите мод в MODS, затем удалите mods\HEMTT_Spares, bin\winhttp.dll и папку bin\plugins.
  Сохранения кампании, где уже есть HEMTT Spares, без мода могут не загрузиться.
