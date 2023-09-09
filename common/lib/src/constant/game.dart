const String kDefaultPlayerName = "Player";
const String kDefaultGameServerHost = "127.0.0.1";
const String kDefaultGameServerPort = "7777";
const String shutdownLine = "FOnlineSubsystemGoogleCommon::Shutdown()";
const List<String> corruptedBuildErrors = [
  "when 0 bytes remain",
  "Pak chunk signature verification failed!"
];
const List<String> cannotConnectErrors = [
  "port 3551 failed: Connection refused",
  "Unable to login to Fortnite servers",
  "HTTP 400 response from ",
  "Network failure when attempting to check platform restrictions",
  "UOnlineAccountCommon::ForceLogout"
];
