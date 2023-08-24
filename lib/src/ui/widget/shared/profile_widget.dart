import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';

import '../../controller/game_controller.dart';

class ProfileWidget extends StatelessWidget {
  final GameController _gameController = Get.find<GameController>();

  ProfileWidget({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return Padding(
      padding: const EdgeInsets.symmetric(
          horizontal: 12.0,
          vertical: 12.0
      ),
      child: GestureDetector(
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
            const Column(
              crossAxisAlignment: CrossAxisAlignment.start,
              children: [
                Text(
                  "Auties00",
                  textAlign: TextAlign.start,
                  style: TextStyle(
                      fontWeight: FontWeight.w600
                  ),
                ),
                Text(
                  "alautiero@gmail.com",
                  textAlign: TextAlign.start,
                  style: TextStyle(
                      fontWeight: FontWeight.w100
                  ),
                )
              ],
            )
          ],
        ),
      ),
    );
  }
}
