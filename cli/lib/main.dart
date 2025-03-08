import 'package:interact/interact.dart';
import 'package:reboot_cli/cli.dart';
import 'package:tint/tint.dart';

const Command _buildImport = Command(name: 'import', parameters: ['version', 'path'], subCommands: []);
const Command _buildDownload = Command(name: 'download', parameters: ['version', 'path'], subCommands: []);
const Command _build = Command(name: 'build', parameters: [], subCommands: [_buildImport, _buildDownload]);
const Command _play = Command(name: 'play', parameters: [], subCommands: []);
const Command _host = Command(name: 'host', parameters: [], subCommands: []);
const Command _backend = Command(name: 'backend', parameters: [], subCommands: []);

void main(List<String> args) {
  _welcome();

  final parser = Parser(commands: [_build, _play, _host, _backend]);
  final command = parser.parse(args);
  print(command);
  _handleRootCommand(command);
}

void _handleRootCommand(CommandCall? call) {
  switch(call == null ? null : call.name) {
    case 'build':
      _handleBuildCommand(call?.subCall);
      break;
    case 'play':
      _handleBuildCommand(call?.subCall);
      break;
    case 'host':
      _handleBuildCommand(call?.subCall);
      break;
    case 'backend':
      _handleBuildCommand(call?.subCall);
      break;
    default:
      _askRootCommand();
      break;
  }
}

void _askRootCommand() {
  final commands = [_build.name, _play.name, _host.name, _backend.name];
  final commandSelector = Select.withTheme(
      prompt: ' Select a command:',
      options: commands,
      theme: Theme.colorfulTheme.copyWith(inputPrefix: '❓', inputSuffix: '')
  );
  _handleRootCommand(CommandCall(name: commands[commandSelector.interact()]));
}

void _handleBuildCommand(CommandCall? call) {
  switch(call == null ? null : call.name) {
    case 'import':
      _handleBuildImportCommand(call!);
      break;
    case 'download':
      _handleBuildDownloadCommand(call!);
      break;
    default:
      _askBuildCommand();
      break;
  }
}

void _handleBuildImportCommand(CommandCall call) {
  final version = call.parameters['path'];
  final path = call.parameters['path'];
  print(version);
  print(path);
}

void _handleBuildDownloadCommand(CommandCall call) {

}

void _askBuildCommand() {
  final commands = [_buildImport.name, _buildDownload.name];
  final commandSelector = Select.withTheme(
      prompt: ' Select a build command:',
      options: commands,
      theme: Theme.colorfulTheme.copyWith(inputPrefix: '❓', inputSuffix: '')
  );
  _handleBuildCommand(CommandCall(name: commands[commandSelector.interact()]));
}

void _handlePlayCommand(CommandCall? call) {

}

void _handleHostCommand(CommandCall? call) {

}

void _handleBackendCommand(CommandCall? call) {

}

void _welcome() => print("""
🎮 Reboot Launcher
🔥 Launch, manage, and play Fortnite using Project Reboot!
🚀 Developed by Auties00 - Version 10.0.7
""".green());