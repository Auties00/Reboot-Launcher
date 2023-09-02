import 'package:flutter/material.dart';
import 'package:reboot_common/common.dart';
import 'package:system_theme/system_theme.dart';

class WindowBorder extends StatelessWidget {
  const WindowBorder({Key? key}) : super(key: key);

  @override
  Widget build(BuildContext context) {
    return IgnorePointer(
        child: Padding(
          padding: const EdgeInsets.only(
              top: 1
          ),
          child: Container(
              decoration: BoxDecoration(
                  borderRadius: BorderRadius.circular(10),
                  border: Border.all(
                      color: SystemTheme.accentColor.accent,
                      width: appBarWidth.toDouble()
                  )
              )
          ),
        )
    );
  }
}
