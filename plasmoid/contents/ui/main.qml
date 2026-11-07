import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import QtQuick.Dialogs as QtDialogs
import QtQuick.Window
import org.kde.plasma.plasmoid
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.extras as PlasmaExtras
import org.kde.plasma.plasma5support as P5Support
import org.kde.kirigami as Kirigami

import "formats.js" as Formats

PlasmoidItem {
    id: root

    readonly property var history: Plasmoid.configuration.history
    readonly property string lastColor: history.length > 0 ? history[0] : ""
    property bool picking: false

    Plasmoid.icon: "color-picker"
    toolTipMainText: i18n("Glass Color Picker")
    toolTipSubText: lastColor ? i18n("Last: %1", format(lastColor)) : i18n("Click the pipette to pick a color")

    // ---- color formatting (history is stored as #RRGGBB) ----
    function format(hex, fmt) { return Formats.format(hex, fmt || Plasmoid.configuration.format) }

    function shellQuote(s) { return "'" + String(s).replace(/'/g, "'\\''") + "'" }

    function copy(text) { runner.connectSource("wl-copy -- " + shellQuote(text)) }

    function pick() {
        if (picking)
            return
        picking = true
        root.expanded = false
        runner.connectSource(Plasmoid.configuration.pickerCommand
                             + " --stdout-hex --format " + shellQuote(Plasmoid.configuration.format)
                             + (Plasmoid.configuration.autoCopy ? "" : " --no-copy")
                             + " --mode " + shellQuote(Plasmoid.configuration.pickerMode)
                             + " --sampling " + shellQuote(Plasmoid.configuration.sampling)
                             + " --print-state --magnifier " + (Plasmoid.configuration.magnifier ? "on" : "off")
                             + " --zoom " + Plasmoid.configuration.zoom
                             + " --badge " + (Plasmoid.configuration.badge ? "on" : "off")
                             + " #pick" + Date.now())
    }

    function remember(hex) {
        const h = history.filter(c => c !== hex)
        h.unshift(hex)
        Plasmoid.configuration.history = h.slice(0, Plasmoid.configuration.historySize)
    }

    function forget(hex) {
        Plasmoid.configuration.history = history.filter(c => c !== hex)
    }

    // Global shortcut (Configure → Keyboard Shortcuts): pick or show history.
    // Plasma's own activation handler runs right after ours and, when activationTogglesExpanded
    // is off, always expands. It only collapses instead when we're expanded *and* toggling, so
    // claim exactly that for the instant: it then closes the popup in the same tick, before the
    // popup ever paints.
    property bool shortcutPicking: false
    activationTogglesExpanded: shortcutPicking || Plasmoid.configuration.shortcutAction === "history"
    Connections {
        target: Plasmoid
        function onActivated() {
            if (Plasmoid.configuration.shortcutAction === "history")
                return
            root.shortcutPicking = true
            root.expanded = true
            Qt.callLater(() => {
                root.shortcutPicking = false
                root.expanded = false
                root.pick()
            })
        }
    }

    P5Support.DataSource {
        id: runner
        engine: "executable"
        onNewData: (source, data) => {
            disconnectSource(source)
            if (source.indexOf("#pick") < 0)
                return
            root.picking = false
            // stdout: "#RRGGBB" when picked, then magnifier=on|off, zoom=N and badge=on|off (always),
            // so the magnifier state set during picking (Alt, wheel, B) is remembered.
            for (const line of String(data["stdout"] || "").split("\n").map(l => l.trim())) {
                if (/^#[0-9A-Fa-f]{6}$/.test(line))
                    root.remember(line.toUpperCase())
                else if (line.startsWith("magnifier="))
                    Plasmoid.configuration.magnifier = line === "magnifier=on"
                else if (line.startsWith("badge="))
                    Plasmoid.configuration.badge = line === "badge=on"
                else if (line.startsWith("zoom=") && parseInt(line.slice(5)) > 0)
                    Plasmoid.configuration.zoom = parseInt(line.slice(5))
            }
        }
    }

    // Color dialog. Qt opens its own native dialog window here, so the host window stays
    // hidden (the stock picker shows it, leaving an empty second window).
    Component {
        id: colorWindowComponent
        Window {
            id: window
            visible: false
            title: Plasmoid.title
            QtDialogs.ColorDialog {
                id: colorDialog
                modality: Qt.NonModal
                title: Plasmoid.title
                selectedColor: root.lastColor || "#ffffff"
                onAccepted: {
                    const c = selectedColor
                    const hex = "#" + [c.r, c.g, c.b].map(v => Math.round(v * 255).toString(16).padStart(2, "0"))
                                                     .join("").toUpperCase()
                    root.remember(hex)
                    if (Plasmoid.configuration.autoCopy)
                        root.copy(root.format(hex))
                    window.destroy()
                }
                onRejected: window.destroy()
            }
            onClosing: destroy()
            Component.onCompleted: colorDialog.open()
        }
    }

    // Same menu as the stock picker.
    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18nc("@action", "Open Color Dialog")
            icon.name: "color-management"
            onTriggered: colorWindowComponent.createObject(root)
        },
        PlasmaCore.Action {
            text: i18nc("@action", "Clear History")
            icon.name: "edit-clear-history"
            enabled: root.history.length > 0
            onTriggered: Plasmoid.configuration.history = []
        },
        PlasmaCore.Action {
            text: i18nc("@action", "View History")
            icon.name: "view-history"
            onTriggered: root.expanded = true
        }
    ]

    // ---- panel: pipette button + last-color dot (opens history) ----
    // Pipette + color dots, shown everywhere (panel, desktop, plasmawindowed).
    component PanelRow: GridLayout {
        id: compact
        rowSpacing: spacing
        columnSpacing: spacing
        flow: vertical ? GridLayout.TopToBottom : GridLayout.LeftToRight
        // Sizing copied from the stock picker (kdeplasma-addons CompactRepresentation.qml):
        // slot = the parent's thickness (not our own, which Plasma shrinks by applet margins).
        readonly property bool vertical: Plasmoid.formFactor === PlasmaCore.Types.Vertical
        readonly property real slot: parent ? (vertical ? parent.width : parent.height) : Kirigami.Units.iconSizes.medium
        // SIZE KNOBS, all as a share of the panel thickness:
        //   boxScale  = button (hover box) size, square
        //   iconScale = pipette icon size, drawn centered in its box
        //   dotScale  = color dot size, drawn centered in its box
        //   gapScale  = space between the boxes (negative lets them overlap)
        readonly property real boxScale: 1
        readonly property real iconScale: 1
        readonly property real dotScale: 0.75
        readonly property real gapScale: 0
        readonly property int boxSize: Math.round(slot * boxScale)
        readonly property int iconSize: Math.round(slot * iconScale)
        // The pipette snaps to standard icon sizes (16/22/32/48…, kept crisp by Kirigami);
        // the dot follows that rounded size so both always keep the dotScale : iconScale ratio.
        readonly property int drawnIconSize: Kirigami.Units.iconSizes.roundedIconSize(iconSize)
        readonly property int dotSize: Math.round(drawnIconSize * dotScale / iconScale)
        readonly property int spacing: Math.round(slot * gapScale)
        Layout.fillHeight: !vertical
        Layout.fillWidth: vertical
        Layout.minimumWidth: vertical ? 0 : implicitWidth
        Layout.minimumHeight: vertical ? implicitHeight : 0
        // Equal square buttons (boxScale), centered in the panel;
        // tighter than stock's full-thickness slots. Max = content, so the row keeps its ratio on the desktop.
        Layout.maximumWidth: vertical ? Number.POSITIVE_INFINITY : implicitWidth
        Layout.maximumHeight: vertical ? implicitHeight : Number.POSITIVE_INFINITY

        // Stock flat ToolButtons: theme hover/press/focus highlight for free.
        // Stock KDE ToolButtons + ToolTipAreas, laid out like the stock color picker:
        // Plasma handles hover/press and right-clicks still open the widget menu.
        PlasmaComponents.ToolButton {
            id: pipette
            Layout.preferredWidth: compact.boxSize
            Layout.preferredHeight: compact.boxSize
            // like stock: extra length spreads the row, pipette at the start, dots at the end
            Layout.alignment: compact.vertical ? (Qt.AlignTop | Qt.AlignHCenter) : (Qt.AlignLeft | Qt.AlignVCenter)
            padding: 0
            display: QQC2.AbstractButton.IconOnly
            text: i18n("Pick Color")
            onClicked: {
                // drop the tooltip now, or its fade-out ends up in the picker's screenshot
                pipetteTip.hideImmediately()
                root.pick()
            }
            PlasmaCore.ToolTipArea {
                id: pipetteTip
                anchors.fill: parent
                mainText: pipette.text
                subText: i18n("Click anywhere on screen to pick a color")
            }
            Kirigami.Icon {
                anchors.centerIn: parent
                width: compact.iconSize   // pipette size: see SIZE KNOBS above
                height: width
                source: "color-picker"
                active: pipette.hovered
            }
        }

        // Glassy swatches of the newest colors (Preview count); match the magnifier lens.
        // Optional separator, sized like stock: 1/16 of the thickness wide, as tall as the dot.
        Item {
            readonly property real thickness: Math.max(1, compact.slot / Kirigami.Units.iconSizes.small)
            visible: Plasmoid.configuration.showSeparator
            Layout.preferredWidth: compact.vertical ? compact.slot : thickness
            Layout.preferredHeight: compact.vertical ? thickness : compact.slot
            Layout.alignment: Qt.AlignCenter
            Rectangle {
                anchors.centerIn: parent
                width: compact.vertical ? compact.dotSize : parent.thickness
                height: compact.vertical ? parent.thickness : compact.dotSize
                color: Kirigami.Theme.textColor
                opacity: 0.6
            }
        }

        Repeater {
            id: repeater
            model: root.lastColor ? root.history.slice(0, Math.max(1, Plasmoid.configuration.previewCount))
                                  : [""]
            delegate: PlasmaComponents.ToolButton {
                id: swatch
                required property string modelData
                Layout.preferredWidth: compact.boxSize
                Layout.preferredHeight: compact.boxSize
                Layout.alignment: compact.vertical ? (Qt.AlignBottom | Qt.AlignHCenter) : (Qt.AlignRight | Qt.AlignVCenter)
                padding: 0
                display: QQC2.AbstractButton.IconOnly
                text: modelData ? root.format(modelData) : i18n("No colors yet")
                // Pressing outside the open popup hides it before the click lands, so toggling
                // on click would reopen it; decide from the state at press time (as stock does).
                property bool wasExpanded: false
                onPressed: wasExpanded = root.expanded
                onClicked: root.expanded = !wasExpanded
                PlasmaCore.ToolTipArea {
                    anchors.fill: parent
                    mainText: swatch.text
                    subText: swatch.modelData ? i18n("Middle-click to copy the color code") : ""
                }

                Item {
                    anchors.fill: parent
                    Rectangle {
                        anchors.centerIn: parent
                        width: compact.dotSize
                        height: width
                        radius: width / 2
                        color: swatch.modelData || "transparent"
                        border.width: 1   // whole pixel: a 0.5 px line gets smeared grey on dark dots
                        border.color: swatch.modelData ? Qt.rgba(1, 1, 1, 0.7)
                                                       : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                                                 Kirigami.Theme.textColor.b, 0.7)
                        Behavior on color { ColorAnimation { duration: Kirigami.Units.longDuration } }

                        // like the stock picker: handlers on the dot, so the ToolButton keeps
                        // its own KDE hover/press look and right-click still reaches Plasma
                        TapHandler {
                            acceptedButtons: Qt.LeftButton
                            onPressedChanged: if (pressed) swatch.wasExpanded = root.expanded
                            onTapped: root.expanded = !swatch.wasExpanded
                        }
                        TapHandler {
                            acceptedButtons: Qt.MiddleButton
                            onTapped: if (swatch.modelData) root.copy(root.format(swatch.modelData))
                        }

                        // light rim toward the edge (Glass style)
                        Rectangle {
                            anchors.fill: parent
                            radius: width / 2
                            visible: swatch.modelData !== "" && Plasmoid.configuration.glassStyle
                            color: "transparent"
                            border.width: Math.max(1, Math.round(width * 0.04))
                            border.color: Qt.rgba(1, 1, 1, 0.35)
                        }
                        // specular highlight, top-left (Glass style)
                        Rectangle {
                            visible: swatch.modelData !== "" && Plasmoid.configuration.glassStyle
                            x: parent.width * 0.16; y: parent.height * 0.07
                            width: parent.width * 0.6; height: parent.height * 0.38
                            radius: height / 2
                            gradient: Gradient {
                                GradientStop { position: 0; color: Qt.rgba(1, 1, 1, 0.55) }
                                GradientStop { position: 1; color: Qt.rgba(1, 1, 1, 0) }
                            }
                        }
                    }
                }
            }
        }
    }

    // Always just the pipette + dot (also on the desktop / plasmawindowed); history opens as a popup.
    preferredRepresentation: compactRepresentation
    compactRepresentation: PanelRow {}

    // ---- popup: history grid ----
    fullRepresentation: PlasmaExtras.Representation {
        Layout.minimumWidth: Kirigami.Units.gridUnit * 19
        Layout.minimumHeight: Kirigami.Units.gridUnit * 12

        PlasmaExtras.PlaceholderMessage {
            anchors.centerIn: parent
            width: parent.width - Kirigami.Units.gridUnit * 2
            visible: root.history.length === 0
            iconName: "color-picker"
            text: i18n("No colors yet")
            explanation: i18n("Click the pipette in the panel, then click anywhere on screen")
        }

        GridView {
            id: grid
            anchors.fill: parent
            anchors.margins: Kirigami.Units.smallSpacing
            model: root.history
            cellWidth: Kirigami.Units.gridUnit * 6
            cellHeight: Kirigami.Units.gridUnit * 6
            clip: true

            delegate: Item {
                id: cell
                required property string modelData
                width: grid.cellWidth
                height: grid.cellHeight

                // KDE color picker style: square tile, color code on a band along the bottom.
                // The outer rectangle is the border (filled, so it stays crisp at any scale);
                // the color sits inset inside it.
                Rectangle {
                    id: tile
                    anchors.fill: parent
                    anchors.margins: Kirigami.Units.smallSpacing
                    // border turns highlight on hover, same size, no extra ring
                    color: swatchArea.containsMouse
                        ? Kirigami.Theme.highlightColor
                        : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g,
                                  Kirigami.Theme.textColor.b, 0.6)
                    radius: 3

                    Rectangle {   // color fill, ending just under the band
                        anchors { fill: parent; margins: 1; bottomMargin: band.height - 1 }
                        radius: 2
                        color: cell.modelData
                    }

                    // color code on a band along the bottom, inside the border
                    Rectangle {
                        id: band
                        anchors { left: parent.left; right: parent.right; bottom: parent.bottom
                                  margins: 1 }
                        height: label.implicitHeight + Kirigami.Units.smallSpacing * 2
                        // Looks like a dark semi-transparent band over the swatch (as in the stock
                        // picker), but is painted opaque so no swatch edge bleeds through.
                        color: Qt.tint(cell.modelData, Qt.rgba(0.2, 0.2, 0.2, 0.85))
                        radius: 2
                        Rectangle {   // square off the top corners
                            anchors { left: parent.left; right: parent.right; top: parent.top }
                            height: parent.radius
                            color: parent.color
                        }
                        PlasmaComponents.Label {
                            id: label
                            anchors.centerIn: parent
                            text: cell.modelData.toUpperCase()
                            color: "#fcfcfc"
                        }
                    }
                    MouseArea {
                        id: swatchArea
                        anchors.fill: parent
                        hoverEnabled: true
                        acceptedButtons: Qt.LeftButton | Qt.RightButton
                        onClicked: mouse => {
                            if (mouse.button === Qt.RightButton)
                                menu.popup()
                            else
                                root.copy(root.format(cell.modelData))
                        }
                    }
                    PlasmaComponents.ToolTip { text: i18n("Click to copy %1", root.format(cell.modelData)) }
                }

                QQC2.Menu {
                    id: menu
                    Repeater {
                        model: Formats.list.map(f => f.value)
                        QQC2.MenuItem {
                            required property string modelData
                            text: i18n("Copy %1", root.format(cell.modelData, modelData))
                            onTriggered: root.copy(root.format(cell.modelData, modelData))
                        }
                    }
                    QQC2.MenuSeparator {}
                    QQC2.MenuItem {
                        text: i18n("Remove")
                        icon.name: "edit-delete"
                        onTriggered: root.forget(cell.modelData)
                    }
                }
            }
        }
    }
}
