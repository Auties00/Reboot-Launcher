#pragma once

// rb::sdk: the C++ interface to the engine over reboot_client. Header-only; each client compiles it
// with its own toolchain and CRT and links only reboot_client.
#include "reboot/sdk/context.hpp"
#include "reboot/sdk/dispatcher.hpp"
#include "reboot/sdk/error.hpp"
#include "reboot/sdk/event.hpp"
#include "reboot/sdk/live_list.hpp"
#include "reboot/sdk/op.hpp"
#include "reboot/sdk/subscription.hpp"
