#include "Look.hpp"
#include "Config.hpp"

#include <hyprland/src/Compositor.hpp>
#include <hyprland/src/debug/log/Logger.hpp>
#include <hyprland/src/output/Monitor.hpp>
#include <hyprland/src/render/Renderer.hpp>
#include <hyprland/src/state/MonitorState.hpp>

#include <fontconfig/fontconfig.h>

#include <algorithm>
#include <charconv>
#include <csignal>
#include <cstdlib>
#include <dlfcn.h>
#include <fcntl.h>
#include <filesystem>
#include <format>
#include <spawn.h>
#include <unistd.h>

extern char** environ;

namespace Hyprdrive::Look {

namespace {

    SLook            g_look = signature();
    uint64_t         g_generation = 1;
    std::string      g_pending;
    std::string      g_fontDirAdded;
    pid_t            g_child = -1;
    int              g_readFd = -1;
    int              g_writeFd = -1;
    wl_event_source* g_readSource  = nullptr;
    wl_event_source* g_retryTimer  = nullptr;
    int              g_failures    = 0;
    bool             g_stopping    = false;

    // A bridge that keeps dying is left alone after a few tries: the signature
    // look is drawn, and the log says why.
    constexpr int RETRY_MS  = 2000;
    constexpr int MAX_TRIES = 5;

    std::optional<CHyprColor> parseColour(std::string_view v) {
        if (v.starts_with('#'))
            v.remove_prefix(1);
        if (v.size() != 6 && v.size() != 8)
            return std::nullopt;
        uint32_t rgba = 0;
        if (std::from_chars(v.data(), v.data() + v.size(), rgba, 16).ec != std::errc{})
            return std::nullopt;
        if (v.size() == 6)
            rgba = (rgba << 8) | 0xFF;
        return CHyprColor{((rgba >> 24) & 0xFF) / 255.F, ((rgba >> 16) & 0xFF) / 255.F, ((rgba >> 8) & 0xFF) / 255.F, (rgba & 0xFF) / 255.F};
    }

    std::optional<float> parseNumber(std::string_view v) {
        float out = 0.F;
        if (std::from_chars(v.data(), v.data() + v.size(), out).ec != std::errc{})
            return std::nullopt;
        return out;
    }

    CHyprColor* roleSlot(SRoles& r, std::string_view name) {
        static const std::pair<std::string_view, CHyprColor SRoles::*> ROLES[] = {
            {"ground", &SRoles::ground}, {"stone1", &SRoles::stone1}, {"stone2", &SRoles::stone2}, {"stone3", &SRoles::stone3},
            {"stone4", &SRoles::stone4}, {"slab", &SRoles::slab},     {"field", &SRoles::field},   {"vein", &SRoles::vein},
            {"edge", &SRoles::edge},     {"fg", &SRoles::fg},         {"fg2", &SRoles::fg2},       {"fg3", &SRoles::fg3},
            {"sel", &SRoles::sel},       {"onSel", &SRoles::onSel},   {"gold", &SRoles::gold},     {"onGold", &SRoles::onGold},
            {"goldInk", &SRoles::goldInk}, {"jade", &SRoles::jade},   {"lapis", &SRoles::lapis},   {"amber", &SRoles::amber},
            {"jasper", &SRoles::jasper}, {"scrim", &SRoles::scrim},   {"container", &SRoles::container},
            {"onContainer", &SRoles::onContainer},
        };
        for (const auto& [KEY, MEMBER] : ROLES)
            if (KEY == name)
                return &(r.*MEMBER);
        return nullptr;
    }

    CHyprColor* planeSlot(SPlanes& p, std::string_view name) {
        static const std::pair<std::string_view, CHyprColor SPlanes::*> PLANES[] = {
            {"side", &SPlanes::side}, {"pane", &SPlanes::pane}, {"paneStroke", &SPlanes::paneStroke},
            {"card", &SPlanes::card}, {"cardStroke", &SPlanes::cardStroke},
        };
        for (const auto& [KEY, MEMBER] : PLANES)
            if (KEY == name)
                return &(p.*MEMBER);
        return nullptr;
    }

    float* cutSlot(SCuts& c, std::string_view name) {
        static const std::pair<std::string_view, float SCuts::*> CUTS[] = {
            {"large", &SCuts::large}, {"mid", &SCuts::mid},         {"small", &SCuts::small}, {"menu", &SCuts::menu},
            {"overlay", &SCuts::overlay}, {"tooltip", &SCuts::tooltip}, {"token", &SCuts::token},
        };
        for (const auto& [KEY, MEMBER] : CUTS)
            if (KEY == name)
                return &(c.*MEMBER);
        return nullptr;
    }

    bool sameColour(const CHyprColor& a, const CHyprColor& b) {
        return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
    }

    bool sameLook(const SLook& a, const SLook& b) {
        const auto COLOURS = [](const SLook& l) {
            return std::to_array<const CHyprColor*>({&l.roles.ground, &l.roles.stone1, &l.roles.stone2, &l.roles.stone3, &l.roles.stone4, &l.roles.slab,
                                                     &l.roles.field, &l.roles.vein, &l.roles.edge, &l.roles.fg, &l.roles.fg2, &l.roles.fg3, &l.roles.sel,
                                                     &l.roles.onSel, &l.roles.gold, &l.roles.onGold, &l.roles.goldInk, &l.roles.jade, &l.roles.lapis,
                                                     &l.roles.amber, &l.roles.jasper, &l.roles.scrim, &l.roles.container, &l.roles.onContainer,
                                                     &l.planes.side, &l.planes.pane, &l.planes.paneStroke, &l.planes.card, &l.planes.cardStroke});
        };
        const auto A = COLOURS(a), B = COLOURS(b);
        for (size_t i = 0; i < A.size(); ++i)
            if (!sameColour(*A[i], *B[i]))
                return false;
        return a.design == b.design && a.dark == b.dark && a.highContrast == b.highContrast && a.reduceMotion == b.reduceMotion &&
            a.corner.round == b.corner.round && a.corner.scale == b.corner.scale && a.corner.pill == b.corner.pill && a.corner.max == b.corner.max &&
            a.sans == b.sans && a.mono == b.mono && a.fontDir == b.fontDir;
    }

    // The faces the bridge copied out of the library, for pango through
    // fontconfig: once per directory, and the font caches dropped after.
    void addFaces(const std::string& dir) {
        if (dir.empty() || dir == g_fontDirAdded || !std::filesystem::is_directory(dir))
            return;
        if (FcConfigAppFontAddDir(nullptr, reinterpret_cast<const FcChar8*>(dir.c_str())) == FcTrue)
            g_fontDirAdded = dir;
    }

    void apply(SLook next) {
        addFaces(next.fontDir);
        if (sameLook(next, g_look))
            return;
        g_look = std::move(next);
        ++g_generation;
        Hyprdrive::Config::invalidateThemeCache();
        if (State::monitorState() && g_pHyprRenderer)
            for (const auto& MONITOR : State::monitorState()->monitors())
                g_pHyprRenderer->damageMonitor(MONITOR);
    }

    std::string bridgePath() {
        if (const char* ENV = std::getenv("HYPRDRIVE_LOOK"); ENV && *ENV)
            return ENV;
        Dl_info info{};
        if (dladdr(reinterpret_cast<void*>(&bridgePath), &info) != 0 && info.dli_fname) {
            const auto BESIDE = std::filesystem::path{info.dli_fname}.parent_path() / "ryoku-hyprdrive-look";
            if (access(BESIDE.c_str(), X_OK) == 0)
                return BESIDE.string();
        }
        return "ryoku-hyprdrive-look";
    }

    void closeChild() {
        if (g_readSource) {
            wl_event_source_remove(g_readSource);
            g_readSource = nullptr;
        }
        if (g_readFd >= 0)
            close(g_readFd);
        if (g_writeFd >= 0)
            close(g_writeFd);
        g_readFd = g_writeFd = -1;
        if (g_child > 0)
            kill(g_child, SIGTERM);
        g_child = -1;
        g_pending.clear();
    }

    bool spawn();

    int onRetry(void*) {
        spawn();
        return 0;
    }

    void scheduleRetry() {
        if (g_stopping || ++g_failures > MAX_TRIES || !g_pCompositor || !g_pCompositor->m_wlEventLoop) {
            if (g_failures > MAX_TRIES)
                Log::logger->log(Log::ERR, "[hyprdrive] look bridge keeps exiting; drawing Lithic's signature look");
            return;
        }
        if (!g_retryTimer)
            g_retryTimer = wl_event_loop_add_timer(g_pCompositor->m_wlEventLoop, onRetry, nullptr);
        if (g_retryTimer)
            wl_event_source_timer_update(g_retryTimer, RETRY_MS);
    }

    int onReadable(int fd, uint32_t mask, void*) {
        char buf[4096];
        for (;;) {
            const ssize_t N = read(fd, buf, sizeof(buf));
            if (N > 0) {
                g_pending.append(buf, sc<size_t>(N));
                continue;
            }
            if (N < 0 && (errno == EAGAIN || errno == EINTR))
                break;
            closeChild();
            scheduleRetry();
            return 0;
        }
        // A block ends at a blank line; only the last whole one counts.
        for (size_t end; (end = g_pending.find("\n\n")) != std::string::npos;) {
            SLook next = signature();
            parseBlock(std::string_view{g_pending}.substr(0, end), next);
            g_pending.erase(0, end + 2);
            g_failures = 0;
            apply(std::move(next));
        }
        if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
            closeChild();
            scheduleRetry();
        }
        return 0;
    }

    bool spawn() {
        if (g_stopping || g_child > 0 || !g_pCompositor || !g_pCompositor->m_wlEventLoop)
            return false;
        int out[2], in[2];
        if (pipe2(out, O_CLOEXEC) != 0)
            return false;
        if (pipe2(in, O_CLOEXEC) != 0) {
            close(out[0]);
            close(out[1]);
            return false;
        }
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, in[0], STDIN_FILENO);
        posix_spawn_file_actions_adddup2(&actions, out[1], STDOUT_FILENO);

        const std::string PATH = bridgePath();
        char*             argv[] = {const_cast<char*>(PATH.c_str()), const_cast<char*>("--watch"), nullptr};
        pid_t             pid = -1;
        const int         RC  = posix_spawnp(&pid, PATH.c_str(), &actions, nullptr, argv, environ);
        posix_spawn_file_actions_destroy(&actions);
        close(in[0]);
        close(out[1]);
        if (RC != 0) {
            close(in[1]);
            close(out[0]);
            Log::logger->log(Log::ERR, std::format("[hyprdrive] cannot start the look bridge {}: {}", PATH, strerror(RC)));
            scheduleRetry();
            return false;
        }
        g_child   = pid;
        g_readFd  = out[0];
        g_writeFd = in[1];
        fcntl(g_readFd, F_SETFL, fcntl(g_readFd, F_GETFL) | O_NONBLOCK);
        g_readSource = wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, g_readFd, WL_EVENT_READABLE, onReadable, nullptr);
        return true;
    }

} // namespace

SLook signature() {
    // Lithic's own colours with no palette, dark (lithic-roles --mode dark).
    SLook       l;
    const auto  C = [](std::string_view hex) { return parseColour(hex).value_or(CHyprColor{}); };
    auto&       r = l.roles;
    r.ground = C("000000ff"), r.stone1 = C("000000ff"), r.stone2 = C("000000ff"), r.stone3 = C("161514ff"), r.stone4 = C("1b1918ff");
    r.slab = C("000000ff"), r.field = C("23211fff"), r.vein = C("46433fff"), r.edge = C("66625eff");
    r.fg = C("cdc4baff"), r.fg2 = C("b0a9a0ff"), r.fg3 = C("958f87ff"), r.sel = C("ce5e32ff"), r.onSel = C("000000ff");
    r.gold = C("ce5e32ff"), r.onGold = C("000000ff"), r.goldInk = C("dc6a3eff");
    r.jade = C("7ab8a0ff"), r.lapis = C("8aa2d6ff"), r.amber = C("d8ad52ff"), r.jasper = C("eb596cff"), r.scrim = C("0000009e");
    r.container = C("ce5e3229"), r.onContainer = C("cdc4baff");
    l.planes = {.side = r.ground, .pane = r.stone2, .paneStroke = r.vein, .card = r.slab, .cardStroke = r.vein};
    return l;
}

void parseBlock(std::string_view block, SLook& into) {
    while (!block.empty()) {
        const auto      NL   = block.find('\n');
        std::string_view line = block.substr(0, NL);
        block.remove_prefix(NL == std::string_view::npos ? block.size() : NL + 1);
        const auto EQ = line.find('=');
        if (EQ == std::string_view::npos)
            continue;
        const auto KEY = line.substr(0, EQ), VAL = line.substr(EQ + 1);
        const bool TRUE_ = VAL == "true";

        if (KEY.starts_with("role.")) {
            if (auto* slot = roleSlot(into.roles, KEY.substr(5)); slot)
                if (const auto C = parseColour(VAL))
                    *slot = *C;
        } else if (KEY.starts_with("plane.")) {
            if (auto* slot = planeSlot(into.planes, KEY.substr(6)); slot)
                if (const auto C = parseColour(VAL))
                    *slot = *C;
        } else if (KEY.starts_with("cut.")) {
            if (auto* slot = cutSlot(into.cuts, KEY.substr(4)); slot)
                if (const auto N = parseNumber(VAL))
                    *slot = *N;
        } else if (KEY == "design")
            into.design = VAL;
        else if (KEY == "dark")
            into.dark = TRUE_;
        else if (KEY == "contrast")
            into.highContrast = TRUE_;
        else if (KEY == "reduceMotion")
            into.reduceMotion = TRUE_;
        else if (KEY == "corner.round")
            into.corner.round = TRUE_;
        else if (KEY == "corner.scale")
            into.corner.scale = parseNumber(VAL).value_or(into.corner.scale);
        else if (KEY == "corner.pill")
            into.corner.pill = parseNumber(VAL).value_or(into.corner.pill);
        else if (KEY == "corner.max")
            into.corner.max = parseNumber(VAL).value_or(into.corner.max);
        else if (KEY == "font.sans" && !VAL.empty())
            into.sans = VAL;
        else if (KEY == "font.mono" && !VAL.empty())
            into.mono = VAL;
        else if (KEY == "font.dir")
            into.fontDir = VAL;
    }
}

void init() {
    g_stopping = false;
    g_failures = 0;
    spawn();
}

void shutdown() {
    g_stopping = true;
    if (g_retryTimer) {
        wl_event_source_remove(g_retryTimer);
        g_retryTimer = nullptr;
    }
    closeChild();
}

const SLook& current() {
    return g_look;
}

uint64_t generation() {
    return g_generation;
}

float corner(float cut, float w, float h) {
    const auto& C = g_look.corner;
    if (!C.round || cut <= 0.F)
        return cut;
    const float LEAST = std::max(0.F, std::min(w, h));
    if (LEAST <= C.pill)
        return LEAST / 2.F;
    return std::min({cut * C.scale, C.max, LEAST / 2.F});
}

float cornerPower() {
    return g_look.corner.round ? 2.F : 1.F;
}

}
