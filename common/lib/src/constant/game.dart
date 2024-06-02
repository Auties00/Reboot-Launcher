const String kDefaultPlayerName = "Player";
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
  "Couldn't find pak signature file"
];
const List<String> kCannotConnectErrors = [
  "port 3551 failed: Connection refused",
  "Unable to login to Fortnite servers",
  "HTTP 400 response from ",
  "Network failure when attempting to check platform restrictions",
  "UOnlineAccountCommon::ForceLogout"
];
const String kGameFinishedLine = "PlayersLeft: 1";
