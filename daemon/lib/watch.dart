import 'dart:io';

import 'package:reboot_common/common.dart';
import 'package:supabase/supabase.dart';

void main(List<String> args) async {
  if(args.length != 5){
    stderr.writeln("Wrong args length: $args");
    return;
  }

  var supabase = SupabaseClient(supabaseUrl, supabaseAnonKey);
  var uuid = args[0];
  var gamePid = int.parse(args[1]);
  var launcherPid = int.parse(args[2]);
  var eacPid = int.parse(args[3]);
  var hosting = args[4].toLowerCase() == "true";
  await watchProcess(gamePid);
  Process.killPid(gamePid, ProcessSignal.sigabrt);
  Process.killPid(launcherPid, ProcessSignal.sigabrt);
  Process.killPid(eacPid, ProcessSignal.sigabrt);
  if(hosting) {
    await supabase.from("hosting")
        .delete()
        .match({'id': uuid});
  }

  exit(0);
}