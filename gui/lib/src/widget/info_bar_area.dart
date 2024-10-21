import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:reboot_launcher/src/page/pages.dart';

class InfoBarArea extends StatefulWidget {
  const InfoBarArea({super.key});

  @override
  State<InfoBarArea> createState() => InfoBarAreaState();
}

class InfoBarAreaState extends State<InfoBarArea> {
  final Rx<List<Widget>> _children = Rx([]);

  void insertChild(Widget child) {
    _children.value.add(child);
    _children.refresh();
  }

  bool removeChild(Widget child) {
    final result = _children.value.remove(child);
    _children.refresh();
    return result;
  }

  @override
  Widget build(BuildContext context) => StreamBuilder(
      stream: pagesController.stream,
      builder: (context, _) => Obx(() => Padding(
        padding: EdgeInsets.only(
            bottom: hasPageButton ? 72.0 : 16.0
        ),
        child: Column(
            mainAxisAlignment: MainAxisAlignment.end,
            crossAxisAlignment: CrossAxisAlignment.start,
            children: _children.value.map((child) => Padding(
                padding: EdgeInsets.only(
                    top: 12.0
                ),
                child: child
            )).toList(growable: false)
        ),
      ))
  );
}