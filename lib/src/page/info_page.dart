import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/foundation.dart';
import 'package:url_launcher/url_launcher.dart';

const String _discordLink = "https://discord.gg/NJU4QjxSMF";

class InfoPage extends StatelessWidget {
  const InfoPage({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.all(12.0),
      child: SizedBox(
          width: double.infinity,
          height: double.infinity,
          child: Stack(
            alignment: AlignmentDirectional.center,
            children: [
              _createVersionInfo(),

              Column(
                mainAxisAlignment: MainAxisAlignment.center,
                mainAxisSize: MainAxisSize.max,
                children: [
                  _createAutiesAvatar(),
                  const SizedBox(
                    height: 16.0,
                  ),
                  const Text("Made by Auties00"),
                  const SizedBox(
                    height: 16.0,
                  ),
                  _createDiscordButton()
                ],
              ),
            ],
          )
      ),
    );
  }

  Button _createDiscordButton() {
    return Button(
        child: const Text("Join the discord"),
        onPressed: () => launchUrl(Uri.parse(_discordLink)));
  }

  CircleAvatar _createAutiesAvatar() {
    return const CircleAvatar(
        radius: 48,
        backgroundImage: AssetImage("assets/images/auties.png"));
  }

  Align _createVersionInfo() {
    return const Align(
        alignment: Alignment.bottomRight,
        child: Text("Version 4.0${kDebugMode ? '-DEBUG' : ''}")
    );
  }
}