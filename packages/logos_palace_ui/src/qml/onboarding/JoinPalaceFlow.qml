import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    required property Item app
    visible: app.onboardingMode === "join"
    spacing: 4

    Text {
        Layout.fillWidth: true
        text: "Join Palace"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 14
    }
    Text {
        Layout.fillWidth: true
        text: "Paste the invitation shared by the Palace creator."
        color: "#463b2d"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
    TextField {
        objectName: "palaceOnboardingInvitation"
        Layout.fillWidth: true
        placeholderText: "Paste Palace invitation JSON"
        maximumLength: 32768
        text: app.onboardingInvitation
        enabled: app.ready && !app.onboardingWorking
        Accessible.name: "Palace invitation"
        onTextEdited: {
            app.onboardingInvitation = text
            app.resetOnboardingAfterEdit()
        }
    }
}
