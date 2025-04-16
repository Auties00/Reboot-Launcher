import 'package:version/version.dart';

const String kDefaultPlayerName = "Player";
const String kDefaultHostName = "Host";
const String kDefaultGameServerHost = "127.0.0.1";
const String kDefaultGameServerPort = "7777";
const String kInitializedLine = "Game Engine Initialized";
const List<String> kLoggedInLines = [
  "[UOnlineAccountCommon::ContinueLoggingIn]",
  "(Completed)"
];
const String kShutdownLine = "FOnlineSubsystemGoogleCommon::Shutdown()";
const List<String> kCorruptedBuildErrors = [
  "Critical error",
  "when 0 bytes remain",
  "Pak chunk signature verification failed!",
  "LogWindows:Error: Fatal error!"
];
const List<String> kCannotConnectErrors = [
  "port 3551 failed: Connection refused",
  "Unable to login to Fortnite servers",
  "HTTP 400 response from ",
  "Network failure when attempting to check platform restrictions",
  "UOnlineAccountCommon::ForceLogout"
];
const String kGameFinishedLine = "TeamsLeft: 1";
const String kDisplayLine = "Display";
const String kDisplayInitializedLine = "Initialized";
const String kShippingExe = "FortniteClient-Win64-Shipping.exe";
const String kLauncherExe = "FortniteLauncher.exe";
const String kEacExe = "FortniteClient-Win64-Shipping_EAC.exe";
const String kCrashReportExe = "CrashReportClient.exe";
const String kGFSDKAftermathLibDll = "GFSDK_Aftermath_Lib.dll";
final Version kMaxAllowedVersion = Version.parse("30.10");