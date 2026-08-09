import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

ColumnLayout {
    required property Item app
    signal openAuthoring()

    spacing: 4

    Text {
        text: "Assets"
        color: "#fff2cf"
        font.bold: true
        font.pixelSize: 13
    }
    Text {
        Layout.fillWidth: true
        visible: app.canManageAssets
        text: app.canManageAssets
            ? "Select, approve, publish, and assign from the asset authoring view."
            : "Read-only. Only the Palace owner can change assets."
        color: "#c9b78e"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
    Button {
        Layout.fillWidth: true
        text: "Manage assets"
        enabled: app.ready
        onClicked: openAuthoring()
    }
    EmptyState {
        Layout.fillWidth: true
        visible: !app.canManageAssets
        title: "Assets are read-only"
        message: "Only the Palace owner can approve, publish, or assign assets."
        foreground: "#c9b78e"
    }
}
