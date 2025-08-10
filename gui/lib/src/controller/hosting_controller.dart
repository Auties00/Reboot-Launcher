import 'package:dart_ipify/dart_ipify.dart';
import 'package:fluent_ui/fluent_ui.dart';
import 'package:get/get.dart';
import 'package:get_storage/get_storage.dart';
import 'package:reboot_common/common.dart';
import 'package:reboot_launcher/main.dart';
import 'package:uuid/uuid.dart';

import '../util/cryptography.dart';

class HostingController extends GetxController {
  static const String storageName = "v3_hosting_storage";

  late final GetStorage? _storage;
  late final String uuid;
  late final TextEditingController accountUsername;
  late final TextEditingController accountPassword;
  late final TextEditingController name;
  late final FocusNode nameFocusNode;
  late final TextEditingController description;
  late final FocusNode descriptionFocusNode;
  late final TextEditingController password;
  late final FocusNode passwordFocusNode;
  late final RxBool showPassword;
  late final RxBool headless;
  late final RxBool autoRestart;
  late final RxBool started;
  late final Rxn<GameInstance> instance;
  late final TextEditingController customLaunchArgs;

  HostingController() {
    _storage = appWithNoStorage ? null : GetStorage(storageName);
    uuid = _storage?.read("uuid") ?? const Uuid().v4();
    _storage?.write("uuid", uuid);
    accountUsername = TextEditingController(text: _storage?.read("account_username") ?? kDefaultHostName);
    accountUsername.addListener(() => _storage?.write("account_username", accountUsername.text));
    accountPassword = TextEditingController(text: _storage?.read("account_password") ?? "");
    accountPassword.addListener(() => _storage?.write("account_password", password.text));
    name = TextEditingController(text: _storage?.read("name"));
    name.addListener(() => _storage?.write("name", name.text));
    description = TextEditingController(text: _storage?.read("description"));
    description.addListener(() => _storage?.write("description", description.text));
    password = TextEditingController(text: _storage?.read("password") ?? "");
    password.addListener(() => _storage?.write("password", password.text));
    nameFocusNode = FocusNode();
    descriptionFocusNode = FocusNode();
    passwordFocusNode = FocusNode();
    headless = RxBool(_storage?.read("headless") ?? true);
    headless.listen((value) => _storage?.write("headless", value));
    autoRestart = RxBool(_storage?.read("auto_restart") ?? true);
    autoRestart.listen((value) => _storage?.write("auto_restart", value));
    started = RxBool(false);
    showPassword = RxBool(false);
    instance = Rxn();
    customLaunchArgs = TextEditingController(text: _storage?.read("custom_launch_args") ?? "");
    customLaunchArgs.addListener(() => _storage?.write("custom_launch_args", customLaunchArgs.text));
  }

  Future<ServerBrowserEntry> createServerBrowserEntry() async {
    final passwordText = password.text;
    final hasPassword = passwordText.isNotEmpty;
    var ip = await Ipify.ipv4();
    if(hasPassword) {
      ip = aes256Encrypt(ip, passwordText);
    }
    return ServerBrowserEntry(
        id: uuid,
        name: name.text,
        description: description.text,
        author: accountUsername.text,
        ip: ip,
        version: instance.value!.version.toString(),
        password: hasPassword ? hashPassword(passwordText) : "",
        timestamp: DateTime.now()
    );
  }

  void reset() {
    accountUsername.text = kDefaultHostName;
    accountPassword.text = "";
    name.text = "";
    description.text = "";
    showPassword.value = false;
    instance.value = null;
    headless.value = true;
    autoRestart.value = true;
    customLaunchArgs.text = "";
  }
}
