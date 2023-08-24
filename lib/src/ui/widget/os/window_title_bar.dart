import 'package:fluent_ui/fluent_ui.dart';
import 'package:reboot_launcher/src/ui/widget/os/window_button.dart';
import 'package:reboot_launcher/src/util/os.dart';
import 'package:system_theme/system_theme.dart';

class WindowTitleBar extends StatelessWidget {
  final bool focused;
  
  const WindowTitleBar({Key? key, required this.focused}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    var lightMode = FluentTheme.of(context).brightness.isLight;
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: [
        MinimizeWindowButton(
          colors: WindowButtonColors(
              iconNormal: focused || !isWin11 ? lightMode ? Colors.black : Colors.white : SystemTheme.accentColor.lighter,
              iconMouseDown: lightMode ? Colors.black : Colors.white,
              iconMouseOver: lightMode ? Colors.black : Colors.white,
              normal: Colors.transparent,
              mouseOver: _color,
              mouseDown: _color.withOpacity(0.7)),
        ),
        MaximizeWindowButton(
          colors: WindowButtonColors(
              iconNormal: focused || !isWin11 ? lightMode ? Colors.black : Colors.white : SystemTheme.accentColor.lighter,
              iconMouseDown: lightMode ? Colors.black : Colors.white,
              iconMouseOver: lightMode ? Colors.black : Colors.white,
              normal: Colors.transparent,
              mouseOver: _color,
              mouseDown: _color.withOpacity(0.7)),
        ),
        CloseWindowButton(
          colors: WindowButtonColors(
            iconNormal: focused || !isWin11 ? lightMode ? Colors.black : Colors.white : SystemTheme.accentColor.lighter,
            iconMouseDown: lightMode ? Colors.black : Colors.white,
            iconMouseOver: lightMode ? Colors.black : Colors.white,
            normal: Colors.transparent,
            mouseOver: Colors.red,
            mouseDown: Colors.red.withOpacity(0.7),
          ),
        ),
      ],
    );
  }

  Color get _color =>
      SystemTheme.accentColor.accent;
}
