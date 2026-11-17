#include "PluginParts.hpp"
#include <hyprland/src/keybinds/Resolver.hpp>
#include "HoverTarget.hpp"
using namespace PluginDetail;

namespace PluginDetail {
SDispatchResult onNavigateDispatcher(std::string arg) {
    const auto OVERVIEW = activeScrollOverview();
    if (!OVERVIEW)
        return {};

    if (arg != "left" && arg != "right" && arg != "up" && arg != "down")
        return {.success = false, .error = "invalid arg. expected left|right|up|down"};

    OVERVIEW->moveSelection(arg);
    return {};
}

SDispatchResult onWindowDispatcher(std::string arg) {
    const auto OVERVIEW = activeScrollOverview();
    if (!OVERVIEW)
        return {};

    if (arg == "click") {
        // Only reached when onMouseButton let the press through, i.e. over a
        // TOP/OVERLAY layer: the press is that layer's (the bind is
        // non-consuming), so the overview must not act on it too.
        if (OvInternal::pointerOnTopLayer(Desktop::focusState()->monitor()))
            return {};
        OVERVIEW->performPrimaryClickAction();
        return {};
    }

    if (arg != "select" && arg != "close")
        return {.success = false, .error = "invalid arg. expected select|close|click"};

    OVERVIEW->windowDispatcherAction(arg);
    return {};
}

SDispatchResult onGestureDispatcher(std::string arg) {
    CConstVarList data(arg, 0, 's', true);
    const std::string CMD = std::string{data[0]};
    if (CMD.empty() || CMD == "help") {
        return {.success = false,
                .error   = "hyprdrive:gesture add|remove <fingers> <direction> <action> [arg] [mod:KEYS] [scale:N] [disable_inhibit]"};
    }

    const bool REMOVE = CMD == "remove" || CMD == "unset" || CMD == "del";
    const bool ADD    = CMD == "add" || CMD == "set";
    if (!ADD && !REMOVE)
        return {.success = false, .error = std::format("hyprdrive:gesture: unknown command {}", CMD)};

    size_t fingerCount = 0;
    try {
        fingerCount = std::stoul(std::string{data[1]});
    } catch (...) {
        return {.success = false, .error = "hyprdrive:gesture: fingers must be an integer"};
    }

    const std::string DIRSTR = std::string{data[2]};
    const bool        ISCLICK = DIRSTR == "click" || DIRSTR == "tap";
    const auto direction = ISCLICK ? TRACKPAD_GESTURE_DIR_NONE : g_pTrackpadGestures->dirForString(data[2]);
    if (!ISCLICK && direction == TRACKPAD_GESTURE_DIR_NONE)
        return {.success = false, .error = std::format("Invalid direction: {}", DIRSTR)};

    int      idx            = 3;
    uint32_t modMask        = 0;
    float    deltaScale     = 1.F;
    bool     disableInhibit = false;
    while (!data[idx].empty()) {
        const auto TOK = std::string{data[idx]};
        if (TOK.starts_with("mod:")) {
            modMask = static_cast<uint32_t>(Keybinds::modMaskFromString(TOK.substr(4)));
            idx++;
            continue;
        }
        if (TOK.starts_with("scale:")) {
            try {
                deltaScale = std::clamp(std::stof(TOK.substr(6)), 0.1F, 10.F);
            } catch (...) {
                return {.success = false, .error = "invalid scale"};
            }
            idx++;
            continue;
        }
        if (TOK == "disable_inhibit") {
            disableInhibit = true;
            idx++;
            continue;
        }
        break;
    }

    const std::string ACTION = REMOVE ? "unset" : std::string{data[idx]};
    std::string       extra;
    if (!REMOVE) {
        for (int i = idx + 1; !data[i].empty(); ++i) {
            const auto TOK = std::string{data[i]};
            if (TOK.starts_with("mod:") || TOK.starts_with("scale:") || TOK == "disable_inhibit")
                continue;
            if (!extra.empty())
                extra += " ";
            extra += TOK;
        }
    }

    if (ISCLICK) {
        if (REMOVE)
            removeClickGesture(fingerCount, modMask);
        else
            addClickGesture(fingerCount, ACTION, modMask, disableInhibit, extra);
        return {};
    }

    const auto res = applyTrackpadGesture(fingerCount, direction, ACTION, modMask, deltaScale, disableInhibit, extra);
    if (!res)
        return {.success = false, .error = res.error()};
    return {};
}

} // namespace PluginDetail
