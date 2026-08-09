import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: welcome

    required property Item app
    signal createRequested()
    signal joinRequested()
    signal recoverRequested()

    objectName: "palaceWelcome"
    spacing: 6

    Text {
        Layout.fillWidth: true
        text: "How do you want to enter?"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 13
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 8

        Button {
            objectName: "palaceCreateChoice"
            Layout.fillWidth: true
            text: "Create Palace"
            Accessible.name: "Create Palace"
            onClicked: welcome.createRequested()
        }

        Button {
            objectName: "palaceJoinChoice"
            Layout.fillWidth: true
            text: "Join Palace"
            Accessible.name: "Join Palace"
            onClicked: welcome.joinRequested()
        }

        Button {
            objectName: "palaceRecoverChoice"
            Layout.fillWidth: true
            text: "Recover Palace"
            Accessible.name: "Recover Palace"
            onClicked: welcome.recoverRequested()
        }
    }
}
