#pragma once

#include <cstdint>
#include <string>
#include <vector>
#include <optional>
#include <functional>

namespace SearchBackend {

enum class EItemKind : uint8_t {
    App,
    Action,      // a menu action or command
    Menu,        // a submenu (drills down into route)
    Back,        // Back to parent menu
    Agent,       // AI Coding Agent chat launcher (/grok, /agy, etc.)
    Calculator,  // Instant arithmetic calculation
    File,        // File search result
    Provider,    // D-Bus search provider
};

struct SSearchItem {
    EItemKind   kind = EItemKind::Action;
    std::string id;
    std::string name;
    std::string description;
    std::string icon;
    std::string iconFont;     // nerd-font family for menu glyphs; empty = the detected icon font (OverviewRender::glyph)
    std::string category;     // "Apps", "Menu", "System", "AI Agent", "Calculator", "Files", "Settings"
    std::string actionCommand;// shell command or executable
    std::string agentPrompt;  // an agent row's prompt: written to a 0600 file at activate(), never into actionCommand
    std::string filePath;     // absolute path for files
    uint64_t    fileSizeBytes = 0;
    std::string fileSizeStr;
    std::string fileModifiedStr;
    std::string fileTypeStr;  // e.g. "PDF Document", "C++ Source", "PNG Image"
    bool        canQuickLook = false;
};

// Lifecycle
void init();
void shutdown();

// WHAT THIS FILE ASKS THE PLUGIN. SearchBackend.cpp is linked into a host test
// with no Hyprland in it, so main.cpp hands it these at PLUGIN_INIT and the test
// its own. Either may be null.
//
//   terminalTemplate  plugin:hyprdrive:search:terminal -- the terminal an agent
//                     opens in ("" = detect, NativeSearch.hpp).
//   appTerminal       plugin:hyprdrive:terminal, the Terminal=true one; the
//                     detection's second choice.
struct SHost {
    std::string (*terminalTemplate)() = nullptr;
    std::string (*appTerminal)()      = nullptr;
};
void setHost(const SHost& host);

// "System": column 0's title at the menu root.
std::string menuRootTitle();

// Forget every cached "is this program installed?" answer now rather than at
// the end of its few-second window (SearchBackend.cpp, resolveCached). For
// tests that change PATH, and harmless anywhere else.
void invalidateProgramLookups();

// Keystroke query across every provider
void query(const std::string& text);

// Provider queries
bool isAgentQuery(const std::string& query);
std::vector<SSearchItem> searchAgents(const std::string& query);
// Logged-in agents only, with the typed sentence as the prompt.
std::vector<SSearchItem> promptAllAgents(const std::string& prompt);
bool                     isGlyphIcon(const std::string& icon);
std::vector<SSearchItem> agentHelpItems();

// THE DEFAULT CODING AGENT. Its rows lead the AI & Tools column; Ctrl x2 over
// an agent row sets it (overviewHandleCtrlTap).
//   It lives in ${XDG_CONFIG_HOME:-~/.config}/hyprdrive/default-agent, one
//   line, the agent's prefix ("claude", "agy"...).
bool canBeDefaultAgent(const SSearchItem& item);
bool isDefaultAgent(const SSearchItem& item);
bool setDefaultAgent(const SSearchItem& item);
std::optional<SSearchItem> evaluateCalculator(const std::string& query);
// A prompt for an agent is typed into it by agent-run, never passed as an
// argument (SearchBackend.cpp, A PROMPT IS TYPED). Host-tested.
std::string agentRunner();
std::string writePromptFile(const std::string& prompt);
std::string withPromptFile(const std::string& command, const std::string& path);
std::vector<SSearchItem> searchMenu(const std::string& query, size_t limit = 10);
std::vector<SSearchItem> fileResults();

// One line of fd's output, split into what a Files & Folders row shows. fd
// prints a DIRECTORY with a trailing slash (`/home/u/Documents/`), and cutting
// that at its last '/' gave an empty name and the folder itself as its parent:
// every folder hit was an icon with no text (D6a). Trailing slashes go first;
// `path` is what is opened, `name` the row's title, `dir` its subtitle.
struct SFoundPath {
    std::string path, dir, name;
};
SFoundPath splitFoundPath(std::string line);
bool fileSearchBusy();

// EVERYTHING THE ROW BUILDER READS THAT IS NOT THE QUERY, as one number.
//
// Building a row set costs a scored sort of the whole app inventory, two
// stat()s for the menu, a stat() and two fs::exists per agent and a copy of the
// file results under a mutex -- and the overview asks for it on every monitor
// on every frame while a query is up, for an answer that changes only when one
// of those files does. This is the key a caller memoises on.
//
// The filesystem half is re-read at most every 250ms, which is also the ceiling
// on how long an edit of the default-agent file takes to show up.
// A Ctrl x2 in the search field writes the file from THIS process and moves the
// generation at once (setDefaultAgent), so the selection can follow the agent. The file-search worker's own generation
// is folded in unpolled, so an async result lands on the next frame.
uint64_t searchInputsGeneration();
void setResultsCallback(std::function<void()> cb);

// Menu hierarchy navigation
void navigateMenu(const std::string& routeId);
bool navigateMenuBack();
bool canNavigateMenuBack();
std::string menuCurrentRoute();
std::string menuCurrentRouteTitle();
void resetMenuRoute();

// Activate an item (runs command, launches agent, copies calc, or opens file)
bool activate(const SSearchItem& item);

// Quick Look (Sushi) state and actions
bool isSushiActive();
void toggleSushi(const std::string& filePath);
void closeSushi();
void updateSushiIfActive(const std::string& filePath);
bool isOverviewSushiWindow(const std::string& windowClass, uint32_t pid = 0);
std::vector<SSearchItem> recentFiles();

} // namespace SearchBackend
