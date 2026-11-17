import QtQuick
import Quickshell.Io
import Lithic
import Ryoku.Ui.Singletons
import ".."
import "../Singletons"
import "../components"
import "../schema/hyprdrive.js" as Schema

// Hyprdrive: its switch, every shortcut it would add with the Ryoku one it would take
// over, and its settings, which the window manager lists
Item {
    id: root

    required property var hub

    // `ryoku-hub desktop hyprdrive status`: the binds, their conflicts and what is taken
    property var status: null

    function focusRow(rowId) {
        page.focusRow(rowId);
    }

    readonly property bool on: Changes.value(Schema.enabledBind) === true

    readonly property var schema: {
        Changes.revision;
        var binds = (root.status && root.status.binds) || [];
        return {
            title: Schema.title,
            lead: Schema.lead,
            sections: Schema.sections,
            related: Schema.related,
            rows: Schema.rows.concat(binds.map(Schema.bindRow))
        };
    }

    readonly property string stateLine: {
        if (!root.status)
            return "";
        if (root.on && root.status.installed === false)
            return I18n.tr("Hyprdrive is not built for this version of Hyprland yet. Rebuild it on Window manager.");
        var line = I18n.tr(Schema.takeoverLine(root.status, root.on));
        if (!root.on)
            line = (line ? line + " " : "") + I18n.tr("Its settings, Mosaic and the infinite canvas among them, show once it is on.");
        return line;
    }

    SettingsPage {
        id: page
        anchors.fill: parent
        hub: root.hub
        pageId: "hyprdrive"
        schema: root.schema
        stateLine: root.stateLine
        bodies: ({ keys: keysLead })
        rimLight: true
    }

    // the status follows every change: a rebind on Keyboard shortcuts frees or takes a chord
    Connections {
        target: Changes
        function onRevisionChanged() {
            refresh.restart();
        }
    }
    Timer {
        id: refresh
        interval: 400
        onTriggered: statusProc.running = true
    }
    Component.onCompleted: statusProc.running = true

    Process {
        id: statusProc
        command: ["ryoku-hub", "desktop", "hyprdrive", "status"]
        stdout: StdioCollector {
            onStreamFinished: {
                try {
                    root.status = JSON.parse(this.text);
                } catch (e) {
                    root.status = null;
                }
            }
        }
    }

    Component {
        id: keysLead
        Label {
            width: parent ? parent.width : 0
            text: I18n.tr("Each shortcut Hyprdrive adds, with the Ryoku shortcut it would replace. One that replaces a Ryoku shortcut stays off until you turn it on; turning Hyprdrive off gives every one back.")
            typeRole: "detail"
            inkRole: "fg2"
            wrapMode: Text.Wrap
            elide: Text.ElideNone
        }
    }
}
