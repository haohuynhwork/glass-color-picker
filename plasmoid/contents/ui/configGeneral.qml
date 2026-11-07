import QtQuick
import QtQuick.Layouts
import QtQuick.Controls as QQC2
import org.kde.kirigami as Kirigami
import org.kde.kcmutils as KCM

import "formats.js" as Formats

KCM.SimpleKCM {
    property string cfg_format
    property alias cfg_autoCopy: autoCopyBox.checked
    property string cfg_shortcutAction
    property string cfg_pickerMode
    property string cfg_sampling
    property alias cfg_previewCount: previewBox.value
    property alias cfg_showSeparator: separatorBox.checked
    property alias cfg_glassStyle: glassBox.checked
    property alias cfg_historySize: sizeBox.value
    property alias cfg_magnifier: magnifierBox.checked
    property alias cfg_zoom: zoomBox.value
    property alias cfg_badge: badgeBox.checked
    property alias cfg_pickerCommand: cmdField.text

    Kirigami.FormLayout {
        RowLayout {
            Kirigami.FormData.label: i18n("Default color format:")
            QQC2.ComboBox {
                id: formatBox
                textRole: "text"
                valueRole: "value"
                model: Formats.list
                Component.onCompleted: currentIndex = indexOfValue(cfg_format)
                onActivated: cfg_format = currentValue
            }
            QQC2.Label {
                text: Formats.format("#3A7BD5", formatBox.currentValue)
                opacity: 0.7
            }
        }

        QQC2.CheckBox {
            id: autoCopyBox
            text: i18n("Automatically copy color to clipboard")
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.ComboBox {
            id: modeBox
            Kirigami.FormData.label: i18n("Picker:")
            textRole: "text"
            valueRole: "value"
            model: [
                { value: "custom", text: i18n("Glass picker (magnifier and badge)") },
                { value: "kwin", text: i18n("KDE built-in picker") },
                { value: "dialog", text: i18n("Qt color dialog") }
            ]
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(cfg_pickerMode))
            onActivated: cfg_pickerMode = currentValue
        }
        QQC2.ComboBox {
            Kirigami.FormData.label: i18n("Sampling:")
            enabled: modeBox.currentValue === "custom"
            textRole: "text"
            valueRole: "value"
            model: [
                { value: "freeze", text: i18n("Freeze screen when picking starts") },
                { value: "live", text: i18n("Live (panels, video and animations keep moving)") }
            ]
            Component.onCompleted: currentIndex = Math.max(0, indexOfValue(cfg_sampling))
            onActivated: cfg_sampling = currentValue
        }

        QQC2.CheckBox {
            id: magnifierBox
            enabled: modeBox.currentValue === "custom"
            Kirigami.FormData.label: i18n("Magnifier:")
            text: i18n("Show magnifier while picking (Alt toggles)")
        }
        QQC2.SpinBox {
            id: zoomBox
            Kirigami.FormData.label: i18n("Magnifier area:")
            enabled: magnifierBox.checked && modeBox.currentValue === "custom"
            from: 7
            to: 31
            stepSize: 2
            textFromValue: v => i18np("%1 pixel across", "%1 pixels across", v)
            valueFromText: t => parseInt(t)
        }
        QQC2.Label {
            text: i18n("Fewer pixels = more zoom. Mouse wheel changes it while picking.")
            font: Kirigami.Theme.smallFont
            opacity: 0.7
        }

        QQC2.CheckBox {
            id: badgeBox
            enabled: modeBox.currentValue === "custom"
            Kirigami.FormData.label: i18n("Color badge:")
            text: i18n("Show color value while picking (B toggles)")
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.RadioButton {
            Kirigami.FormData.label: i18n("When pressing the keyboard shortcut:")
            text: i18n("Pick a color")
            checked: cfg_shortcutAction !== "history"
            onToggled: if (checked) cfg_shortcutAction = "pick"
        }
        QQC2.RadioButton {
            text: i18n("Show history")
            checked: cfg_shortcutAction === "history"
            onToggled: if (checked) cfg_shortcutAction = "history"
        }

        Item { Kirigami.FormData.isSection: true }

        QQC2.CheckBox {
            id: separatorBox
            Kirigami.FormData.label: i18n("Panel:")
            text: i18n("Show separator between pipette and colors")
        }
        QQC2.CheckBox {
            id: glassBox
            text: i18n("Glass style (shiny color dots)")
        }
        QQC2.SpinBox {
            id: previewBox
            Kirigami.FormData.label: i18n("Preview count:")
            from: 1
            to: 9
        }
        QQC2.SpinBox {
            id: sizeBox
            Kirigami.FormData.label: i18n("History size:")
            from: 1
            to: 48
        }
        QQC2.TextField {
            id: cmdField
            Kirigami.FormData.label: i18n("Picker command:")
            Layout.fillWidth: true
        }
    }
}
