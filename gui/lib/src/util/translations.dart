import 'package:fluent_ui/fluent_ui.dart';
import 'package:intl/intl.dart';
import 'package:reboot_launcher/l10n/reboot_localizations.dart';

AppLocalizations? _translations;
bool _init = false;

AppLocalizations get translations {
  if(!_init) {
    throw StateError("Translations haven't been loaded");
  }

  return _translations!;
}

void loadTranslations(BuildContext context) {
  _translations = AppLocalizations.of(context)!;
  _init = true;
}

String get currentLocale => Intl.getCurrentLocale().split("_")[0];