
import 'package:dart_vlc/dart_vlc.dart';
import 'package:fluent_ui/fluent_ui.dart' hide Card;
import 'package:flutter/foundation.dart';
import 'package:flutter/material.dart';
import 'package:get/get.dart';

import '../controller/settings_controller.dart';

class InfoPage extends StatefulWidget {
  const InfoPage({Key? key}) : super(key: key);

  @override
  State<InfoPage> createState() => _InfoPageState();
}

class _InfoPageState extends State<InfoPage> {
  final SettingsController _settingsController = Get.find<SettingsController>();

  @override
  void initState() {
    if(_settingsController.player == null){
      var player = Player(id: 1);
      player.open(
          Media.network("https://cdn.discordapp.com/attachments/1006260074416701450/1038844107986055190/tutorial.mp4")
      );
      _settingsController.player = player;
    }

    _settingsController.player?.play();
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return SizedBox(
      width: double.infinity,
      height: double.infinity,
      child: Card(
          child: Video(
            player: _settingsController.player,
            height: MediaQuery.of(context).size.height * 0.85,
            width: MediaQuery.of(context).size.width * 0.90,
            scale: 1.0,
            showControls: true,
          )
      ),
    );
  }
}