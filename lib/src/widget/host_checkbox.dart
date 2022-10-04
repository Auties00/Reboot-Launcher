import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/widget/smart_switch.dart';

class DeploymentSelector extends StatefulWidget {
  const DeploymentSelector({Key? key}) : super(key: key);

  @override
  State<DeploymentSelector> createState() => _DeploymentSelectorState();
}

class _DeploymentSelectorState extends State<DeploymentSelector> {
  final Map<GameType, String> _options = {
    GameType.client: "Client",
    GameType.server: "Server",
    GameType.headlessServer: "Headless Server"
  };
  final Map<GameType, String> _descriptions = {
    GameType.client: "A fortnite client will be launched to play multiplayer games",
    GameType.server: "A fortnite client will be launched to host multiplayer games",
    GameType.headlessServer: "A fortnite client will be launched in the background to host multiplayer games",
  };
  final GameController _gameController = Get.find<GameController>();
  bool? _value;

  @override
  void initState() {
    switch(_gameController.type.value){
      case GameType.client:
        _value = false;
        break;
      case GameType.server:
        _value = true;
        break;
      case GameType.headlessServer:
        _value = null;
        break;
    }
    super.initState();
  }

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: _descriptions[_gameController.type.value]!,
      child: InfoLabel(
          label: _options[_gameController.type.value]!,
        child: Checkbox(
          checked: _value,
          onChanged: _onSelected
        ),
      ),
    );
  }

  void _onSelected(bool? value){
    if(value == null){
      _gameController.type(GameType.client);
      setState(() => _value = false);
      return;
    }

    if(value){
      _gameController.type(GameType.server);
      setState(() => _value = true);
      return;
    }

    _gameController.type(GameType.headlessServer);
    setState(() => _value = null);
  }
}
