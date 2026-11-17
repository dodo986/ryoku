.pragma library

var title = "Hyprdrive";
var lead = "An overview of every window, Mosaic layouts and smooth window motion, drawn in the desktop's own style.";

// the window manager's rows fill every section but the shortcuts one, which the page
// builds from what Hyprdrive would bind and what each would take over
var sections = [
    { id: "on", title: "Hyprdrive" },
    { id: "keys", title: "Shortcuts it adds" },
    { id: "overview", title: "Overview" },
    { id: "hot-corner", title: "Hot corner" },
    { id: "search", title: "Search" },
    { id: "mosaic", title: "Mosaic" },
    { id: "canvas", title: "Infinite canvas" },
    { id: "windows", title: "Windows" },
    { id: "desktops", title: "Switching desktops" },
    { id: "motion", title: "Motion" },
    { id: "motion-blur", title: "Motion blur" }
];

function d(key) {
    return { store: "desktop", key: key };
}

var enabledBind = d("wm.hyprland.plugins.hyprdrive.enabled");

// chords as the shortcuts page writes them: "SUPER + D" reads Super+D
function chordText(chord) {
    return String(chord || "").split("+").map(function (part) {
        var k = part.trim();
        if (k === "mouse_down")
            return "Wheel down";
        if (k === "mouse_up")
            return "Wheel up";
        return k.length > 1 ? k.charAt(0) + k.slice(1).toLowerCase() : k.toUpperCase();
    }).join("+");
}

// one shortcut row from the window manager's status: its keys, and the Ryoku shortcut it
// would take over. One that takes another over is off until you turn it on.
function bindRow(b) {
    var keys = (b.chords || []).map(chordText).join(", ");
    var detail = b.detail;
    if (keys.length > 0)
        detail = keys + ". " + detail;
    var over = (b.conflicts || []).map(function (c) {
        return chordText(c.chord) + " (" + c.label + ")";
    });
    if (over.length > 0)
        detail += ". Takes over " + over.join(" and ") + " from Ryoku";
    return {
        id: "bind-" + b.id, section: "keys", label: b.label, detail: detail,
        keywords: ["shortcut", "keybind", "takes over", "conflict"].concat((b.chords || []).map(chordText)),
        control: "toggle", bind: d("wm.hyprland.plugins.hyprdrive.binds." + b.id), "default": b.on === true,
        takesOver: over.length > 0
    };
}

// what the state line says about the shortcuts while Hyprdrive is off and once it is on
function takeoverLine(status, on) {
    var taken = (status && status.takesOver) || [];
    var waiting = ((status && status.binds) || []).filter(function (b) {
        return !b.on && (b.conflicts || []).length > 0 && !b.decided;
    }).length;
    var parts = [];
    if (taken.length > 0) {
        var names = taken.map(function (c) { return chordText(c.chord) + " (" + c.label + ")"; });
        parts.push((on ? "Hyprdrive has taken over " : "Turning Hyprdrive on takes over ") + names.join(", ") + ".");
    }
    if (waiting > 0)
        parts.push(waiting === 1 ? "One shortcut would replace a Ryoku one and stays off until you turn it on."
                                 : waiting + " shortcuts would replace Ryoku ones and stay off until you turn them on.");
    return parts.join(" ");
}

var rows = [];

var related = ["window-manager", "shortcuts", "animations", "window-behaviour"];
