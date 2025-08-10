import 'package:reboot_common/common.dart';
import 'package:reboot_common/src/browser/server_browser_state.dart';

sealed class ServerBrowserEvent {

}

final class ServerBrowserStateEvent extends ServerBrowserEvent {
  final ServerBrowserState state;

  ServerBrowserStateEvent({required this.state});
}

final class ServerBrowserAddEvent extends ServerBrowserEvent {
  final List<ServerBrowserEntry> entries;

  ServerBrowserAddEvent({required this.entries});
}

final class ServerBrowserRemoveEvent extends ServerBrowserEvent {
  final List<String> entries;

  ServerBrowserRemoveEvent({required this.entries});
}

final class ServerBrowserErrorEvent extends ServerBrowserEvent {
  final String error;

  ServerBrowserErrorEvent({required this.error});
}