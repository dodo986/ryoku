#include "PluginParts.hpp"
#include "FunctionHooks.hpp"
#include "WindowPlacement.hpp"
#include "Workers.hpp"
#include "TerminateLog.hpp"
#include <hyprland/src/layout/target/WindowTarget.hpp>
#include <hyprland/src/managers/eventLoop/EventLoopManager.hpp>
using namespace PluginDetail;

namespace PluginDetail {
// Methods
CFunctionHook* g_pScrollRenderWorkspaceHook = nullptr;
CFunctionHook* g_pScrollAddDamageHookA      = nullptr;
CFunctionHook* g_pScrollAddDamageHookB      = nullptr;
CFunctionHook* g_pScrollDamageSurfaceHook   = nullptr;
CFunctionHook* g_pScrollScheduleFrameHook   = nullptr;
CFunctionHook* g_pScrollSendFrameEventsHook = nullptr;
CFunctionHook* g_pScrollSurfaceFrameHook    = nullptr;
CFunctionHook* g_pScrollMouseMovedHook      = nullptr;
CFunctionHook* g_pMouseButtonHook           = nullptr;

bool g_unloading = false;
} // namespace PluginDetail

// Do NOT change this function.
APICALL EXPORT std::string PLUGIN_API_VERSION() {
    return HYPRLAND_API_VERSION;
}

namespace PluginDetail {
bool renderingOverview = false;
bool damageFromSurface = false;
PHLMONITORREF renderingOverviewMonitor;
bool g_scrollOverviewHooksActive = false;

// ConfigureHold.hpp: a gesture's in-between sizes never reach the client.
CFunctionHook* g_pSendWindowSizeHook = nullptr;

// Mosaic.hpp hideDuringSlide: a canvas window off its view stays off the
// desktop slide.
CFunctionHook* g_pShouldRenderWindowHook = nullptr;
} // namespace PluginDetail

namespace PluginDetail {
// OwnSizeTile.hpp: a fixed-size window's tile has room for it.
CFunctionHook* g_pTargetPlaceHook = nullptr;

// Keycodes, not a count: Super-tap key-repeat used to increment a counter
// without a matching release, after which searchKeyFor refused every letter
// for the rest of the session.
std::unordered_set<uint32_t> g_heldGuardKeys;

// ---------------------------------------------------------------------------
// THE LETTER TICK (user, 2026-09-19: the pop is "sometimes smooth sometimes
// it's not", worst on "the trailing characters bouncing").
//
// The field's animation was kept alive from INSIDE the render pass: renderSearch
// ended in `damage(); requestInputFrame();`, and requestInputFrame is a
// scheduleFrame issued while a frame is already being composited, where it is
// liable to be absorbed by the one in flight instead of booking the next one.
//
// MEASURED, not guessed (49s screen recording plus the in-compositor profile,
// 3140 frames). Frames dropped, by position in the pop:
//
//     f=2-4   (first 65ms)      0.0 - 0.5%
//     f=6-8   (100-135ms)      15   - 17%
//     f=9-12                   22   - 29%
//     f=15-21 (250-350ms)      36   - 39%
//
// The head of every pop is clean because the compositor's OWN animation
// variables -- resultsWidth, searchDock, resultsBand -- are still running and
// schedule those frames as a side effect. Once they settle, the letters are the
// only thing still moving and the in-pass request is all that is left, so the
// tail of every pop loses better than one frame in three. That is why it got
// worse the more letters were on screen (more tail), why it was periodic rather
// than constant, and why a held backspace stayed smooth until the end -- the
// same coupling Overview.cpp's frame-policy comment records from the
// earlier incarnation, where letters froze outright once the animvars settled.
//
// Ticking from the event loop instead: half a 60Hz frame, so a request is
// always pending before the next vblank, self-sustaining while anything in the
// field still moves and stopping on its own when nothing does. renderSearch
// keeps its own call as a belt-and-braces fallback; it is harmless when a tick
// has already booked the frame.
wl_event_source*  g_letterTickTimer = nullptr;

// OVERVIEW_CPP_SPEC 5 F5. The overview is not a wayland client. Seat keyboard
// focus is cleared while it is open, and the query is assembled from the raw
// key stream with those events cancelled so they cannot reach a window or bar.
//
// This used to be a pure isSearchKey(keycode) predicate, consulted separately
// for the press and the release on the promise that both would agree. COMPOSE
// IS STATEFUL AND BREAKS THAT PROMISE: a sequence can complete between a press
// and its release, so the predicate flips underneath and leaks a stray release
// into the window below. The decision is made ONCE, on the press, and the
// release is consumed iff that press was.
std::unordered_set<uint32_t> g_consumedPresses;

// Hyprland's key-repeat timer only fires for focused clients. Search steals
// seat focus, so Backspace would delete once and then stall until another
// physical press. Drive repeats ourselves off the compositor loop.
wl_event_source* g_searchRepeatTimer = nullptr;
uint32_t         g_searchRepeatKey   = ~0u;
} // namespace PluginDetail

static int              g_fileWakeFd     = -1;
static wl_event_source* g_fileWakeSource = nullptr;

namespace PluginDetail {
CHyprSignalListener g_pRawKeyHook;
} // namespace PluginDetail

static CHyprSignalListener g_pSessionLockHook;

namespace PluginDetail {
CHyprSignalListener g_pTapSuperButtonHook;
CHyprSignalListener g_pTapSuperAxisHook;
} // namespace PluginDetail

static CHyprSignalListener g_pRenderPreHook;
static CHyprSignalListener g_pMembOpenHook;
static CHyprSignalListener g_pMembLateHook;
static CHyprSignalListener g_pMembCloseHook;
static CHyprSignalListener g_pMembKillHook;
static CHyprSignalListener g_pMembGoneHook;
static CHyprSignalListener g_pMembPinHook;
static CHyprSignalListener g_pMembFloatHook;
static CHyprSignalListener g_pMembTitleHook;
static CHyprSignalListener g_pMembClassHook;
static CHyprSignalListener g_pMembWsHook;
static CHyprSignalListener g_pMembFullHook;
static CHyprSignalListener g_pConfigHook;
static CHyprSignalListener g_pPointerMoveHook;
static CHyprSignalListener g_pPointerButtonHook;
static CHyprSignalListener g_pDesktopRenderHook;
static CHyprSignalListener g_pScratchExitRenderHook;
static CHyprSignalListener g_pMonitorGoneHook;
static CHyprSignalListener g_pScrollCameraActiveHook;
static CHyprSignalListener g_pScrollCameraMovedHook;
static CHyprSignalListener g_pClickPreReloadHook;

namespace PluginDetail {
bool                g_tapSuperCandidate = false;
uint32_t            g_tapSuperPressedAt = 0;
} // namespace PluginDetail

// Mosaic.cpp: Super held down to pan an infinite canvas was not a tap.
namespace Hyprdrive {
    void cancelSuperTap() {
        g_tapSuperCandidate = false;
    }
}

namespace PluginDetail {
bool                g_tapShiftCandidate = false;
uint32_t            g_tapShiftPressedAt = 0;
// Release time of the last completed SHIFT tap; 0 = no pair in flight.
uint32_t            g_tapShiftLastTapMs = 0;
bool                g_tapCtrlCandidate  = false;
uint32_t            g_tapCtrlPressedAt  = 0;
uint32_t            g_tapCtrlLastTapMs  = 0;
} // namespace PluginDetail

APICALL EXPORT PLUGIN_DESCRIPTION_INFO PLUGIN_INIT(HANDLE handle) {
    HYPRDRIVE_HANDLE = handle;

    // Every worker thread is started through Workers (Workers.hpp). Accepting
    // again matters only to an image an earlier unload had to keep loaded.
    Hyprdrive::Workers::setLog([](const std::string& line) { Log::logger->log(Log::ERR, Log::logFnName(), "{}", line); });
    Hyprdrive::Workers::reopen();

    // The debug logs older versions left in /tmp, readable by everyone at
    // umask 022: ours are removed at every load (DebugPaths::removeLegacy).
    Hyprdrive::DebugPaths::removeLegacy();

    // Before anything can record a use of a pre-rename name. ONE RELEASE.
    Hyprdrive::Compat::openLog();

    const std::string HASH        = __hyprland_api_get_hash();
    const std::string CLIENT_HASH = __hyprland_api_get_client_hash();

    if (HASH != CLIENT_HASH) {
        failNotif("Version mismatch (headers ver is not equal to running hyprland ver)");
        throw std::runtime_error("[he] Version mismatch");
    }

    g_pScrollRenderWorkspaceHook = Hyprdrive::FunctionHooks::required("main", "renderWorkspace", {"IHyprRenderer::renderWorkspace(Render::CRenderContext&, Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>,"}, rc<void*>(hkRenderWorkspace));

    g_pScrollScheduleFrameHook = Hyprdrive::FunctionHooks::required("main", "_ZN7Monitor8CMonitor13scheduleFrameEN10Aquamarine7IOutput19scheduleFrameReasonE", {""}, rc<void*>(hkScheduleFrame));

    g_pScrollDamageSurfaceHook = Hyprdrive::FunctionHooks::required("main", "damageSurface", {"CHyprRenderer::damageSurface(", "IHyprRenderer::damageSurface("}, rc<void*>(hkDamageSurface));

    g_pScrollSendFrameEventsHook = Hyprdrive::FunctionHooks::required("main", "sendFrameEventsToWorkspace", {"CHyprRenderer::sendFrameEventsToWorkspace(", "IHyprRenderer::sendFrameEventsToWorkspace("}, rc<void*>(hkSendFrameEventsToWorkspace));

    g_pScrollSurfaceFrameHook = Hyprdrive::FunctionHooks::required("main", "_ZN18CWLSurfaceResource5frameERKNSt6chrono10time_pointINS0_3_V212steady_clockENS0_8durationIlSt5ratioILl1ELl1000000000EEEEEE", {""}, rc<void*>(hkSurfaceFrame));

    g_pScrollAddDamageHookB = Hyprdrive::FunctionHooks::required("main", "addDamageEPK15pixman_region32", {"CMonitor::addDamage"}, rc<void*>(hkAddDamageB));

    g_pScrollAddDamageHookA = Hyprdrive::FunctionHooks::required("main", "_ZN7Monitor8CMonitor9addDamageERKN9Hyprutils4Math4CBoxE", {""}, rc<void*>(hkAddDamageA));

    // Hooked here and not through ensureScrollOverviewHooks(): those go on and
    // off with the overview, and Mosaic's resize and the edge springs live on
    // the plain desktop, where they are not installed.
    g_pScrollMouseMovedHook = Hyprdrive::FunctionHooks::required("main", "onMouseMoved", {"CInputManager::onMouseMoved("}, rc<void*>(hkOnMouseMoved));
    g_pScrollMouseMovedHook->hook();

    g_pMouseButtonHook = Hyprdrive::FunctionHooks::required("main", "onMouseButton", {"CInputManager::onMouseButton("}, rc<void*>(hkOnMouseButton));
    g_pMouseButtonHook->hook();

    g_pSendWindowSizeHook = Hyprdrive::FunctionHooks::required("main", "sendWindowSize", {"Layout::CWindowTarget::sendWindowSize("}, rc<void*>(hkSendWindowSize));
    g_pSendWindowSizeHook->hook();

    // OwnSizeTile's tile placement (Hooks.cpp, hkTargetPlace).
    g_pTargetPlaceHook = Hyprdrive::FunctionHooks::required("main", "setPositionGlobal", {"CWindowTarget::setPositionGlobal("}, rc<void*>(hkTargetPlace));
    g_pTargetPlaceHook->hook();
    Hyprdrive::ConfigureHold::setSender([](Desktop::View::CWindow* window, bool force) {
        if (g_pSendWindowSizeHook)
            // The window's own target: layoutTarget() is the group's for a grouped window.
            if (const auto TARGET = window->m_target)
                rc<origSendWindowSize>(g_pSendWindowSizeHook->m_original)(TARGET.get(), force);
    });
    g_pShouldRenderWindowHook = Hyprdrive::FunctionHooks::required("main", "shouldRenderWindow", {"shouldRenderWindow(Hyprutils::Memory::CSharedPointer<Desktop::View::CWindow>, Hyprutils::Memory::CSharedPointer<Monitor::CMonitor>)"}, rc<void*>(hkShouldRenderWindow));
    g_pShouldRenderWindowHook->hook();
    Hyprdrive::Compat::hookInvokeBind();

    CursorShape::init();
    OpenProf::init();
    // NOT FramePacing::apply() here: plugin keys are registered further down
    // (registerConfig), and reading an unregistered key aborts Hyprland in
    // CConfigValue::bind -- which is exactly how 2026-09-29's build crash-looped
    // every login. The config.reloaded hook and every overview open apply it.
    g_pRenderPreHook = Event::bus()->m_events.render.pre.listen([](PHLMONITOR monitor) {
        // Throttled inside to a few stat()s every couple of seconds.
        AppSearch::pollForChanges();

        if (const auto overview = scrollOverviewForMonitor(monitor))
            overview->onPreRender();

        // The desktop strip's place for this frame (DesktopStrip.hpp), then a
        // canvas desktop a swipe brings in starts past its overhang (Mosaic.cpp).
        Hyprdrive::DesktopSwitch::preRender(monitor);
        Hyprdrive::Mosaic::preRender(monitor);
        Hyprdrive::FullscreenDesktop::preRender(monitor);
        Hyprdrive::Intent::preRender(monitor);
        // After Mosaic's: a live swipe's place, for its release (DesktopStrip.hpp).
        Hyprdrive::DesktopStrip::sampleSwipe(monitor);
        // Show desktop's slide, after every module that places windows (it has
        // the last word on where they are drawn) and before the motion blur
        // reads where they went (ShowDesktop.hpp).
        Hyprdrive::ShowDesktop::preRender(monitor);

        // Layout motion blur (FlightBlur.hpp). The overview draws its own
        // windows; there, only the sweep runs.

        if (!scrollOverviewForMonitor(monitor))
            Hyprdrive::FlightBlur::trackLayoutMotion(monitor, [](const PHLWINDOW&) { return false; });
        else {
            // The overview draws its own tiles; only a special workspace (the
            // scratchpad opening or closing over it) is drawn natively, through
            // the renderer the blur rides.
            Hyprdrive::FlightBlur::trackLayoutMotion(monitor, [](const PHLWINDOW& w) { return !w->m_workspace || !Hyprdrive::Compat::isSpecial(w->m_workspace); });
        }

        // Whatever animates on a desktop nothing else repaints asks for the
        // next frame here (FramePump.cpp's SOURCES).
        if (Hyprdrive::FramePump::reason(monitor))
            Hyprdrive::FramePump::request(monitor);
    });

    // P6i1 SHADOW. Everything the compositor tells us that can change the
    // membership gate's answer: what windows exist, and the four properties the
    // gate reads off one. The plugin's own mutators -- setHidden for a flight,
    // minimize/restore, the search query -- bump at their own call sites, since
    // no event announces them.
    g_pMembOpenHook  = Event::bus()->m_events.window.open.listen([](PHLWINDOW w) {
        Membership::bump("window.open");
    });
    g_pMembLateHook  = Event::bus()->m_events.window.openLate.listen([](PHLWINDOW) { Membership::bump("window.openLate"); });
    g_pMembCloseHook = Event::bus()->m_events.window.close.listen([](PHLWINDOW w) {
        Membership::bump("window.close");
        Hyprdrive::Mosaic::onWindowClose(w); // its mosaic size, remembered per app
    });
    g_pMembKillHook  = Event::bus()->m_events.window.kill.listen([](PHLWINDOW) { Membership::bump("window.kill"); });
    g_pMembGoneHook  = Event::bus()->m_events.window.destroy.listen([](PHLWINDOWREF) { Membership::bump("window.destroy"); });
    g_pMembPinHook   = Event::bus()->m_events.window.pin.listen([](PHLWINDOW) { Membership::bump("window.pin"); });
    g_pMembFloatHook = Event::bus()->m_events.window.floating.listen([](PHLWINDOW) { Membership::bump("window.floating"); });
    g_pMembTitleHook = Event::bus()->m_events.window.title.listen([](PHLWINDOW) { Membership::bump("window.title"); });
    g_pMembClassHook = Event::bus()->m_events.window.class_.listen([](PHLWINDOW) { Membership::bump("window.class"); });
    g_pMembWsHook    = Event::bus()->m_events.window.moveToWorkspace.listen([](PHLWINDOW w, PHLWORKSPACE ws) {
        Hyprdrive::Minimize::onWindowMoved(w, ws); // B24: off the stash = not minimized
        Membership::bump("window.moveToWorkspace");
    });
    g_pMembFullHook  = Event::bus()->m_events.window.fullscreen.listen([](PHLWINDOW) { Membership::bump("window.fullscreen"); });

    g_pClickPreReloadHook = Event::bus()->m_events.config.preReload.listen([] { clearClickGestures(); });

    g_pConfigHook = Event::bus()->m_events.config.reloaded.listen([] {
        // A no-op unless the pointer set changed -- a reload is not a reason to
        // drop a touchpad gesture that is still under the hand.
        attachPadFingerListeners();
        Hyprdrive::FingerSwipe::rescan();
        // FIRST, before anything in this plugin reads a config value this pass:
        // a value the user set under the old plugin:scrolloverview: name is
        // copied onto its plugin:hyprdrive: twin here and nowhere else, so the
        // rest of the codebase only ever knows the new name. ONE RELEASE.
        Hyprdrive::Compat::adoptLegacyValues();
        // A reload writes the user's render:new_render_scheduling back.
        Hyprdrive::FramePacing::apply();
        // A reload can drop plugin-registered beziers, and may have changed the
        // animation:curve preset: point the house names at it again.
        Hyprdrive::Motion::sync();
        // The overview's tile order (GridLayout.hpp): read here, not per frame.
        OvInternal::gridTileOrderReload();
        healStuckOverviewSubmap();

        // The look can change with a reload (a theme writes Hyprland's
        // decoration keys): drop the palette and font caches so it shows on the
        // next frame.
        Hyprdrive::Config::invalidateThemeCache();

        // The reload just overwrote the overrides an OPEN overview installed
        // (workspace animations, follow_mouse, no_warps) -- without this, a
        // workspace switch inside the overview plays Hyprland's slide again.
        // Only the shared-state owner acts; the method self-guards.
        for (const auto& OVERVIEW : scrollOverviews()) {
            if (const auto SCROLL = OVERVIEW ? dynamic_cast<COverview*>(OVERVIEW.get()) : nullptr)
                SCROLL->reapplyConfigOverridesAfterReload();
        }
    });

    g_pPointerMoveHook = Event::bus()->m_events.input.mouse.move.listen([](Vector2D, Event::SCallbackInfo& info) {
        const auto HIT = monitorUnderCursor();

        // The scratchpad's drag-out zone rides the same motion.
        Hyprdrive::ScratchpadExit::onPointerMove(HIT ? HIT->first : PHLMONITOR{});
        // Mosaic's corner resize: the resize cursor over a grabbable corner.
        Hyprdrive::Mosaic::onMove(!HIT || g_pSessionLockManager->isSessionLocked());
        // Through the bar: the window under it takes focus (Mosaic.hpp).
        if (HIT && !g_pSessionLockManager->isSessionLocked())
            Hyprdrive::Mosaic::focusUnderBar();
        // The zoomed-out canvas owns the pointer: no window under its full-size
        // place gets the motion or focus while it lasts.
        if (Hyprdrive::Mosaic::zoomOwnsPointer())
            info.cancelled = true;
    });

    g_pPointerButtonHook = Event::bus()->m_events.input.mouse.button.listen([](IPointer::SButtonEvent event, Event::SCallbackInfo& info) {
        if (handleClickGestureButton(event)) {
            info.cancelled = true;
            return;
        }
        // Mosaic's corner resize (Mosaic.hpp). Not over an open overview
        // (monitorUnderCursor skips those monitors), not while locked.
        const auto HIT = monitorUnderCursor();
        if (Hyprdrive::Mosaic::onButton(event, !HIT || g_pSessionLockManager->isSessionLocked()))
            info.cancelled = true;
    });

    g_pDesktopRenderHook = Event::bus()->m_events.render.stage.listen([](Event::SRenderStageEvent e) {
        if (e.stage != RENDER_POST_WINDOWS || scrollOverviewForMonitor(e.monitor))
            return;
        Hyprdrive::Mosaic::renderZoomLabels(e.monitor);
    });

    // The scratchpad's drag-out zone draws at the LAST moment: it sits at the
    // very top of the screen, where the bar lives, and POST_WINDOWS is below
    // the top layer, so the bar would paint over it.
    g_pScratchExitRenderHook = Event::bus()->m_events.render.stage.listen([](Event::SRenderStageEvent e) {
        if (e.stage != RENDER_LAST_MOMENT)
            return;
        const auto MONITOR = e.monitor;
        if (!MONITOR || scrollOverviewForMonitor(MONITOR))
            return;
        Hyprdrive::ScratchpadExit::render(MONITOR);
    });

    // P0. An overview must not outlive its output. Nothing used to tear one
    // down when its monitor went away, so it stayed in the registry holding a
    // dead PHLMONITORREF and the next `overview off` dereferenced it inside
    // close() -- a SIGSEGV, which gdm then papered over by bringing the session
    // straight back WITHOUT the user's config, so the damage outlived the crash.
    // `hyprctl output remove` and a user unplugging a display raise this same
    // event, and it is the one Hyprland's own monitor-reference holders listen
    // to. It is deliberately NOT emitted while the compositor is shutting down,
    // which is what keeps this from queueing a deferred destroy that would never
    // run -- PLUGIN_EXIT owns teardown there.
    //
    // Before the desktop is drawn, not after: corrected on the first frame it
    // would slide the window across the screen instead of simply being right.
    // See normalizeFittingScrollingOffset.
    g_pScrollCameraActiveHook = Event::bus()->m_events.workspace.active.listen([](PHLWORKSPACE workspace) { normalizeScrollingCamera(workspace); });
    g_pScrollCameraMovedHook  = Event::bus()->m_events.workspace.moveToMonitor.listen([](PHLWORKSPACE workspace, PHLMONITOR) { normalizeScrollingCamera(workspace); });
    // And whatever is already stuck when the plugin comes up.
    normalizeAllScrollingCameras();

    g_pMonitorGoneHook = Event::bus()->m_events.monitor.removed.listen([](PHLMONITOR monitor) {
        dropScrollOverviewsForMonitor(monitor);
        if (monitor) {
            if (const auto OPENGL = g_pHyprRenderer ? g_pHyprRenderer->glBackend() : WP<Render::GL::CHyprOpenGLImpl>{})
                OPENGL->makeEGLCurrent();
            Hyprdrive::releaseWallpaperStash(monitor->m_id);
        }
    });

    Hyprdrive::Config::registerDispatcher("overview", ::onOverviewDispatcher);
    Hyprdrive::Config::registerDispatcher("navigate", ::onNavigateDispatcher);
    Hyprdrive::Config::registerDispatcher("window", ::onWindowDispatcher);
    Hyprdrive::Config::registerDispatcher("gesture", ::onGestureDispatcher);
    Hyprdrive::Config::registerDispatcher("debug", ::onDebugDispatcher);
    // Open the settings: the Hyprdrive page of Ryoku Settings (SettingsApp.hpp).
    Hyprdrive::Config::registerDispatcher("settings", Hyprdrive::SettingsApp::dispatch);
    // Super+scroll's next / previous desktop (DesktopSwitch.hpp).
    Hyprdrive::Config::registerDispatcher("desktop", Hyprdrive::DesktopSwitch::dispatch);
    Hyprdrive::Config::registerDispatcher("show_desktop", Hyprdrive::ShowDesktop::dispatch);
    Hyprdrive::Config::registerGesture(::onRegisterOverviewGesture, ::overviewGestureKeyword);
    Hyprdrive::Config::registerConfig();
    // Its keys exist now: the overview's tile order (also on every reload).
    OvInternal::gridTileOrderReload();
    // Plain function pointers: SearchBackend.cpp is linked into host tests that
    // have no Config (SearchBackend.hpp SHost).
    SearchBackend::setHost({
        .terminalTemplate = [] { return Hyprdrive::Config::getSearchTerminal(); },
        .appTerminal      = [] { return Hyprdrive::Config::getTerminal(); },
    });
    // A search never starts a GNOME search provider's app behind the user's
    // back: a running one still answers (SearchProviders.hpp setActivation).
    SearchProviders::setActivation([] { return false; });
    // The desktop's look, read from the Lithic bridge (Look.hpp).
    Hyprdrive::Look::init();
    // `hyprctl hyprdrive-binds` / `hyprdrive-theme` (SettingsApp.hpp).
    Hyprdrive::SettingsApp::init();
    // The "mosaic" layout (Mosaic.hpp). Registered whether or not the panel has
    // enabled it: a desktop whose saved layout is mosaic must find it at login.
    Hyprdrive::Mosaic::registerLayout(HYPRDRIVE_HANDLE);
    Hyprdrive::Intent::init();
    Hyprdrive::GameApps::init();
    // Which app each window is (IdentityLive.hpp): its worker builds the
    // Steam and Lutris indexes and resolves every window off this thread.
    Hyprdrive::AppIdentity::init();
    // Each monitor's row of desktops (Ryoku's blocks of ten): before anything
    // renumbers.
    Hyprdrive::Rows::init();
    Hyprdrive::FullscreenDesktop::init();
    Hyprdrive::ShowDesktop::init();
    // Super+T on a fixed-size window: a tile at its own size (OwnSizeTile.hpp).
    Hyprdrive::OwnSizeTile::init();
    // Where every new window goes: the one openEarly decision (Placement.hpp).
    // After Intent, FullscreenDesktop and OwnSizeTile, which it calls.
    Hyprdrive::WindowPlacement::init();
    // Super+F / Super+W on the hovered overview tile (HoverActions.hpp).
    Hyprdrive::HoverActions::init();
    // The animation:curve preset onto Hyprland's named house beziers -- only
    // now that the key exists (Config.hpp: an earlier read aborts Hyprland).
    Hyprdrive::Motion::sync();
    // A previous instance unloaded mid-overview can leave Hyprland in its submap.
    healStuckOverviewSubmap();
    // After a reload, windows are still parked on the stash with no ledger entry
    // behind them; without this they can never be restored.
    Hyprdrive::Minimize::adoptStashedWindows();
    registerRawKeyHooks();
    if (g_pSessionLockManager)
        g_pSessionLockHook = g_pSessionLockManager->m_events.lock.listen([] { onSessionLock(); });
    attachPadFingerListeners();
    // Both read their keys at once: only after registerConfig() above.
    Hyprdrive::FingerSwipe::init();
    Hyprdrive::SpringLoad::init();
    Hyprdrive::HotCorner::init();
    Hyprdrive::DesktopSwitch::init();
    Hyprdrive::XGeometry::installStateRequestFix();
    Hyprdrive::Ripple::init();
    Hyprdrive::FlightBlur::init();

    // Reload can drop live window alphas until decorations are recomputed.
    if (Desktop::globalWindowController())
        Desktop::globalWindowController()->updateAllWindowsDecorations();

    if (g_pCompositor && g_pCompositor->m_wlEventLoop) {
        g_fileWakeFd = eventfd(0, EFD_CLOEXEC | EFD_NONBLOCK);
        if (g_fileWakeFd >= 0) {
            g_fileWakeSource = wl_event_loop_add_fd(g_pCompositor->m_wlEventLoop, g_fileWakeFd, WL_EVENT_READABLE, onFileResultsReady, nullptr);
            SearchBackend::setResultsCallback([]() {
                if (g_fileWakeFd >= 0) {
                    uint64_t one = 1;
                    ::write(g_fileWakeFd, &one, sizeof(one));
                }
            });
            // The dictionary/translator answers off-thread too; same wake.
            ToolSearch::setReadyCallback([]() {
                if (g_fileWakeFd >= 0) {
                    uint64_t one = 1;
                    ::write(g_fileWakeFd, &one, sizeof(one));
                }
            });
        }
    }

    // X51/X52. Build the icon index at LOAD, where a one-off ~10ms pass is
    // invisible, rather than letting the first search pay for it inside a frame
    // that is already trying to paint results.
    // Y13. ASYNC. Building this inline cost 4237ms of PARSE TIME, which is
    // time the compositor spends before exec-once and therefore before the
    // wallpaper can even be asked for.
    AppSearch::warmIconIndexAsync();
    // Decode icons ahead of the first open / first keystroke (OverviewRender.hpp).
    OverviewRender::startIconWarmer();

    // LAST: a std::terminate while the plugin is loaded leaves its exception
    // and a backtrace in $XDG_STATE_HOME/hyprdrive/terminate.log, then aborts
    // as before (TerminateLog.hpp). Not earlier: an init that throws is
    // dlclosed with no PLUGIN_EXIT, which would leave the handler in unmapped code.
    Hyprdrive::TerminateLog::install();

    return {Hyprdrive::Compat::NAME, "Mission-control overview, Mosaic and window motion for Ryoku", "Vaxry, yayuuu, dragos, Ryoku", SCROLLOVERVIEW_VERSION};
}

APICALL EXPORT void PLUGIN_EXIT() {
    // Before any module's shutdown: no new worker starts from here on, every
    // worker is asked to stop, and the joins below share one deadline
    // (Workers.hpp: one that misses it is left running in code kept loaded).
    Hyprdrive::Workers::beginShutdown();
    // First: no window may stay hidden behind a plugin that is gone.
    Hyprdrive::ShowDesktop::shutdown();
    Hyprdrive::WindowPlacement::shutdown();
    Hyprdrive::OwnSizeTile::shutdown();
    Hyprdrive::Look::shutdown(); // the bridge process, its pipe and timer
    Hyprdrive::SettingsApp::shutdown(); // its hyprctl handlers live in this .so
    // FIRST: Hyprland hands every mosaic desktop back to its default layout and
    // destroys our algorithm objects -- while their code is still mapped.
    Hyprdrive::Intent::shutdown();
    Hyprdrive::FullscreenDesktop::shutdown();
    Hyprdrive::Rows::shutdown(); // its inotify fd, timer and listeners
    Hyprdrive::AppIdentity::shutdown();
    Hyprdrive::GameApps::shutdown();
    Hyprdrive::Mosaic::shutdown();
    Hyprdrive::Mosaic::unregisterLayout(HYPRDRIVE_HANDLE);
    Hyprdrive::KeyboardCat::shutdown(); // ffmpeg killed, its reader joined, before the code goes
    Hyprdrive::BorderFade::clear(); // drops its hold on Hyprland's `border` bezier
    stopSearchRepeat();
    if (g_searchRepeatTimer) {
        wl_event_source_remove(g_searchRepeatTimer);
        g_searchRepeatTimer = nullptr;
    }
    if (g_letterTickTimer) {
        wl_event_source_remove(g_letterTickTimer);
        g_letterTickTimer = nullptr;
    }
    SearchBackend::setResultsCallback(nullptr);
    ToolSearch::shutdown();
    if (g_fileWakeSource) {
        wl_event_source_remove(g_fileWakeSource);
        g_fileWakeSource = nullptr;
    }
    // Not while a worker abandoned above may still write to it (Workers.hpp).
    if (g_fileWakeFd >= 0 && Hyprdrive::Workers::abandoned() == 0) {
        close(g_fileWakeFd);
        g_fileWakeFd = -1;
    }
    g_pRawKeyHook.reset();
    g_pSessionLockHook.reset();
    g_pTapSuperButtonHook.reset();
    g_pTapSuperAxisHook.reset();
    g_pRenderPreHook.reset();
    CursorShape::shutdown();
    Hyprdrive::FramePacing::shutdown();
    OpenProf::shutdown();
    OvInternal::cancelPointerHandoff();
    g_pMembOpenHook.reset();
    g_pMembLateHook.reset();
    g_pMembCloseHook.reset();
    g_pMembKillHook.reset();
    g_pMembGoneHook.reset();
    g_pMembPinHook.reset();
    g_pMembFloatHook.reset();
    g_pMembTitleHook.reset();
    g_pMembClassHook.reset();
    g_pMembWsHook.reset();
    g_pMembFullHook.reset();
    g_pConfigHook.reset();
    g_pPointerMoveHook.reset();
    g_pPointerButtonHook.reset();
    g_pDesktopRenderHook.reset();
    g_pScratchExitRenderHook.reset();
    g_pMonitorGoneHook.reset();
    g_pScrollCameraActiveHook.reset();
    g_pScrollCameraMovedHook.reset();
    // The config.preReload listener: HyprlandAPI::reloadConfig() at the end of
    // this function would otherwise still call into it (clearClickGestures)
    // with the plugin half torn down.
    g_pClickPreReloadHook.reset();

    g_unloading = true;
    // The D-Bus worker outlives the overview otherwise, and hyprpm reloads the
    // plugin in-process.
    AppSearch::shutdownIconIndex();
    SearchProviders::shutdown();
    SearchBackend::shutdown();
    clearScrollOverviews();
    // AGAIN, after the overviews are gone: their teardown hands focus back and
    // can queue work (a cursor warp) after the first round of cancels above -- a doLater left in Hyprland's event loop
    // is a call into unloaded code when that loop is destroyed.
    OvInternal::cancelPointerHandoff();
    // Minimized windows sit on special:minimized, which is never shown: with
    // the plugin gone nothing could bring them back (they stayed there until a
    // reload re-adopted them). Before ConfigureHold::flush below, since the
    // restore holds configures.
    Hyprdrive::Minimize::restoreAllForUnload();
    // A moving window may hold the motion blur's transformer; its destructor is
    // this plugin's code, so it goes before the dlclose (Hyprland's own exit too).
    Hyprdrive::FlightBlur::shutdown();
    // No window may keep a transformer whose code is about to be unmapped.
    Hyprdrive::FlightBlur::detachAll();
    OverviewRender::stopIconWarmer();
    // Nothing may stay held once the hook goes with the plugin.
    Hyprdrive::ConfigureHold::flush();
    Hyprdrive::XGeometry::shutdown();
    // Outlives every overview on purpose (Window.cpp), so it is freed here.
    if (const auto OPENGL = g_pHyprRenderer ? g_pHyprRenderer->glBackend() : WP<Render::GL::CHyprOpenGLImpl>{})
        OPENGL->makeEGLCurrent();
    OverviewWindow::releaseTexturePrefilterCache();
    Hyprdrive::releaseWallpaperStash(std::nullopt);
    // The text textures outlive a close now (X51), so unload is where they go --
    // same moment the last overview's destructor used to drop them, and hyprpm
    // reloads this plugin in-process.
    OverviewRender::clearTextCache();
    // The drag-out zone's drift timer and stripe textures: the timer would fire
    // into unloaded code, and the textures need the context made current above.
    Hyprdrive::ScratchpadExit::shutdown();
    shutdownClickGestures();
    disableScrollOverviewHooks();

    // Before the gesture teardown below: a swipe the plugin is driving ends
    // through it, and the spring timer must not fire into unloaded code.
    Hyprdrive::SpringLoad::shutdown();
    Hyprdrive::HotCorner::shutdown();
    Hyprdrive::DesktopSwitch::shutdown();
    Hyprdrive::DebugGesture::shutdown();
    Hyprdrive::FingerSwipe::shutdown();

    if (g_pTrackpadGestures)
    {
        // The swipe in progress first: clearGestures() does not touch it, and it
        // may be one of ours (session abort mid-swipe, 2026-09-29).
        Hyprdrive::endActiveTrackpadGesture();
        g_pTrackpadGestures->clearGestures();
    }

    // LAST, and it is the difference between unloading and killing the session.
    //
    // The types below are defined IN THIS SHARED OBJECT (OverviewPassElement.hpp).
    // A queued one that outlives the dlclose leaves Hyprland's render pass holding a
    // vtable in freed memory, and the next frame's CRenderPass::clear() runs its
    // destructor through that vtable. `hyprctl plugin unload` SEGV'd the compositor
    // twice on 2026-09-12 doing exactly this -- both cores identical, and both name
    // it outright: beginRender -> Render::CRenderPass::clear() -> a call to an
    // address in no mapped region, with hyprdrive.so already gone from the map.
    //
    // Hyprland cannot know these types, so the plugin has to take them back out
    // before its code goes away. The trackpad gestures above are the same hazard,
    // and clearGestures() is what already answers it for them.
    if (g_pHyprRenderer) {
        for (const char* TYPE : {"COverviewBlurPassElement", "COverviewSplitBorderPassElement", "COverviewShadowPassElement", Hyprdrive::Ripple::PASS_NAME})
            g_pHyprRenderer->context().m_pass.removeAllOfType(TYPE);
    }

    // After the purge: the ripple's GL program has to go while this object's
    // code is still mapped.
    Hyprdrive::Ripple::shutdown();

    // Whatever no module joined, within the same deadline. No
    // thread of ours may outlive this function in code about to be unmapped.
    Hyprdrive::Workers::finishShutdown();

    HyprlandAPI::reloadConfig(); // re-adds built-in gestures cleared above

    // The very last thing: a terminate anywhere in the exit above is still
    // logged. The previous handler goes back unless another was installed
    // over ours meanwhile, which is then left as it is (and said so).
    if (!Hyprdrive::TerminateLog::uninstall())
        Log::logger->log(Log::ERR, "[hyprdrive] another std::terminate handler was installed over hyprdrive's: left in place, and the plugin's code kept loaded for it");
}
