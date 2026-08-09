import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    required property Item app

    spacing: 4

    Text {
        text: "Props"
        color: "#fff2cf"
        font.bold: true
        font.pixelSize: 13
    }
    Text {
        Layout.fillWidth: true
        text: app.availablePropId.length > 0
            ? "Assigned prop: " + app.availablePropId
            : "No prop assigned."
        color: "#c9b78e"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
    Button {
        Layout.fillWidth: true
        text: "Ban assigned prop"
        enabled: app.canBanProp && app.availablePropId.length > 0
        onClicked: app.banProp(app.availablePropId)
    }
}
