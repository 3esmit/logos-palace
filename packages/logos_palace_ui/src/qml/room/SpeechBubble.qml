import QtQuick

Rectangle {
    id: bubble

    required property string text
    property real maximumHeight: 156

    objectName: "palaceSpeechBubble"
    width: Math.min(184, Math.max(88, speechText.implicitWidth + 24))
    height: Math.min(maximumHeight, speechText.implicitHeight + 16)
    radius: 12
    color: "#fff8e7"
    border.color: "#8c7145"
    visible: text.length > 0

    Text {
        id: speechText
        anchors.centerIn: parent
        width: Math.min(158, implicitWidth)
        height: Math.min(bubble.maximumHeight - 16, implicitHeight)
        text: bubble.text
        color: "#2b2016"
        font.pixelSize: 13
        wrapMode: Text.Wrap
        horizontalAlignment: Text.AlignHCenter
        clip: true
    }
}
