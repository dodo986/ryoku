import QtQuick
import Lithic
import Ryoku.Ui.Singletons
import "../../Singletons"

// the canvas zoom as a shortcut: the key you hold while the pointer moves, and Turn
// off / Turn on. Holding a key alone is not a chord a field can record, so the key is
// a choice of the four modifiers.
Row {
    id: root

    // set by RowControl: the row (mosaic:super_zoom), its on/off value and its commit
    property var row: null
    property var value: undefined
    property var commit: null

    readonly property bool on: root.value === true
    readonly property var keyBind: ({ store: "desktop", key: "wm.hyprland.plugins.hyprdrive.config.hyprdrive:mosaic:zoom_key" })
    readonly property var keys: ["super", "alt", "ctrl", "shift"]
    readonly property string held: {
        var v = Changes.value(root.keyBind);
        return root.keys.indexOf(v) >= 0 ? v : "super";
    }

    spacing: LithicTokens.space.s2

    function activate() {
        if (root.commit)
            root.commit(!root.on);
    }

    Segmented {
        anchors.verticalCenter: parent.verticalCenter
        enabled: root.on
        model: [I18n.tr("Super"), I18n.tr("Alt"), I18n.tr("Ctrl"), I18n.tr("Shift")]
        currentIndex: root.keys.indexOf(root.held)
        accessibleName: I18n.tr("Key to hold for the canvas zoom")
        onActivated: index => Changes.set(root.keyBind, root.keys[index], I18n.tr("Key for the canvas zoom"), "hyprdrive")
    }

    Plaque {
        anchors.verticalCenter: parent.verticalCenter
        kind: "quiet"
        small: true
        text: root.on ? I18n.tr("Turn off") : I18n.tr("Turn on")
        accessibleName: (root.on ? I18n.tr("Turn off %1") : I18n.tr("Turn on %1")).arg(I18n.tr(root.row ? root.row.label : ""))
        onClicked: root.activate()
    }
}
