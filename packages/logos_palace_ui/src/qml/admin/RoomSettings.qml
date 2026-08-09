import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    required property Item app
    signal openAssets()

    spacing: 5

    Text {
        text: "Room"
        color: "#fff2cf"
        font.bold: true
        font.pixelSize: 13
    }
    Text {
        Layout.fillWidth: true
        text: app.roomTitle + (app.roomLocked ? " is locked" : " is open")
        color: "#c9b78e"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
    Button {
        objectName: "palaceAdminRoomLock"
        Layout.fillWidth: true
        text: app.roomLocked ? "Unlock room" : "Lock room"
        enabled: app.canSetRoomLock
        onClicked: app.setRoomLocked(app.roomTitle, !app.roomLocked)
    }
    Button {
        objectName: "palaceAdminOpenAssets"
        Layout.fillWidth: true
        text: "Open asset authoring"
        enabled: app.ready
        onClicked: openAssets()
    }
}
