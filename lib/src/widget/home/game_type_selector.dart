import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/controller/game_controller.dart';
import 'package:reboot_launcher/src/controller/settings_controller.dart';
import 'package:reboot_launcher/src/model/game_type.dart';
import 'package:reboot_launcher/src/widget/shared/smart_switch.dart';

class GameTypeSelector extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();

  GameTypeSelector({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Tooltip(
      message: "The type of Fortnite instance to launch",
      child: _createAdvancedSelector(),
    );
  }

  Widget _createAdvancedSelector() => InfoLabel(
      label: "Type",
      child: SizedBox(
          width: double.infinity,
          child: Obx(() => DropDownButton(
              leading: Text(_gameController.type.value.name),
              items: GameType.values
                  .map((type) => _createItem(type))
                  .toList())
          )
      )
  );

  MenuFlyoutItem _createItem(GameType type) => MenuFlyoutItem(
      text: SizedBox(
          width: double.infinity,
          child: Tooltip(
              message: type.message,
              child: Text(type.name)
          )
      ),
      onPressed: () {
        _gameController.type(type);
        _gameController.started.value = _gameController.currentGameInstance != null;
      }
  );
}
