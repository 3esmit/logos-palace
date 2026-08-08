import QtQuick
import QtQuick.Layouts

ColumnLayout {
    required property Item app
    visible: app.onboardingMode === "recover"
    spacing: 4

    Text {
        Layout.fillWidth: true
        text: "Recover Palace"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 14
    }
    Text {
        Layout.fillWidth: true
        text: "Reconnect this profile to its existing Palace and retained Storage catalog."
        color: "#463b2d"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
}
