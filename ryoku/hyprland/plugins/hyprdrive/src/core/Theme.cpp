#include "ConfigParts.hpp"
#include "Look.hpp"
using namespace ConfigDetail;

namespace Hyprdrive::Config {

using namespace ConfigPart;

// Every colour the plugin draws is a Lithic role of the desktop's look
// (Look.hpp, docs/lithic/design.md#3-materials-roles-not-colours). Nothing here
// names a colour, and nothing offers a picker of its own: the reader chose once,
// in Settings > Appearance > Style.

void invalidateThemeCache() {
    ++g_themeGeneration;
}

uint64_t themeGeneration() {
    return g_themeGeneration;
}

static const Look::SRoles& roles() {
    return Look::current().roles;
}

bool isLightTheme() {
    return !Look::current().dark;
}

CHyprColor themeForeground() {
    return roles().fg;
}

// fg2 is the working ink: details, counts, the quiet half of a row.
CHyprColor themeMutedText() {
    return roles().fg2;
}

CHyprColor themeFaintText() {
    return roles().fg3;
}

// sel is a mark of place only: what is selected, focused or being read.
CHyprColor themeAccent() {
    return roles().sel;
}

CHyprColor themeHeading() {
    return roles().fg;
}

CHyprColor themeSelection() {
    return roles().sel;
}

// The chosen row wears the container wash with its own ink, as the Hub's tree
// does (Tone.select); no ring, no left bar.
CHyprColor themeSelectedFill() {
    return roles().container;
}

CHyprColor themeSelectedRing() {
    return CHyprColor{0.F, 0.F, 0.F, 0.F};
}

CHyprColor themeSelectedText() {
    return roles().onContainer;
}

CHyprColor themeUrgent() {
    return roles().jasper;
}

CHyprColor themeAction() {
    return roles().gold;
}

CHyprColor themeActionInk() {
    return roles().onGold;
}

CHyprColor highlightColor() {
    return roles().sel;
}

CHyprColor restingBorderColor() {
    return roles().vein;
}

CHyprColor highlightForeground() {
    return roles().onSel;
}

CHyprColor chromePlate() {
    return Look::current().planes.pane;
}

CHyprColor chromeStroke() {
    return Look::current().planes.paneStroke;
}

CHyprColor chromeCard() {
    return Look::current().planes.card;
}

CHyprColor chromeCardStroke() {
    return Look::current().planes.cardStroke;
}

CHyprColor chromeOverlay() {
    return roles().stone4;
}

CHyprColor chromeScrim() {
    return roles().scrim;
}

float chromeCorner(float cut, float w, float h) {
    return Look::corner(cut, w, h);
}

float chromeCornerPower() {
    return Look::cornerPower();
}

// The cards are the windows themselves, so they keep the window's own corner.
int themeRounding() {
    static auto& VAL = valueRef<Hyprlang::INT>("decoration:rounding");
    return std::max<int>(0, sc<int>(*VAL));
}

float themeRoundingPower() {
    static auto& VAL   = valueRef<Hyprlang::FLOAT>("decoration:rounding_power");
    const auto   POWER = sc<float>(*VAL);
    return std::clamp(POWER > 0.F ? POWER : 2.F, 1.F, 10.F);
}

// Geologica for everything a person reads (docs/lithic/design.md#4-type).
const std::string& themeFontFamily() {
    return Look::current().sans;
}

// Martian Mono for data and numbering.
const std::string& themeMonoFamily() {
    return Look::current().mono;
}

const std::string& glyphFontSetting() {
    static std::string value{"auto"};
    static uint64_t    seen = 0;
    if (seen != g_themeGeneration && g_pluginKeysRegistered) {
        seen  = g_themeGeneration;
        value = getValue<std::string>("plugin:hyprdrive:glyph_font");
        if (value.empty() || value == STRVAL_EMPTY)
            value = "auto";
    }
    return value;
}

}
