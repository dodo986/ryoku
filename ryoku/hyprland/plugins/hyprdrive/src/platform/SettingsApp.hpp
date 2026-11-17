#pragma once

// The plugin's two hyprctl answers -- `hyprdrive-binds` (every bind that calls a
// hyprdrive dispatcher, which Ryoku Settings reads to show what Hyprdrive holds)
// and `hyprdrive-theme` (the look it is drawing in) -- and the `settings`
// dispatcher, which opens Ryoku Settings on the Hyprdrive page.

#include <string>

#include <hyprland/src/SharedDefs.hpp>

namespace Hyprdrive::SettingsApp {
    SDispatchResult dispatch(std::string arg);

    void init();
    void shutdown();
}
