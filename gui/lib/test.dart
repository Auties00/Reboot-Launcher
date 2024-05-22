import 'dart:io';

import 'package:reboot_common/common.dart';

void main() async {
    final process = await startProcess(
        executable: File("C:\\FortniteBuilds\\Fortnite 4.2\\Fortnite 4.2\\Fortnite1\\FortniteGame\\Binaries\\Win64\\FortniteClient-Win64-Shipping-Reboot.exe"),
      args: "-epicapp=Fortnite -epicenv=Prod -epiclocale=en-us -epicportal -skippatchcheck -nobe -fromfl=eac -fltoken=3db3ba5dcbd2e16703f3978d -caldera=eyJhbGciOiJFUzI1NiIsInR5cCI6IkpXVCJ9.eyJhY2NvdW50X2lkIjoiYmU5ZGE1YzJmYmVhNDQwN2IyZjQwZWJhYWQ4NTlhZDQiLCJnZW5lcmF0ZWQiOjE2Mzg3MTcyNzgsImNhbGRlcmFHdWlkIjoiMzgxMGI4NjMtMmE2NS00NDU3LTliNTgtNGRhYjNiNDgyYTg2IiwiYWNQcm92aWRlciI6IkVhc3lBbnRpQ2hlYXQiLCJub3RlcyI6IiIsImZhbGxiYWNrIjpmYWxzZX0.VAWQB67RTxhiWOxx7DBjnzDnXyyEnX7OljJm-j2d88G_WgwQ9wrE6lwMEHZHjBd1ISJdUO1UVUqkfLdU5nofBQ -AUTH_LOGIN=Player698@projectreboot.dev -AUTH_PASSWORD=Rebooted -AUTH_TYPE=epic -nullrhi -nosplash -nosound".split(" ")
    );
    process.stdOutput.listen((event) => stdout.writeln(event));
    process.errorOutput.listen((event) => stdout.writeln(event));
}