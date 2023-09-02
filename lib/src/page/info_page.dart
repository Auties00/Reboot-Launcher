import 'package:fluent_ui/fluent_ui.dart' hide showDialog;
import 'package:get/get.dart';
import 'package:reboot_launcher/src/widget/common/setting_tile.dart';

import 'package:reboot_launcher/src/controller/settings_controller.dart';

class InfoPage extends StatefulWidget {
  const InfoPage({Key? key}) : super(key: key);

  @override
  State<InfoPage> createState() => _InfoPageState();
}

class _InfoPageState extends State<InfoPage> with AutomaticKeepAliveClientMixin {
  final SettingsController _settingsController = Get.find<SettingsController>();
  late final ScrollController _controller;

  @override
  bool get wantKeepAlive => true;

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
    super.build(context);
    return Column(
      children: [
        Expanded(
          child: ListView(
            children: [
              SettingTile(
                title: 'What is Project Reboot?',
                subtitle: 'Project Reboot allows anyone to easily host a game server for most of Fortnite\'s seasons. '
                    'The project was started on Discord by Milxnor. '
                    'The project is no longer being actively maintained.',
                titleStyle: FluentTheme
                    .of(context)
                    .typography
                    .title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'What is a game server?',
                subtitle: 'When you join a Fortnite Game, your client connects to a game server that allows you to play with others. '
                    'You can join someone else\'s game server, or host one on your PC by going to the "Host" tab. ',
                titleStyle: FluentTheme
                    .of(context)
                    .typography
                    .title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'What is a client?',
                subtitle: 'A client is the actual Fortnite game. '
                    'You can download any version of Fortnite from the launcher in the "Play" tab. '
                    'You can also import versions from your local PC, but remember that these may be corrupted. '
                    'If a local version doesn\'t work, try installing it from the launcher before reporting a bug.',
                titleStyle: FluentTheme
                    .of(context)
                    .typography
                    .title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'What is an authenticator?',
                subtitle: 'An authenticator is a program that handles authentication, parties and voice chats. '
                    'By default, a LawinV1 server will be started for you to play. '
                    'You can use also use an authenticator running locally(on your PC) or remotely(on another PC). '
                    'Changing the authenticator settings can break the client and game server: unless you are an advanced user, do not edit, for any reason, these settings! '
                    'If you need to restore these settings, go to the "Settings" tab and click on "Restore Defaults". ',
                titleStyle: FluentTheme
                    .of(context)
                    .typography
                    .title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'Do I need to update DLLs?',
                subtitle: 'No, all the files that the launcher uses are automatically updated. '
                    'You can use your own DLLs by going to the "Settings" tab, but make sure that they don\'t create a console that reads IO or the launcher will stop working correctly. '
                    'Unless you are an advanced user, changing these options is not recommended',
                titleStyle: FluentTheme
                    .of(context)
                    .typography
                    .title,
                contentWidth: null,
              ),
              const SizedBox(
                height: 8.0,
              ),
              SettingTile(
                title: 'Where can I report bugs or ask for new features?',
                subtitle: 'Go to the "Settings" tab and click on report bug. '
                    'Please make sure to be as specific as possible when filing a report as it\'s crucial to make it as easy to fix/implement',
                titleStyle: FluentTheme
                    .of(context)
                    .typography
                    .title,
                contentWidth: null,
              )
            ],
          ),
        )
      ],
    );
  }
}