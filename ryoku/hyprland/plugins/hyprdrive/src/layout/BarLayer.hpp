#pragma once

#include <string_view>

namespace Hyprdrive {

// The desktop's bar, by its layer namespace: Ryoku's shell draws it as
// ryoku-bar. The pointer on it may still focus the window painted under it.
inline bool isBarLayer(std::string_view ns) {
    return ns == "ryoku-bar" || ns == "waybar";
}

}
