import 'dart:io';
import 'package:reboot_launcher/supabase.dart';
import 'package:supabase/supabase.dart';

void main(List<String> args) async {
  if(args.length != 4){
    stderr.writeln("Wrong args length: $args");
    return;
  }

  var instance = _GameInstance(args[0], int.parse(args[1]), int.parse(args[2]), int.parse(args[3]));
  var supabase = SupabaseClient(supabaseUrl, supabaseAnonKey);
  while(true){
    sleep(const Duration(seconds: 2));
    stdout.writeln("Looking up tasks...");
    var result = Process.runSync('tasklist', []);
    var output = result.stdout.toString();
    if(output.contains(" ${instance.gameProcess} ")) {
      continue;
    }

    stdout.writeln("Killing $instance");
    Process.killPid(instance.gameProcess, ProcessSignal.sigabrt);
    if(instance.launcherProcess != -1){
      Process.killPid(instance.launcherProcess, ProcessSignal.sigabrt);
    }

    if(instance.eacProcess != -1){
      Process.killPid(instance.eacProcess, ProcessSignal.sigabrt);
    }

    await supabase.from('hosts')
        .delete()
        .match({'id': instance.uuid});
    exit(0);
  }
}

class _GameInstance {
  final String uuid;
  final int gameProcess;
  final int launcherProcess;
  final int eacProcess;

  _GameInstance(this.uuid, this.gameProcess, this.launcherProcess, this.eacProcess);

  @override
  String toString() {
    return '{uuid: $uuid, gameProcess: $gameProcess, launcherProcess: $launcherProcess, eacProcess: $eacProcess}';
  }
}