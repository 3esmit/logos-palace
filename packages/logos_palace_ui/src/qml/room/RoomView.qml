import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: roomView

    required property Item app

    objectName: "palaceRoomView"
    Layout.fillWidth: true
    Layout.fillHeight: true
    enabled: app.roomUsable

    Rectangle {
        id: roomCanvas
        objectName: "palaceRoomCanvas"
        anchors.fill: parent
        color: "#312a24"
        border.color: "#404040"
        border.width: 1
        clip: true

        Image {
            id: roomBackground
            objectName: "palaceRoomBackground"
            anchors.fill: parent
            source: roomView.app.roomBackgroundHandle.length === 64
                ? "image://basecamp-verified/"
                  + roomView.app.roomBackgroundHandle : ""
            fillMode: Image.PreserveAspectCrop
            smooth: false
        }
        Rectangle {
            objectName: "palaceRoomBackgroundPlaceholder"
            anchors.fill: parent
            color: "#3a342c"
            visible: roomBackground.status !== Image.Ready
        }
        Rectangle {
            anchors.fill: parent
            color: "#1b130d"
            opacity: roomBackground.status === Image.Ready ? 0.12 : 0
        }

        MouseArea {
            objectName: "palaceRoomMoveSurface"
            anchors.fill: parent
            z: 2
            enabled: roomView.app.ready
            acceptedButtons: Qt.LeftButton
            onClicked: function(mouse) {
                var coordinate = roomView.app.canvasPixelsToProtocol(
                    mouse.x, mouse.y)
                roomView.app.moveAvatar(coordinate.x, coordinate.y)
            }
        }

        Item {
            objectName: "palaceParticipants"
            anchors.fill: parent
            z: 4

            Repeater {
                model: roomView.app.participants.length
                delegate: ParticipantView {
                    required property int index
                    app: roomView.app
                    participant: roomView.app.participants[index] || ({})
                    participantIndex: index
                    participantCount: roomView.app.participants.length
                }
            }
        }

        Button {
            id: roomDoor
            objectName: "palaceRoomDoor"
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: roomView.app.roomCanvasDoorBottomMargin
            width: 154
            height: roomView.app.roomCanvasDoorHeight
            text: roomView.app.roomTitle !== "Atrium"
                ? "Door to Atrium"
                : (roomView.app.doorBlocked
                   ? "Door finalizing…" : "Door to Lounge")
            font.bold: true
            font.pixelSize: 12
            Accessible.name: text
            enabled: roomView.app.ready
                && (roomView.app.roomTitle !== "Atrium"
                    || !roomView.app.doorBlocked)
            z: 10
            ToolTip.visible: hovered
            ToolTip.text: text

            background: Rectangle {
                radius: 5
                color: roomDoor.down ? "#74421f" : "#9a6030"
                border.color: "#f3cf8a"
                border.width: 2
                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 8
                    anchors.verticalCenter: parent.verticalCenter
                    width: 28
                    height: parent.height - 12
                    radius: 2
                    color: "#4d2918"
                    border.color: "#e3b76a"
                }
                Rectangle {
                    anchors.left: parent.left
                    anchors.leftMargin: 29
                    anchors.verticalCenter: parent.verticalCenter
                    width: 5
                    height: 5
                    radius: 3
                    color: "#f9db79"
                }
            }
            contentItem: Text {
                anchors.left: parent.left
                anchors.leftMargin: 44
                anchors.right: parent.right
                anchors.rightMargin: 8
                anchors.verticalCenter: parent.verticalCenter
                text: roomDoor.text
                color: "#fff8e7"
                font: roomDoor.font
                wrapMode: Text.Wrap
                horizontalAlignment: Text.AlignHCenter
                verticalAlignment: Text.AlignVCenter
            }
            onClicked: roomView.app.selectFixedRoom(
                roomView.app.roomTitle === "Atrium" ? "lounge" : "atrium")
        }
    }
}
