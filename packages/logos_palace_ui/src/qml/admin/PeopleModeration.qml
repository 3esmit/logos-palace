import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    required property Item app

    spacing: 4

    Text {
        text: "People"
        color: "#fff2cf"
        font.bold: true
        font.pixelSize: 13
    }
    Text {
        Layout.fillWidth: true
        text: app.selectedModerationUserName().length > 0
            ? "Selected: " + app.selectedModerationUserName()
            : "Select a participant in the People panel."
        color: "#c9b78e"
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
    RowLayout {
        Layout.fillWidth: true
        Button {
            Layout.fillWidth: true
            text: "Delegate"
            enabled: app.canDelegateModerator
                && app.selectedModerationUserId.length === 64
            onClicked: app.delegateModerator(app.selectedModerationUserId)
        }
        Button {
            Layout.fillWidth: true
            text: "Ban"
            enabled: app.canBanUser
                && app.selectedModerationUserId.length === 64
            onClicked: app.banUser(app.selectedModerationUserId)
        }
    }
}
