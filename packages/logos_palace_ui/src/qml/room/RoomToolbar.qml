import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: toolbar

    required property Item app

    objectName: "palaceToolbox"
    Layout.fillWidth: true
    Layout.preferredHeight: 36
    color: "#d4d0c8"
    border.color: "#808080"

    Row {
        anchors.verticalCenter: parent.verticalCenter
        anchors.left: parent.left
        anchors.leftMargin: 4
        spacing: 3

        Button {
            objectName: "palaceToolboxDoor"
            width: 32
            height: 28
            text: "🚪"
            Accessible.name: "Door / room exit"
            ToolTip.visible: hovered
            ToolTip.text: "Door / room exit"
            enabled: toolbar.app.ready
                && (toolbar.app.roomTitle !== "Atrium"
                    || !toolbar.app.doorBlocked)
            onClicked: toolbar.app.selectFixedRoom(
                toolbar.app.roomTitle === "Atrium" ? "lounge" : "atrium")
        }

        Button {
            objectName: "palaceToolboxRooms"
            width: 32
            height: 28
            text: "⌂"
            Accessible.name: "Rooms"
            ToolTip.visible: hovered
            ToolTip.text: "Rooms"
            enabled: toolbar.app.ready
            onClicked: toolbar.app.roomListOpen = !toolbar.app.roomListOpen
        }

        Button {
            objectName: "palaceMoveUp"
            width: 28
            height: 28
            text: "↑"
            Accessible.name: "Move up"
            enabled: toolbar.app.ready
            onClicked: toolbar.app.moveAvatar(
                toolbar.app.localMotionX, toolbar.app.localMotionY - 750)
        }
        Button {
            objectName: "palaceMoveLeft"
            width: 28
            height: 28
            text: "←"
            Accessible.name: "Move left"
            enabled: toolbar.app.ready
            onClicked: toolbar.app.moveAvatar(
                toolbar.app.localMotionX - 750, toolbar.app.localMotionY)
        }
        Button {
            objectName: "palaceMoveRight"
            width: 28
            height: 28
            text: "→"
            Accessible.name: "Move right"
            enabled: toolbar.app.ready
            onClicked: toolbar.app.moveAvatar(
                toolbar.app.localMotionX + 750, toolbar.app.localMotionY)
        }
        Button {
            objectName: "palaceMoveDown"
            width: 28
            height: 28
            text: "↓"
            Accessible.name: "Move down"
            enabled: toolbar.app.ready
            onClicked: toolbar.app.moveAvatar(
                toolbar.app.localMotionX, toolbar.app.localMotionY + 750)
        }

        Rectangle {
            width: 1
            height: 22
            color: "#808080"
            anchors.verticalCenter: parent.verticalCenter
        }

        Button {
            objectName: "palaceWearAssignedProp"
            width: 32
            height: 28
            text: "☺+"
            Accessible.name: "Wear assigned prop"
            ToolTip.visible: hovered
            ToolTip.text: "Wear assigned prop"
            enabled: toolbar.app.ready
                && toolbar.app.availablePropId.length > 0
                && toolbar.app.localWornPropId
                    !== toolbar.app.availablePropId
            onClicked: toolbar.app.wearProp(toolbar.app.availablePropId)
        }
        Button {
            objectName: "palaceRemoveAssignedProp"
            width: 32
            height: 28
            text: "☺−"
            Accessible.name: "Remove worn prop"
            ToolTip.visible: hovered
            ToolTip.text: "Remove worn prop"
            enabled: toolbar.app.ready
                && toolbar.app.availablePropId.length > 0
                && toolbar.app.localWornPropId
                    === toolbar.app.availablePropId
            onClicked: toolbar.app.removeProp(toolbar.app.availablePropId)
        }
        Button {
            objectName: "palaceBackgroundModerationButton"
            width: 56
            height: 28
            text: "Assets"
            Accessible.name: "Assets"
            enabled: toolbar.app.ready
            ToolTip.visible: hovered && !toolbar.app.canManageAssets
            ToolTip.text: toolbar.app.assetAuthoringReadOnlyMessage()
            onClicked: toolbar.app.adminDrawerOpen = true
        }
        Button {
            objectName: "palaceUserListToggle"
            width: 56
            height: 28
            text: "Users"
            Accessible.name: "User list"
            enabled: toolbar.app.ready
            onClicked: toolbar.app.userListOpen = !toolbar.app.userListOpen
        }
    }

    Row {
        anchors.right: parent.right
        anchors.rightMargin: 8
        anchors.verticalCenter: parent.verticalCenter
        spacing: 7

        Rectangle {
            objectName: "palaceLocalDevelopmentIndicator"
            visible: toolbar.app.localDevelopmentMode
            width: localDevelopmentIndicatorText.implicitWidth + 12
            height: 20
            radius: 2
            color: "#e7edf3"
            border.color: "#8197ad"

            Text {
                id: localDevelopmentIndicatorText
                anchors.centerIn: parent
                text: "Local development · public finality unavailable"
                color: "#24394d"
                font.pixelSize: 10
            }
        }

        Text {
            text: toolbar.app.roomTitle
            color: "#000080"
            font.bold: true
            font.pixelSize: 12
        }
    }
}
