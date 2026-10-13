#pragma once

#include "reboot/base/types.hpp"

namespace rb {

enum class LogLevel : u8 { Trace, Debug, Info, Warn, Error };

enum class LogCategory : u8 { Engine, Ipc, Net, Storage, Builds, Play, Host, Backend, GameOutput, Wine, Browser, Update, Client, Ui };

}  // namespace rb
