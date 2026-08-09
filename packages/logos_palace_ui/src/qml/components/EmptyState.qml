import QtQuick
import QtQuick.Layouts

ColumnLayout {
    id: emptyState
    property string title: "Nothing here yet"
    property string message: ""
    property color foreground: "#463b2d"

    spacing: 4

    Text {
        Layout.fillWidth: true
        text: emptyState.title
        color: emptyState.foreground
        font.bold: true
        font.pixelSize: 14
    }

    Text {
        Layout.fillWidth: true
        visible: emptyState.message.length > 0
        text: emptyState.message
        color: emptyState.foreground
        opacity: 0.8
        wrapMode: Text.Wrap
        font.pixelSize: 11
    }
}
