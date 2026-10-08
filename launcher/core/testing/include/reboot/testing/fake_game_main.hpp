#pragma once

namespace reboot::testing {

// reboot-fake-game `[--script=<file>] <game argv...>`, Windows only; without --script it reads
// `<its own path>.script.json` when present. It prints its argv first, for argv and redaction tests.
int fake_game_main(int argc, char** argv);

}  // namespace reboot::testing
