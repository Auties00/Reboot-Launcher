import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:system_theme/system_theme.dart';

class WindowTitleBar extends StatelessWidget {
  const WindowTitleBar({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    var lightMode = FluentTheme.of(context).brightness.isLight;
    return Row(
      mainAxisAlignment: MainAxisAlignment.end,
      children: [
        MinimizeWindowButton(
          colors: WindowButtonColors(
              iconNormal: lightMode ? Colors.black : Colors.white,
              iconMouseDown: lightMode ? Colors.black : Colors.white,
              iconMouseOver: lightMode ? Colors.black : Colors.white,
              normal: Colors.transparent,
              mouseOver: _getColor(context),
              mouseDown: _getColor(context).withOpacity(0.7)),
        ),
        MaximizeWindowButton(
          colors: WindowButtonColors(
              iconNormal: lightMode ? Colors.black : Colors.white,
              iconMouseDown: lightMode ? Colors.black : Colors.white,
              iconMouseOver: lightMode ? Colors.black : Colors.white,
              normal: Colors.transparent,
              mouseOver: _getColor(context),
              mouseDown: _getColor(context).withOpacity(0.7)),
        ),
        CloseWindowButton(
          onPressed: () {
            appWindow.close();
          },
          colors: WindowButtonColors(
            iconNormal: lightMode ? Colors.black : Colors.white,
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

  Color _getColor(BuildContext context) =>
      FluentTheme.of(context).brightness.isDark
          ? SystemTheme.accentColor.light
          : SystemTheme.accentColor.light;
}
