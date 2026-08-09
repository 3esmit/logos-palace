import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: chatBar

    required property Item app

    objectName: "palaceInputStrip"
    Layout.fillWidth: true
    Layout.preferredHeight: 32
    color: "#d4d0c8"
    border.color: "#808080"

    function focusInput() {
        chatInput.forceActiveFocus()
    }

    RowLayout {
        anchors.fill: parent
        anchors.margins: 3
        spacing: 4

        TextField {
            id: chatInput
            objectName: "palaceChatInput"
            Layout.fillWidth: true
            Layout.preferredHeight: 24
            placeholderText: ""
            maximumLength: 280
            Accessible.name: "Chat message"
            enabled: chatBar.app.ready
            onAccepted: {
                if (text.length > 0) {
                    chatBar.app.sendSpeech(text)
                    clear()
                }
            }
        }
        Button {
            objectName: "palaceSayButton"
            text: "Say"
            Accessible.name: "Send chat message"
            Layout.preferredHeight: 24
            Layout.preferredWidth: 44
            enabled: chatBar.app.ready && chatInput.text.length > 0
            onClicked: {
                chatBar.app.sendSpeech(chatInput.text)
                chatInput.clear()
            }
        }
    }
}
