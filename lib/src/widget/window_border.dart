import 'package:bitsdojo_window/bitsdojo_window.dart';
import 'package:flutter/material.dart';
import 'package:system_theme/system_theme.dart';

import '../util/os.dart';

class WindowBorder extends StatelessWidget {
  const WindowBorder({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
        child: Padding(
          padding: EdgeInsets.only(
              top: 1 / appWindow.scaleFactor
          ),
          child: Container(
          decoration: BoxDecoration(
              borderRadius: BorderRadius.circular(10),
              border: Border.all(
                  color: SystemTheme.accentColor.accent,
                  width: appBarSize.toDouble()
              )
          )
          ),
    ));
  }
}
