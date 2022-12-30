import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:get/get.dart';

import '../controller/settings_controller.dart';
import '../model/tutorial_page.dart';

class InfoPage extends StatefulWidget {
  const InfoPage({Key? key}) : super(key: key);

  @override
  State<InfoPage> createState() => _InfoPageState();
}

class _InfoPageState extends State<InfoPage> {
  final List<String> _elseTitles = [
    "Open the settings tab",
    "Type the ip address of the host, including the port if it's not 7777\n    The complete address should follow the schema ip:port",
    "Open the home page",
    "Type your username if you haven't already",
    "Select the exact version that the host is using from the dropdown menu\n    If necessary, install it using the download button",
    "As you want to play, select client from the dropdown menu",
    "Click launch to open the game",
    "Once you are in game, click PLAY to enter in-game\n    If this doesn't work open the Fortnite console by clicking the button above tab\n    If nothing happens, make sure that your keyboard locale is set to English\n    Type 'open TYPE_THE_IP' without the quotes, for example: open 85.182.12.1"
  ];
  final List<String> _ownTitles = [
    "Open the settings tab",
    "Type 127.0.0.1 as the matchmaking host",
    "Open the home page",
    "Type your username if you haven't already",
    "Select the version you want to host\n    If necessary, install it using the download button",
    "As you want to host, select Headless Server from the dropdown menu\n    If the headless server doesn't work for your version, use the normal server instead",
    "Click launch to start the server and wait until the Reboot GUI shows up",
    "To allow your friends to join your server, follow the instructions on playit.gg\n    If you are an advanced user, open port 7777 on your router\n    Finally, share your playit ip or public IPv4 address with your friends\n    If you just want to play by yourself, skip this step",
    "When you want to start the game, click on the 'Start Bus Countdown' button",
    "If you also want to play, start a client by selecting Client from the dropdown menu\n     Don't close or open again the launcher, use the same window",
    "Click launch to open the game",
    "Once you are in game, click PLAY to enter in-game\n    If this doesn't work open the Fortnite console by clicking the button above tab\n    If nothing happens, make sure that your keyboard locale is set to English\n    Type 'open TYPE_THE_IP' without the quotes, for example: open 85.182.12.1"
  ];

  final SettingsController _settingsController = Get.find<SettingsController>();
  late final ScrollController _controller;

  @override
  void initState() {
    _controller = ScrollController(initialScrollOffset: _settingsController.scrollingDistance);
    _controller.addListener(() {
      _settingsController.scrollingDistance = _controller.offset;
    });
    super.initState();
  }

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    switch(_settingsController.tutorialPage()) {
      case TutorialPage.start:
        return _createHomeScreen();
      case TutorialPage.someoneElse:
        return _createInstructions(false);
      case TutorialPage.yourOwn:
        return _createInstructions(true);
    }
  }

  SizedBox _createInstructions(bool own) {
    var titles = own ? _ownTitles : _elseTitles;
    var codeName = own ? "own" : "else";
    return SizedBox.expand(
          child: ListView.separated(
            controller: _controller,
            itemBuilder: (context, index) => Padding(
              padding: const EdgeInsets.only(
                  right: 20.0
              ),
              child: Card(
                  borderRadius: const BorderRadius.all(Radius.circular(12.0)),
                  child: ListTile(
                      title: SelectableText("${index + 1}. ${titles[index]}"),
                      subtitle: Padding(
                        padding: const EdgeInsets.only(top: 12.0),
                        child: Image.asset("assets/images/tutorial_${codeName}_${index + 1}.png"),
                      )
                  )
              ),
            ),
            separatorBuilder: (context, index) => const SizedBox(height: 8.0),
            itemCount: titles.length,
          )
      );
  }

  Widget _createHomeScreen() {
    return Row(
        mainAxisAlignment: MainAxisAlignment.spaceBetween,
        children: [
          _createCardWidget(
              text: "Play on someone else's server",
              description: "If one of your friends is hosting a game server, click here",
              onClick: () => setState(() => _settingsController.tutorialPage.value = TutorialPage.someoneElse)
          ),

          const SizedBox(
            width: 8.0,
          ),

          _createCardWidget(
              text: "Host your own server",
              description: "If you want to create your own server to invite your friends or to play around by yourself, click here",
              onClick: () => setState(() => _settingsController.tutorialPage.value = TutorialPage.yourOwn)
          )
        ]
    );
  }

  Widget _createCardWidget({required String text, required String description, required Function() onClick}) {
    return Expanded(
        child: SizedBox(
          height: double.infinity,
          child: MouseRegion(
            cursor: SystemMouseCursors.click,
            child: GestureDetector(
                onTap: onClick,
                child: Card(
                    child: Padding(
                      padding: const EdgeInsets.all(8.0),
                      child: Column(
                        mainAxisAlignment: MainAxisAlignment.center,
                        children: [
                          Text(
                            text,
                            textAlign: TextAlign.center,
                            style: const TextStyle(
                                fontSize: 20.0,
                                fontWeight: FontWeight.bold
                            ),
                          ),

                          const SizedBox(
                            height: 8.0,
                          ),

                          Text(
                              description,
                              textAlign: TextAlign.center
                          ),
                        ],
                      )
                    )
                )
            )
          )
        )
    );
  }
}