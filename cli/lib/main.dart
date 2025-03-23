import 'dart:io';
import 'dart:isolate';

import 'package:interact_cli/interact_cli.dart';
import 'package:reboot_cli/src/controller/config.dart';
import 'package:reboot_cli/src/util/console.dart';
import 'package:reboot_cli/src/util/extensions.dart';
import 'package:reboot_common/common.dart';
import 'package:tint/tint.dart';
import 'package:version/version.dart';

const Command _buildList = Command(name: 'list', parameters: [], subCommands: []);
const Command _buildImport = Command(name: 'import', parameters: ['version', 'path'], subCommands: []);
const Command _buildDownload = Command(name: 'download', parameters: ['version', 'path'], subCommands: []);
const Command _build = Command(name: 'versions', parameters: [], subCommands: [_buildList, _buildImport, _buildDownload]);
const Command _play = Command(name: 'play', parameters: [], subCommands: []);
const Command _host = Command(name: 'host', parameters: [], subCommands: []);
const Command _backend = Command(name: 'backend', parameters: [], subCommands: []);
final List<String> _versions = downloadableBuilds.map((build) => build.version.toString()).toList(growable: false);

void main(List<String> args) async {
  enableLoggingToConsole = false;
  useDefaultPath = true;

  print("""
🎮 Reboot Launcher
🔥 Launch, manage, and play Fortnite using Project Reboot!
🚀 Developed by Auties00 - Version 10.0.7
""".green());

  final parser = ConsoleParser(
      commands: [
        _build,
        _play,
        _host,
        _backend
      ]
  );
  final command = parser.parse(args);
  await _handleRootCommand(command);
}

Future<void> _handleRootCommand(CommandCall? command) async {
  if(command == null) {
    await _askRootCommand();
    return;
  }

  switch (command.name) {
    case 'versions':
      await _handleBuildCommand(command.subCall);
      break;
    case 'play':
      _handlePlayCommand(command.subCall);
      break;
    case 'host':
      _handleHostCommand(command.subCall);
      break;
    case 'backend':
      _handleBackendCommand(command.subCall);
      break;
    default:
      await _askRootCommand();
      break;
  }
}

Future<void> _askRootCommand() async {
  final commands = [_build.name, _play.name, _host.name, _backend.name];
  final commandSelector = Select.withTheme(
      prompt: ' Select a command:',
      options: commands,
      theme: Theme.colorfulTheme.copyWith(inputPrefix: '❓', inputSuffix: '', successSuffix: '', errorPrefix: '❌')
  );
  await _handleRootCommand(CommandCall(name: commands[commandSelector.interact()]));
}

Future<void> _handleBuildCommand(CommandCall? call) async {
  if(call == null) {
    _askBuildCommand();
    return;
  }

  switch(call.name) {
    case 'import':
      await _handleBuildImportCommand(call);
      break;
    case 'download':
      _handleBuildDownloadCommand(call);
      break;
    case 'list':
      _handleBuildListCommand(call);
      break;
    default:
      _askBuildCommand();
      break;
  }
}

void _handleBuildListCommand(CommandCall commandCall) {
  final versions = readVersions();
  final versionSelector = Select.withTheme(
      prompt: ' Select a version:',
      options: versions.map((version) => version.content.toString()).toList(growable: false),
      theme: Theme.colorfulTheme.copyWith(inputPrefix: '❓', inputSuffix: '', successSuffix: '', errorPrefix: '❌')
  );
  final version = versions[versionSelector.interact()];
  final actionSelector = Select.withTheme(
      prompt: ' Select an action:',
      options: ['Play', 'Host', 'Delete', 'Open in Explorer'],
      theme: Theme.colorfulTheme.copyWith(inputPrefix: '❓', inputSuffix: '', successSuffix: '', errorPrefix: '❌')
  );
  actionSelector.interact();
}

Future<void> _handleBuildImportCommand(CommandCall call) async {
  final version = _getOrPromptVersion(call);
  if(version == null) {
    return;
  }

  final path = await _getOrPromptPath(call, true);
  if(path == null) {
    return;
  }

  final fortniteVersion = FortniteVersion(
    name: "dummy",
      content: Version.parse(version),
      location: Directory(path)
  );
  writeVersion(fortniteVersion);
  print('');
  print('✅ Imported build: ${version.green()}');
}

String? _getOrPromptVersion(CommandCall call) {
  final version = call.parameters['version'];
  if(version != null) {
    final result = version.trim();
    if (_versions.contains(result)) {
      return result;
    }

    print('');
    print("❌ Unknown version: $result");
    return null;
  }

  stdout.write('❓ Type a version: ');
  final result = runAutoComplete(_autocompleteVersion).trim();
  if(_versions.contains(result)) {
    print('✅ Type a version: ${result.green()}');
    return result;
  }

  print('');
  print("❌ Unknown version: $version");
  return null;
}

Future<String?> _getOrPromptPath(CommandCall call, bool existing) async {
  var path = call.parameters['path'];
  if(path != null) {
    final result = path.trim();
    final check = await _checkBuildPath(result, existing);
    if(!check) {
      return null;
    }

    return result;
  }

  stdout.write('❓ Type a path: ');
  final result = runAutoComplete(_autocompletePath).trim();
  final check = await _checkBuildPath(result, existing);
  if(!check) {
    return null;
  }

  print('✅ Type a path: ${result.green()}');
  return result;
}

Future<bool> _checkBuildPath(String path, bool existing) async {
  final directory = Directory(path);
  if(!directory.existsSync()) {
    if(existing) {
      print('');
      print("❌ Unknown path: $path");
      return false;
    }else {
      directory.createSync(recursive: true);
    }
  }

  if (existing) {
    final checker = Spinner.withTheme(
        icon: '✅',
        rightPrompt: (status) => status != SpinnerStateType.inProgress ? 'Finished looking for FortniteClient-Win64-Shipping.exe' : 'Looking for FortniteClient-Win64-Shipping.exe...',
        theme: Theme.colorfulTheme.copyWith(successSuffix: '', errorPrefix: '❌', spinners: '🕐 🕑 🕒 🕓 🕔 🕕 🕖 🕗 🕘 🕙 🕚 🕛'.split(' '))
    ).interact();
    final result = await Future.wait([
      Future.delayed(const Duration(seconds: 1)).then((_) => true),
      Isolate.run(() => FortniteVersionExtension.findFiles(directory, "FortniteClient-Win64-Shipping.exe") != null)
    ]).then((values) => values.reduce((first, second) => first && second));
    checker.done();
    if(!result) {
      print("❌ Cannot find FortniteClient-Win64-Shipping.exe: $path");
      return false;
    }
  }

  return true;
}

String? _autocompleteVersion(String input) => input.isEmpty ? null : _versions.firstWhereOrNull((version) => version.toLowerCase().startsWith(input.toLowerCase()));

String? _autocompletePath(String path) {
  try {
    if (path.isEmpty) {
      return null;
    }

    final String separator = Platform.isWindows ? '\\' : '/';
    path = path.replaceAll('\\', separator);

    if (FileSystemEntity.isFileSync(path)) {
      return null;
    }

    if(FileSystemEntity.isDirectorySync(path)) {
      return path.endsWith(separator) ? null : path + separator;
    }

    final lastSeparatorIndex = path.lastIndexOf(separator);
    String directoryPath;
    String partialName;
    String prefixPath;
    if (lastSeparatorIndex == -1) {
      directoryPath = '';
      partialName = path;
      prefixPath = '';
    } else {
      directoryPath = path.substring(0, lastSeparatorIndex);
      partialName = path.substring(lastSeparatorIndex + 1);
      prefixPath = path.substring(0, lastSeparatorIndex + 1);
      if (directoryPath.isEmpty && lastSeparatorIndex == 0) {
        directoryPath = separator;
      } else if (directoryPath.isEmpty) {
        directoryPath = '.';
      }
    }

    final dir = Directory(directoryPath);
    if (!dir.existsSync()) {
      return null;
    }

    final entries = dir.listSync();
    final matches = <FileSystemEntity>[];
    for (var entry in entries) {
      final name = entry.path.split(separator).last;
      if (name.startsWith(partialName)) {
        matches.add(entry);
      }
    }

    if (matches.isEmpty) {
      return null;
    }

    matches.sort((a, b) {
      final aIsDir = a is Directory;
      final bIsDir = b is Directory;

      if (aIsDir != bIsDir) {
        return aIsDir ? -1 : 1;
      }

      final aName = a.path.split(separator).last;
      final bName = b.path.split(separator).last;

      if (aName.length != bName.length) {
        return aName.length - bName.length;
      }

      return aName.compareTo(bName);
    });

    final bestMatch = matches.first;
    final bestMatchName = bestMatch.path.split(separator).last;

    var result = prefixPath + bestMatchName;
    if (bestMatch is Directory) {
      result = result.endsWith(separator) ? result : result + separator;
    }
    return result;
  } catch (_) {
    return null;
  }
}


Future<void> _handleBuildDownloadCommand(CommandCall call) async {
  final version = _getOrPromptVersion(call);
  if(version == null) {
    return;
  }

  final parsedVersion = Version.parse(version);
  final build = downloadableBuilds.firstWhereOrNull((build) => build.version == parsedVersion);
  if(build == null) {
    print('');
    print("❌ Cannot find mirror for version: $parsedVersion");
    return;
  }

  final path = await _getOrPromptPath(call, false);
  if(path == null) {
    return;
  }

  double progress = 0;
  bool extracting = false;
  final downloader = Spinner.withTheme(
      icon: '✅',
      rightPrompt: (status) => status != SpinnerStateType.inProgress ? 'Finished ${extracting ? 'extracting' : 'downloading'} ${parsedVersion.toString()}' : '${extracting ? 'Extracting' : 'Downloading'} ${parsedVersion.toString()} (${progress.round()}%)...',
      theme: Theme.colorfulTheme.copyWith(successSuffix: '', errorPrefix: '❌', spinners: '🕐 🕑 🕒 🕓 🕔 🕕 🕖 🕗 🕘 🕙 🕚 🕛'.split(' '))
  ).interact();
  final parsedDirectory = Directory(path);
  final receivePort = ReceivePort();
  SendPort? sendPort;
  receivePort.listen((message) {
    if(message is FortniteBuildDownloadProgress) {
      if(message.progress >= 100) {
        sendPort?.send(kStopBuildDownloadSignal);
        stopDownloadServer();
        downloader.done();
        receivePort.close();
        final fortniteVersion = FortniteVersion(
          name: "dummy",
            content: parsedVersion,
            location: parsedDirectory
        );
        writeVersion(fortniteVersion);
        print('');
        print('✅ Downloaded build: ${version.green()}');
      }else {
        progress = message.progress;
        extracting = message.extracting;
      }
    }else if(message is SendPort) {
      sendPort = message;
    }else {
      sendPort?.send(kStopBuildDownloadSignal);
      stopDownloadServer();
      downloader.done();
      receivePort.close();
      print("❌ Cannot download build: $message");
    }
  });
  final options = FortniteBuildDownloadOptions(
      build,
      parsedDirectory,
      receivePort.sendPort
  );
  await downloadArchiveBuild(options);
}

void _askBuildCommand() {
  final commands = [_buildList.name, _buildImport.name, _buildDownload.name];
  final commandSelector = Select.withTheme(
      prompt: ' Select a version command:',
      options: commands,
      theme: Theme.colorfulTheme.copyWith(inputPrefix: '❓', inputSuffix: '', successSuffix: '', errorPrefix: '❌')
  );
  _handleBuildCommand(CommandCall(name: commands[commandSelector.interact()]));
}

void _handlePlayCommand(CommandCall? call) {

}

void _handleHostCommand(CommandCall? call) {

}

void _handleBackendCommand(CommandCall? call) {

}