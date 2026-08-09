import QtQuick
import QtQuick.Layouts

ColumnLayout {
    required property Item app
    visible: app.onboardingMode === "create"
    spacing: 4

    Text {
        Layout.fillWidth: true
        text: "Create Palace"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 14
    }
    Text {
        Layout.fillWidth: true
        text: "Choose a title, then prepare the Atrium and Lounge rooms."
        color: "#463b2d"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
}
