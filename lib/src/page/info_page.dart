import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:url_launcher/url_launcher.dart';

const String _discordLink = "https://discord.gg/NJU4QjxSMF";

class InfoPage extends StatelessWidget {
  const InfoPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Column(
      children: [
        const Expanded(child: SizedBox()),
        Column(
          children: [
            const CircleAvatar(
                radius: 48,
                backgroundImage: AssetImage("assets/images/auties.png")),
            const SizedBox(
              height: 16.0,
            ),
            const Text("Made by Auties00"),
            const SizedBox(
              height: 16.0,
            ),
            Button(
                child: const Text("Join the discord"),
                onPressed: () => launchUrl(Uri.parse(_discordLink))),
          ],
        ),
        const Expanded(
            child: Align(
                alignment: Alignment.bottomLeft, child: Text("Version 3.1${kDebugMode ? '-DEBUG' : ''}")))
      ],
    );
  }
}
