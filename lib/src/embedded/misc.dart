import 'dart:convert';

import 'package:jaguar/http/context/context.dart';
import 'package:reboot_launcher/src/embedded/utils.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:reboot_launcher/src/util/time.dart';

const List<String> _modes = [
  "saveTheWorldUnowned",
  "battleRoyale",
  "creative",
  "saveTheWorld"
];

List<Map<String, Object>> getReceipts(Context context) => [];

Map<String, Object> getDownload(Context context) => {};

Map<String, Object?> getContentPages(Context context) =>
    _getContentPages(context);

Map<String, Object?> _getContentPages(Context context) {
  var contentPages = jsonDecode(
      loadEmbedded("responses/contentpages.json").readAsStringSync());
  try {
    var seasonBuild = parseSeasonBuild(context);
    var season = parseSeason(context);
    var language = _getLanguage(context);

    for (var mode in _modes) {
      contentPages["subgameselectdata"][mode]["message"]["title"] =
          contentPages["subgameselectdata"][mode]["message"]["title"][language];
      contentPages["subgameselectdata"][mode]["message"]["body"] =
          contentPages["subgameselectdata"][mode]["message"]["body"][language];
    }

    contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
        ["stage"] = "season$season";
    contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][1]
        ["stage"] = "season$season";

    if (season == 10) {
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
          ["stage"] = "seasonx";
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][1]
          ["stage"] = "seasonx";
    }

    if (seasonBuild == 11.31 || seasonBuild == 11.40) {
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
          ["stage"] = "Winter19";
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][1]
          ["stage"] = "Winter19";
    }

    if (seasonBuild == 19.01) {
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
          ["stage"] = "winter2021";
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
              ["backgroundimage"] =
          "https://cdn2.unrealengine.com/t-bp19-lobby-xmas-2048x1024-f85d2684b4af.png";
      contentPages["subgameinfo"]["battleroyale"]["image"] =
          "https://cdn2.unrealengine.com/19br-wf-subgame-select-512x1024-16d8bb0f218f.jpg";
      contentPages["specialoffervideo"]["bSpecialOfferEnabled"] = "true";
    }

    if (season == 20) {
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
              ["backgroundimage"] =
          "https://cdn2.unrealengine.com/t-bp20-lobby-2048x1024-d89eb522746c.png";
      if (seasonBuild == 20.40) {
        contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
                ["backgroundimage"] =
            "https://cdn2.unrealengine.com/t-bp20-40-armadillo-glowup-lobby-2048x2048-2048x2048-3b83b887cc7f.jpg";
      }
    }

    if (season == 21) {
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
              ["backgroundimage"] =
          "https://cdn2.unrealengine.com/s21-lobby-background-2048x1024-2e7112b25dc3.jpg";
      if (seasonBuild == 21.30) {
        contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
                ["backgroundimage"] =
            "https://cdn2.unrealengine.com/nss-lobbybackground-2048x1024-f74a14565061.jpg";
        contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
            ["stage"] = "season2130";
      }
    }

    if (season == 22) {
      contentPages["dynamicbackgrounds"]["backgrounds"]["backgrounds"][0]
              ["backgroundimage"] =
          "https://cdn2.unrealengine.com/t-bp22-lobby-square-2048x2048-2048x2048-e4e90c6e8018.jpg";
    }
  } catch (_) {
    // Ignored
  }

  return contentPages;
}

String _getLanguage(Context context) {
  var acceptLanguage = context.headers.value("accept-language");
  if (acceptLanguage == null) {
    return 'en';
  }

  if (acceptLanguage.contains("-") &&
      acceptLanguage != "es-419" &&
      acceptLanguage != "pt-BR") {
    return acceptLanguage.split("-")[0];
  }

  return acceptLanguage;
}

Map<String, Object?> getFriendsSettings(Context context) => {};

List<Map<String, Object?>> getFriendsBlocklist(Context context) => [];

List<Map<String, Object?>> getFriendsList(Context context) => [];

List<Map<String, Object?>> getRecentPlayers(Context context) => [];

Map<String, Object> getTimeline(Context context) {
  var build = parseSeasonBuild(context);
  var season = parseSeason(context);

  var activeEvents = [
    {
      "eventType": "EventFlag.Season$season",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    },
    {
      "eventType": "EventFlag.LobbySeason$season",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    }
  ];

  if (season == 3) {
    activeEvents.add({
      "eventType": "EventFlag.Spring2018Phase1",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    if (build >= 3.1) {
      activeEvents.add({
        "eventType": "EventFlag.Spring2018Phase2",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
    if (build >= 3.3) {
      activeEvents.add({
        "eventType": "EventFlag.Spring2018Phase3",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
    if (build >= 3.4) {
      activeEvents.add({
        "eventType": "EventFlag.Spring2018Phase4",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
  }

  if (season == 4) {
    activeEvents.add({
      "eventType": "EventFlag.Blockbuster2018",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Blockbuster2018Phase1",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    if (build >= 4.3) {
      activeEvents.add({
        "eventType": "EventFlag.Blockbuster2018Phase2",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
    if (build >= 4.4) {
      activeEvents.add({
        "eventType": "EventFlag.Blockbuster2018Phase3",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
    if (build >= 4.5) {
      activeEvents.add({
        "eventType": "EventFlag.Blockbuster2018Phase4",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
  }

  if (season == 5) {
    activeEvents.add({
      "eventType": "EventFlag.RoadTrip2018",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Horde",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Anniversary2018_BR",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_Heist",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (build == 5.10) {
    activeEvents.add({
      "eventType": "EventFlag.BirthdayBattleBus",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 6) {
    activeEvents.add({
      "eventType": "EventFlag.LTM_Fortnitemares",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_LilKevin",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    if (build >= 6.20) {
      activeEvents.add({
        "eventType": "EventFlag.Fortnitemares",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.FortnitemaresPhase1",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
    if (build >= 6.22) {
      activeEvents.add({
        "eventType": "EventFlag.FortnitemaresPhase2",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
  }

  if (build == 6.20 || build == 6.21) {
    activeEvents.add({
      "eventType": "EventFlag.LobbySeason6Halloween",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.HalloweenBattleBus",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 7) {
    activeEvents.add({
      "eventType": "EventFlag.Frostnite",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_14DaysOfFortnite",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Festivus",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_WinterDeimos",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_S7_OverTime",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 8) {
    activeEvents.add({
      "eventType": "EventFlag.Spring2019",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Spring2019.Phase1",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_Ashton",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_Goose",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_HighStakes",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_BootyBay",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    if (build >= 8.2) {
      activeEvents.add({
        "eventType": "EventFlag.Spring2019.Phase2",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
  }

  if (season == 9) {
    activeEvents.add({
      "eventType": "EventFlag.Season9.Phase1",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Anniversary2019_BR",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_14DaysOfSummer",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_Mash",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_Wax",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    if (build >= 9.2) {
      activeEvents.add({
        "eventType": "EventFlag.Season9.Phase2",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
  }

  if (season == 10) {
    activeEvents.add({
      "eventType": "EventFlag.Mayday",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Season10.Phase2",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Season10.Phase3",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_BlackMonday",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S10_Oak",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S10_Mystery",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 11) {
    activeEvents.add({
      "eventType": "EventFlag.LTE_CoinCollectXP",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Fortnitemares2019",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Galileo_Feats",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Galileo",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_WinterFest2019",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });

    if (build >= 11.2) {
      activeEvents.add({
        "eventType": "EventFlag.Starlight",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }

    if (build < 11.3) {
      activeEvents.add({
        "eventType": "EventFlag.Season11.Fortnitemares.Quests.Phase1",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.Season11.Fortnitemares.Quests.Phase2",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.Season11.Fortnitemares.Quests.Phase3",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.Season11.Fortnitemares.Quests.Phase4",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.StormKing.Landmark",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    } else {
      activeEvents.add({
        "eventType": "EventFlag.HolidayDeco",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.Season11.WinterFest.Quests.Phase1",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.Season11.WinterFest.Quests.Phase2",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.Season11.WinterFest.Quests.Phase3",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.Season11.Frostnite",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }

    // Credits to Silas for these BR Winterfest event flags
    if (build == 11.31 || build == 11.40) {
      activeEvents.add({
        "eventType": "EventFlag.Winterfest.Tree",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.LTE_WinterFest",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
      activeEvents.add({
        "eventType": "EventFlag.LTE_WinterFest2019",
        "activeUntil": "9999-01-01T00:00:00.000Z",
        "activeSince": "2020-01-01T00:00:00.000Z"
      });
    }
  }

  if (season == 12) {
    activeEvents.add({
      "eventType": "EventFlag.LTE_SpyGames",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_JerkyChallenges",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Oro",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_StormTheAgency",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 14) {
    activeEvents.add({
      "eventType": "EventFlag.LTE_Fortnitemares_2020",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 15) {
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_01",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_02",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_03",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_04",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_05",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_06",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_07",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_08",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_09",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_10",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_11",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_12",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_13",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_14",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S15_Legendary_Week_15",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_HiddenRole",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_OperationSnowdown",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_PlumRetro",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 16) {
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_01",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_02",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_03",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_04",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_05",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_06",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_07",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_08",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_09",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_10",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_11",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S16_Legendary_Week_12",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_NBA_Challenges",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_Spire_Challenges",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 17) {
    activeEvents.add({
      "eventType": "EventFlag.Event_TheMarch",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_O2_Challenges",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Buffet_PreQuests",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Buffet_Attend",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Buffet_PostQuests",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Buffet_Cosmetics",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_CosmicSummer",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_IslandGames",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_01",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_02",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_03",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_04",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_05",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_06",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_07",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_08",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_09",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_10",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_11",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_12",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_13",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Legendary_Week_14",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_CB_Radio",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Sneak_Week",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Yeet_Week",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Zap_Week",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S17_Bargain_Bin_Week",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 18) {
    activeEvents.add({
      "eventType": "EventFlag.LTE_Season18_BirthdayQuests",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_Fornitemares_2021",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_HordeRush",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly_06",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly_07",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly_08",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly_09",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly_10",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly_11",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTQ_S18_Repeatable_Weekly_12",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_SoundWave",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Season18_TextileQuests",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S18_WildWeek_Shadows",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S18_WildWeek_Bargain",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 19) {
    activeEvents.add({
      "eventType": "EventFlag.LTM_Hyena",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_Vigilante",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTM_ZebraWallet",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.LTE_Galileo_Feats",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S19_Trey",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S19_DeviceQuestsPart1",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S19_DeviceQuestsPart2",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S19_DeviceQuestsPart3",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S19_Gow_Quests",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_MonarchLevelUpPack",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S19_WinterfestCrewGrant",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S19_WildWeeks_Chicken",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S19_WildWeeks_BargainBin",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S19_WildWeeks_Spider",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S19_WildWeeks_Primal",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 20) {
    activeEvents.add({
      "eventType": "Event_S20_AliQuest",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.CovertOps_Phase1",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.CovertOps_Phase2",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.CovertOps_Phase3",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.CovertOps_Phase4",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S20_LevelUpPack",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S20_May4thQuest",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.NoBuildQuests",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S20_NoBuildQuests_TokenGrant",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S20_EmicidaQuest",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S20_WildWeeks_Bargain",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S20_WildWeeks_Chocolate",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.S20_WildWeeks_Purple",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (season == 21) {
    activeEvents.add({
      "eventType": "Event_S21_Stamina",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S21_FallFest",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S21_IslandHopper",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_S21_LevelUpPack",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "EventFlag.Event_NoSweatSummer",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S21_CRRocketQuest",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S21_GenQuest",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S21_WildWeeks_BargainBin",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S21_WildWeeks_Fire",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "Event_S21_WildWeeks_Kondor",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  if (build == 19.01) {
    activeEvents.add({
      "eventType": "EventFlag.LTE_WinterFest",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
    activeEvents.add({
      "eventType": "WF_IG_AVAIL",
      "activeUntil": "9999-01-01T00:00:00.000Z",
      "activeSince": "2020-01-01T00:00:00.000Z"
    });
  }

  return {
    "channels": {
      "client-matchmaking": {
        "states": [],
        "cacheExpire": "9999-01-01T22:28:47.830Z"
      },
      "client-events": {
        "states": [
          {
            "validFrom": "2020-01-01T20:28:47.830Z",
            "activeEvents": activeEvents,
            "state": {
              "activeStorefronts": [],
              "eventNamedWeights": {},
              "seasonNumber": season,
              "seasonTemplateId": "AthenaSeason:athenaseason$season",
              "matchXpBonusPoints": 0,
              "seasonBegin": "2020-01-01T13:00:00Z",
              "seasonEnd": "9999-01-01T14:00:00Z",
              "seasonDisplayedEnd": "9999-01-01T07:30:00Z",
              "weeklyStoreEnd": "9999-01-01T00:00:00Z",
              "stwEventStoreEnd": "9999-01-01T00:00:00.000Z",
              "stwWeeklyStoreEnd": "9999-01-01T00:00:00.000Z",
              "dailyStoreEnd": "9999-01-01T00:00:00Z"
            }
          }
        ],
        "cacheExpire": "9999-01-01T22:28:47.830Z"
      }
    },
    "eventsTimeOffsetHrs": 0,
    "cacheIntervalMins": 10,
    "currentTime": "2022-11-08T18:55:52.341Z",
  };
}

List<Map<String, Object>> getTournamentHistory(Context context) => [];

Map<String, Object> getSocialBan(Context context) => {
  "bans": [],
  "warnings": []
};

Map<String, Object> getParty(Context context) => {
  "current": [],
  "pending": [],
  "invites": [],
  "pings": []
};

Map<String, Object> getBlockedFriends(Context context) => {
  "blockedUsers": []
};

