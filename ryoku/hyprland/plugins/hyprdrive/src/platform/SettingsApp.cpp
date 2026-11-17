// See SettingsApp.hpp.
#include "SettingsApp.hpp"

#include "Compat.hpp"
#include "Config.hpp"
#include "Look.hpp"
#include "globals.hpp"

#include <hyprland/src/event/EventBus.hpp>
#include <hyprland/src/keybinds/Manager.hpp>
#include <hyprland/src/plugins/PluginAPI.hpp>
#include <hyprutils/os/Process.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <format>
#include <string>
#include <sys/stat.h>
#include <unistd.h>
#include <vector>

namespace Hyprdrive::SettingsApp {
    namespace {
        SP<IPC::Socket1::SCommand> g_hyprctl;
        SP<IPC::Socket1::SCommand> g_hyprctlBinds;
        CHyprSignalListener g_preReload;

        // Qt reads #AARRGGBB; the channels are clamped because a theme colour
        // can arrive slightly out of range from a mix.
        std::string qtColor(const CHyprColor& c) {
            const auto BYTE = [](double v) { return sc<int>(std::lround(std::clamp(v, 0.0, 1.0) * 255.0)); };
            return std::format("#{:02x}{:02x}{:02x}{:02x}", BYTE(c.a), BYTE(c.r), BYTE(c.g), BYTE(c.b));
        }

        std::string jsonEscape(std::string_view s) {
            std::string out;
            out.reserve(s.size() + 2);
            for (const char ch : s) {
                switch (ch) {
                    case '"': out += "\\\""; break;
                    case '\\': out += "\\\\"; break;
                    case '\n': out += "\\n"; break;
                    case '\t': out += "\\t"; break;
                    default:
                        if (sc<unsigned char>(ch) < 0x20)
                            out += std::format("\\u{:04x}", sc<int>(ch));
                        else
                            out += ch;
                }
            }
            return out;
        }

        // `hyprctl hyprdrive-theme [-j]`: the look the plugin is drawing in, as
        // the bridge handed it over (Look.hpp), for checking it against the Hub.
        std::string themeReply(IPC::Socket1::eOutputFormat format) {
            const auto& L = Look::current();
            const std::pair<const char*, CHyprColor> ROWS[] = {
                {"pane", L.planes.pane}, {"paneStroke", L.planes.paneStroke}, {"card", L.planes.card}, {"fg", L.roles.fg},
                {"fg2", L.roles.fg2},    {"sel", L.roles.sel},                {"container", L.roles.container},
                {"gold", L.roles.gold},  {"jasper", L.roles.jasper},
            };
            const bool  JSON = format == IPC::Socket1::FORMAT_JSON;
            std::string out  = JSON ? std::format(R"({{"design": "{}", "dark": {}, "round": {}, "sans": "{}", "mono": "{}")", jsonEscape(L.design), L.dark,
                                                  L.corner.round, jsonEscape(L.sans), jsonEscape(L.mono)) :
                                      std::format("design: {}\ndark: {}\nround: {}\nsans: {}\nmono: {}\n", L.design, L.dark, L.corner.round, L.sans, L.mono);
            for (const auto& [NAME, C] : ROWS)
                out += JSON ? std::format(R"(, "{}": "{}")", NAME, qtColor(C)) : std::format("{}: {}\n", NAME, qtColor(C));
            return JSON ? out + "}" : out;
        }

        // Hyprland's modifier bits, named as a bind is written (SUPER + ALT).
        std::string modNames(Input::ModifierMask mask) {
            static constexpr std::pair<Input::eKeyboardModifiers, const char*> NAMES[] = {
                {HL_MODIFIER_META, "SUPER"}, {HL_MODIFIER_CTRL, "CTRL"}, {HL_MODIFIER_ALT, "ALT"}, {HL_MODIFIER_SHIFT, "SHIFT"},
                {HL_MODIFIER_CAPS, "CAPS"},  {HL_MODIFIER_MOD2, "MOD2"}, {HL_MODIFIER_MOD3, "MOD3"}, {HL_MODIFIER_MOD5, "MOD5"},
            };
            std::string out;
            for (const auto& [BIT, NAME] : NAMES)
                if (mask & BIT)
                    out += (out.empty() ? "" : " + ") + std::string{NAME};
            return out;
        }

        // `hyprctl hyprdrive-binds [-j]`: every bind that calls a hyprdrive
        // dispatcher, as Hyprland holds it right now -- what the settings'
        // Keybinds pane lists. `hyprctl binds` cannot say it: under Lua each
        // bind is an opaque `__lua` ref (Config::boundAction resolves ours).
        std::string bindsReply(IPC::Socket1::eOutputFormat format) {
            std::string out = format == IPC::Socket1::FORMAT_JSON ? "[" : "";
            bool        first = true;
            if (Keybinds::mgr()) {
                for (const auto& KB : Keybinds::mgr()->registry().binds()) {
                    if (!KB)
                        continue;
                    const auto ACTION = Config::boundAction(KB->metadata().argument);
                    if (!ACTION)
                        continue;
                    const std::string MODS = modNames(KB->modifierMask());
                    const std::string KEY  = KB->keyNames().size() == 1 ? KB->keyNames()[0] : KB->metadata().displayKey;
                    if (format == IPC::Socket1::FORMAT_JSON) {
                        out += std::format(R"({}{{"mods": "{}", "key": "{}", "submap": "{}", "mouse": {}, "description": "{}", "dispatcher": "{}", "arg": "{}"}})", first ? "" : ",",
                                           jsonEscape(MODS), jsonEscape(KEY), jsonEscape(KB->metadata().submap), KB->hasFlag(Keybinds::BIND_FLAG_MOUSE) ? "true" : "false", jsonEscape(KB->metadata().description.value_or("")),
                                           jsonEscape(ACTION->first), jsonEscape(ACTION->second));
                    } else
                        out += std::format("{}{}{} -> hyprdrive:{} {}{}\n", MODS, MODS.empty() ? "" : " + ", KEY, ACTION->first, ACTION->second,
                                           KB->metadata().submap.empty() ? "" : " (submap " + KB->metadata().submap + ")");
                    first = false;
                }
            }
            if (format == IPC::Socket1::FORMAT_JSON)
                out += "]";
            return out;
        }

    }

    // Settings live in Ryoku Settings, on its Hyprdrive page.
    SDispatchResult dispatch(std::string arg) {
        std::vector<std::string> args{"hub", "open", "hyprdrive"};
        if (arg.starts_with("pane ") && arg.size() > 5)
            args.back() += "/" + arg.substr(5);
        else if (!arg.empty() && arg != "open")
            return {.success = false, .error = "settings: expected open or pane <section>"};
        Hyprutils::OS::CProcess("ryoku-shell", args).runAsync();
        return {};
    }

    void init() {
        g_hyprctl = HyprlandAPI::registerHyprCtlCommand(HYPRDRIVE_HANDLE, IPC::Socket1::SCommand{.name = "hyprdrive-theme", .match = IPC::Socket1::COMMAND_MATCH_EXACT, .handler = [](const IPC::Socket1::SRequest& r) { return IPC::Socket1::SResponse{themeReply(r.format)}; }});
        g_hyprctlBinds = HyprlandAPI::registerHyprCtlCommand(HYPRDRIVE_HANDLE, IPC::Socket1::SCommand{.name = "hyprdrive-binds", .match = IPC::Socket1::COMMAND_MATCH_EXACT, .handler = [](const IPC::Socket1::SRequest& r) { return IPC::Socket1::SResponse{bindsReply(r.format)}; }});
        // The Lua state the bind refs live in is rebuilt by every reload.
        g_preReload = Event::bus()->m_events.config.preReload.listen([] { Config::forgetBindActions(); });
    }

    void shutdown() {
        if (g_hyprctl)
            HyprlandAPI::unregisterHyprCtlCommand(HYPRDRIVE_HANDLE, g_hyprctl);
        g_hyprctl.reset();
        if (g_hyprctlBinds)
            HyprlandAPI::unregisterHyprCtlCommand(HYPRDRIVE_HANDLE, g_hyprctlBinds);
        g_hyprctlBinds.reset();
        g_preReload.reset();
        Config::forgetBindActions();
    }
}
