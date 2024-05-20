import 'dart:io';

import 'package:fluent_ui/fluent_ui.dart';
import 'package:flutter/scheduler.dart';

final RegExp _winBuildRegex = RegExp(r'(?<=\(Build )(.*)(?=\))');

bool get isWin11 {
  var result = _winBuildRegex.firstMatch(Platform.operatingSystemVersion)?.group(1);
  if(result == null){
    return false;
  }

  var intBuild = int.tryParse(result);
  return intBuild != null && intBuild > 22000;
}

bool get isDarkMode
    => SchedulerBinding.instance.platformDispatcher.platformBrightness.isDark;