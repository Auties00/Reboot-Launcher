import 'dart:async';

import 'package:fluent_ui/fluent_ui.dart' hide FluentIcons;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/page/abstract/page.dart';
import 'package:reboot_launcher/src/page/abstract/page_type.dart';
import 'package:reboot_launcher/src/page/pages.dart';
import 'package:reboot_launcher/src/util/translations.dart';
import 'package:reboot_launcher/src/widget/info_tile.dart';

class InfoPage extends RebootPage {
  const InfoPage({Key? key}) : super(key: key);

  @override
  RebootPageState<InfoPage> createState() => _InfoPageState();

  @override
  String get name => translations.infoName;

  @override
  String get iconAsset => "assets/images/info.png";

  @override
  bool hasButton(String? routeName) => false;

  @override
  RebootPageType get type => RebootPageType.info;
}

class _InfoPageState extends RebootPageState<InfoPage> {
  final SettingsController _settingsController = Get.find<SettingsController>();
  RxInt _counter = RxInt(180);

  static final List<InfoTile> _infoTiles = [
      InfoTile(
          title: Text("What is Project Reboot?"),
          content: Text(
              "Project Reboot is a game server for Fortnite that aims to support as many seasons as possible.\n"
                  "The project was started on Discord by Milxnor, while the launcher is developed by Auties00.\n"
                  "Both are open source on GitHub, anyone can easily contribute or audit the code!"
          )
      ),
      InfoTile(
          title: Text("What is a Fortnite game server?"),
          content: Text(
              "If you have ever played Minecraft multiplayer, you might know that the servers you join are hosted on a computer running a program, called Minecraft Game Server.\n"
                  "While the Minecraft Game server is written by the creators of Minecraft, Mojang, Epic Games doesn't provide an equivalent for Fortnite.\n"
                  "By exploiting the Fortnite internals, though, it's possible to create a game server just like in Minecraft: this is in easy terms what Project Reboot does.\n"
                  "Some Fortnite versions support running this game server in the background without rendering the game(\"headless\"), while others still require the full game to be open.\n"
                  "Just like in Minecraft, you need a game client to play the game and one to host the server.\n"
                  "By default, a game server is automatically started on your PC when you start a Fortnite version from the \"Play\" section in the launcher.\n"
                  "If you want to play in another way, for example by joining a server hosted by one of your friends instead of running one yourself, you can checkout the \"Multiplayer\" section in the \"Play\" tab of the launcher."
          )
      ),
      InfoTile(
          title: Text("Types of Fortnite game server"),
          content: Text(
              "Some Fortnite versions support running this game server in the background without rendering the game: this type of server is called \"headless\" as the game is running, but you can't see it on your screen.\n"
                  "If headless is not supported by the Fortnite version you want to play, or if you disabled it manually from the \"Configuration\" section in the \"Host\" tab of the launcher, you will see an instance of Fortnite open on your screen.\n"
                  "For convenience, this window will be opened on a new Virtual Desktop, if your Windows version supports it. This feature can be disabled as well from from the \"Configuration\" section in the \"Host\" tab of the launcher."
                  "Just like in Minecraft, you need a game client to play the game and one to host the server."
          )
      ),
      InfoTile(
          title: Text("How can others join my game server?"),
          content: Text(
              "For others to join your game server, port 7777 must be accessible on your PC.\n"
                  "One option is to use a private VPN service like Hamachi or Radmin, but all of the players will need to download this software.\n"
                  "The best solution is to use port forwarding:\n"
                  "1. Set a static IP\n"
                  "    If you don't have already a static IP set, set one by following any tutorial on Google\n"
                  "2. Log into your router's admin panel\n"
                  "    Usually this can be accessed on any web browser by going to http://192.168.1.1/\n"
                  "    You might need a username and a password to log in: refer to your router's manual for precise instructions\n"
                  "3. Find the port forwarding section\n"
                  "    Once logged in into the admin panel, navigate to the port forwarding section of your router's settings\n"
                  "    This location may vary from router to router, but it's typically labelled as \"Port Forwarding,\" \"Port Mapping\" or \"Virtual Server\"\n"
                  "    Refer to your router's manual for precise instructions\n"
                  "4. Add a port forwarding rule\n"
                  "    Now, you'll need to create a new port forwarding rule. Here's what you'll typically need to specify:\n"
                  "    - Service Name: Choose a name for your port forwarding rule (e.g., \"Fortnite Game Server\")\n"
                  "    - Port Number: Enter 7777 for both the external and internal ports\n"
                  "    - Protocol: Select the UDP protocol\n"
                  "    - Internal IP Address: Enter the static IP address you set earlier\n"
                  "    - Enable: Make sure the port forwarding rule is enabled\n"
                  "5. Save and apply the changes\n"
                  "    After configuring the port forwarding rule, save your changes and apply them\n"
                  "    This step may involve clicking a \"Save\" or \"Apply\" button on your router's web interface"
          )
      ),
      InfoTile(
          title: Text("What is a backend?"),
          content: Text(
              "A backend is a piece of software that emulates the Epic Games server responsible for authentication and related features.\n"
                  "By default, the Reboot Launcher ships with a slightly customized version of LawinV1, an open source implementation available on Github.\n"
                  "If you are having any problems with the built in backend, enable the \"Detached\" option in the \"Backend\" tab of the Reboot Laucher to troubleshoot the issue."
                  "LawinV1 was chosen to allow users to log into Fortnite and join games easily, but keep in mind that if you want to use features such as parties, voice chat or skins, you will need to use a custom backend.\n"
                  "Other popular options are LawinV2 and Momentum, both available on Github, but it's not recommended to use them if you are not an advanced user.\n"
                  "You can run these alternatives either either on your PC or on a server by selecting respectively \"Local\" or \"Remote\" from the \"Type\" section in the \"Backend\" tab of the Reboot Launcher."
          )
      ),
      InfoTile(
          title: Text("What is the Unreal Engine console?"),
          content: Text(
              "Many Fortnite versions don't support entering in game by clicking the \"Play\" button.\n"
                  "Instead, you need to click the key assigned to the Unreal Engine console, by default F8 or the tilde(the button above tab), and type open 127.0.0.1\n"
                  "Keep in mind that the Unreal Engine console key is controlled by the backend, so this is true only if you are using the embedded backend: custom backends might use different keys.\n"
                  "When using the embedded backend, you can customize the key used to open the console in the \"Backend\" tab of the Reboot Launcher."
          )
      ),
      InfoTile(
          title: Text("What is a matchmaker?"),
          content: Text(
              "A matchmaker is a piece of software that emulates the Epic Games server responsible for putting you in game when you click the \"Play\" button in Fortnite.\n"
                  "By default, the Reboot Launcher ships with a slightly customized version of Lawin's FortMatchmaker, an open source implementation available on Github.\n"
                  "If you are having any problems with the built in matchmaker, enable the \"Detached\" option in the \"Matchmaker\" tab of the Reboot Launcher to troubleshoot the issue.\n"
                  "Lawin's FortMatchmaker is an extremely basic implementation of a matchmaker: it takes the IP you configured in the \"Matchmaker\" tab, by default 127.0.0.1(your local machine) of the Reboot Launcher and send you with no wait to that game server.\n"
                  "Unfortunately right now the play button still doesn't work on many Fortnite versions, you so might need to use the Unreal Engine console.\n"
                  "Just like a backend, you can run a custom matchmaker, either on your PC or on a server with the appropriate configuration."
          )
      ),
      InfoTile(
          title: Text("The backend is not working correctly"),
          content: Text(
              "To resolve this issue:\n"
                  "- Check that your backend is working correctly from the \"Backend\" tab\n"
                  "- If you are using a custom backend, try to use the embedded one\n"
                  "- Try to run the backend as detached by enabling the \"Detached\" option in the \"Backend\" tab"
          )
      ),
      InfoTile(
          title: Text("The matchmaker is not working correctly"),
          content: Text(
              "To resolve this issue:\n"
                  "- Check that your matchmaker is working correctly from the \"Matchmaker\" tab\n"
                  "- If you are using a custom matchmaker, try to use the embedded one\n"
                  "- Try to run the matchmaker as detached by enabling the \"Detached\" option in the \"Matchmaker\" tab"
          )
      ),
      InfoTile(
          title: Text("Why do I see two Fortnite versions opened on my PC?"),
          content: Text(
              "As explained in the \"What is a Fortnite game server?\" section, one instance of Fortnite is used to host the game server, while the other is used to let you play.\n"
                  "The Fortnite instance used up by the game server is usually frozen, so it should be hard to use the wrong one to try to play.\n"
                  "If you do not want to host a game server yourself, you can:\n"
                  "1. Set a custom IP in the \"Matchmaker\" tab\n"
                  "2. Set a custom matchmaker in the \"Matchmaker\" tab\n"
                  "3. Disable the automatic game server from the \"Configuration\" section in the \"Host\" tab\n"
          )
      ),
      InfoTile(
          title: Text("I cannot open Fortnite because of an authentication error"),
          content: Text(
              "To resolve this issue:\n"
                  "- Check that your backend is working correctly from the \"Backend\" tab\n"
                  "- If you are using a custom backend, try to use the embedded one\n"
                  "- Try to run the backend as detached by enabling the \"Detached\" option in the \"Backend\" tab"
          )
      ),
      InfoTile(
          title: Text("I cannot enter in a match when I'm in Fortnite"),
          content: Text(
              "As explained in the \"What is the Unreal Engine console?\" section, the \"Play\" button doesn't work in many Fortnite versions.\n"
                  "Instead, you need to click the key assigned to the Unreal Engine console, by default F8 or the tilde(the button above tab), and type open 127.0.0.1"
          )
      ),
      InfoTile(
          title: Text("An error occurred while downloading a build (DioException)"),
          content: Text(
              "Unfortunately the servers that host the Fortnite builds are not reliable all the time so it might take a few tries, or downloading another version, to get started"
          )
      ),
      InfoTile(
          title: Text("Failed to open descriptor file / Fortnite crash Reporter / Unreal Engine crash reporter"),
          content: Text(
              "Your version of Fortnite is corrupted, download it again from the launcher or use another build."
          )
      ),
  ];

  @override
  void initState() {
    if(_settingsController.firstRun.value) {
      Timer.periodic(const Duration(seconds: 1), (timer) {
        if (_counter.value <= 0) {
          _settingsController.firstRun.value = false;
          timer.cancel();
        } else {
          _counter.value = _counter.value - 1;
        }
      });
    }
    super.initState();
  }

  @override
  List<Widget> get settings => _infoTiles;

  @override
  Widget? get button => Obx(() {
    if(!_settingsController.firstRun.value) {
      return const SizedBox.shrink();
    }

    final totalSecondsLeft = _counter.value;
    final minutesLeft = totalSecondsLeft ~/ 60;
    final secondsLeft = totalSecondsLeft % 60;
    return SizedBox(
        width: double.infinity,
        height: 48,
        child: Button(
          onPressed: totalSecondsLeft <= 0 ? () => pageIndex.value = RebootPageType.play.index : null,
          child: Text(
              totalSecondsLeft <= 0 ? "I have read the instructions"
                  : "Read the instructions for at least ${secondsLeft == 0 ? '$minutesLeft minute${minutesLeft > 1 ? 's' : ''}' : minutesLeft == 0 ? '$secondsLeft second${secondsLeft > 1 ? 's' : ''}' : '$minutesLeft minute${minutesLeft > 1 ? 's' : ''} and $secondsLeft second${secondsLeft > 1 ? 's' : ''}'}"
          ),
        )
    );
  });
}