import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/messenger/abstract/overlay.dart';
import 'package:reboot_launcher/src/messenger/implementation/profile.dart';

class ProfileWidget extends StatefulWidget {
  final GlobalKey<OverlayTargetState> overlayKey;
  const ProfileWidget({required this.overlayKey});

  @override
  State<ProfileWidget> createState() => _ProfileWidgetState();
}

class _ProfileWidgetState extends State<ProfileWidget> {
  final GameController _gameController = Get.find<GameController>();

  @override
  Widget build(BuildContext context) => OverlayTarget(
    key: widget.overlayKey,
    child: HoverButton(
        margin: const EdgeInsets.all(8.0),
        onPressed: () async {
          if(await showProfileForm(context)) {
            setState(() {});
          }
        },
        builder: (context, states) => Container(
          decoration: BoxDecoration(
              color: ButtonThemeData.uncheckedInputColor(
                FluentTheme.of(context),
                states,
                transparentWhenNone: true,
              ),
              borderRadius: BorderRadius.all(Radius.circular(6.0))
          ),
          child: Padding(
            padding: const EdgeInsets.symmetric(
                vertical: 8.0,
                horizontal: 8.0
            ),
            child: Row(
              children: [
                Container(
                    width: 64,
                    height: 64,
                    decoration: const BoxDecoration(
                        shape: BoxShape.circle
                    ),
                    child: Image.asset("assets/images/user.png")
                ),
                const SizedBox(
                  width: 12.0,
                ),
                Column(
                  crossAxisAlignment: CrossAxisAlignment.start,
                  children: [
                    Text(
                        _username,
                        textAlign: TextAlign.start,
                        style: const TextStyle(
                            fontWeight: FontWeight.w600
                        ),
                        maxLines: 1
                    ),
                    Text(
                        _email,
                        textAlign: TextAlign.start,
                        style: const TextStyle(
                            fontWeight: FontWeight.w100
                        ),
                        maxLines: 1
                    )
                  ],
                )
              ],
            ),
          ),
        )
    ),
  );

  String get _username {
    var username = _gameController.username.text;
    if(username.isEmpty) {
      return kDefaultPlayerName;
    }

    var atIndex = username.indexOf("@");
    if(atIndex == -1) {
      return username.substring(0, 1).toUpperCase() + username.substring(1);
    }

    var result = username.substring(0, atIndex);
    return result.substring(0, 1).toUpperCase() + result.substring(1);
  }

  String get _email {
    var username = _gameController.username.text;
    if(username.isEmpty) {
      return "$kDefaultPlayerName@projectreboot.dev";
    }

    if(username.contains("@")) {
      return username.toLowerCase();
    }

    return "$username@projectreboot.dev".toLowerCase();
  }
}
