import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    readonly property var backend: logos.module("logos_palace_ui")
    readonly property string roomTitle: backend ? backend.roomTitle : "Connecting..."
    readonly property string syncHealth: backend ? backend.syncHealth : "recovering"
    property bool ready: false

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "logos_palace_ui")
                root.ready = isReady && root.backend !== null
        }
    }

    Component.onCompleted: {
        root.ready = root.backend !== null && logos.isViewModuleReady("logos_palace_ui")
    }

    Rectangle {
        anchors.fill: parent
        color: "#15110d"

        Rectangle {
            id: roomCanvas
            anchors.fill: parent
            anchors.margins: 28
            color: root.roomTitle === "Lounge" ? "#39495b" : "#796549"
            radius: 8
            border.color: "#d5b77a"
            border.width: 3

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 24
                text: root.roomTitle
                font.pixelSize: 30
                font.bold: true
                color: "#fff2cf"
            }

            Rectangle {
                width: 74
                height: 74
                radius: width / 2
                color: "#d87950"
                anchors.centerIn: parent
                border.color: "#ffe1bb"
                border.width: 3

                Text {
                    anchors.centerIn: parent
                    text: "A"
                    color: "#22140d"
                    font.pixelSize: 28
                    font.bold: true
                }
            }

            Rectangle {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 92
                width: 168
                height: 48
                radius: 24
                color: "#fff8e7"

                Text {
                    anchors.centerIn: parent
                    text: "Welcome to Palace"
                    color: "#2b2016"
                    font.pixelSize: 15
                }
            }

            Button {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 22
                text: root.roomTitle === "Atrium" ? "Door to Lounge" : "Door to Atrium"
                enabled: root.ready
                onClicked: {
                    if (root.roomTitle === "Atrium")
                        logos.watch(backend.useSpot("door"), function () {}, function () {})
                    else
                        logos.watch(backend.enterRoom("atrium"), function () {}, function () {})
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 40
            width: 126
            height: 72
            color: "#211a14cc"
            radius: 6

            Column {
                anchors.centerIn: parent
                spacing: 5
                Text { text: "Rooms"; color: "#fff2cf"; font.bold: true }
                Text { text: "Atrium\nLounge"; color: "#f0dfba"; font.pixelSize: 12 }
            }
        }

        Text {
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 40
            text: root.ready ? root.syncHealth.replace("_", " ") : "Connecting"
            color: root.syncHealth === "fully_synchronized" ? "#a7e3a0" : "#f3c36b"
        }
    }
}
