import 'package:reboot_launcher/src/util/translations.dart';
import 'package:url_launcher/url_launcher.dart';

Future<void> openYoutubeTutorial() => launchUrl(Uri.parse("https://www.youtube.com/watch?v=nrVE2RB0qa4"));

Future<void> openDiscordServer() => launchUrl(Uri.parse("https://discord.gg/reboot"));

Future<void> openTutorials() => launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/blob/master/documentation/$currentLocale"));

Future<void> openPortTutorial() => launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/blob/master/documentation/$currentLocale/PortForwarding.md"));

Future<void> openBugReport() => launchUrl(Uri.parse("https://github.com/Auties00/reboot_launcher/issues"));