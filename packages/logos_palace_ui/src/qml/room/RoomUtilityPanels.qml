pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls

Item {
    id: utility

    required property Item app

    anchors.left: parent.left
    anchors.right: parent.right
    anchors.top: parent.top
    // Keep utility overlay out of status/chat hit targets below it.
    anchors.bottom: parent.bottom
    anchors.bottomMargin: 72
    enabled: app.roomUsable
        && (app.roomListOpen || app.userListOpen || app.propBagOpen)
    z: enabled ? 10 : -1

    // Fixed, visible room palette: no inferred routing or hidden rooms.
    Rectangle {
        id: roomListPanel
        objectName: "palaceRoomListPanel"
        anchors.left: parent.left
        anchors.top: parent.top
        anchors.margins: 8
        anchors.topMargin: 42
        width: 164
        height: 92
        radius: 2
        color: "#f5f5f5"
        border.color: "#404040"
        z: 20
        visible: utility.app.roomListOpen

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 3

            Text {
                text: "Rooms"
                color: "#000000"
                font.bold: true
                font.pixelSize: 11
            }

            Button {
                objectName: "palaceRoomListAtrium"
                width: 150
                height: 25
                text: utility.app.roomTitle === "Atrium"
                    ? "Atrium (current)" : "Atrium"
                enabled: utility.app.ready
                onClicked: {
                    utility.app.selectFixedRoom("atrium")
                    utility.app.roomListOpen = false
                }
            }

            Button {
                objectName: "palaceRoomListLounge"
                width: 150
                height: 25
                text: utility.app.roomTitle === "Lounge"
                    ? "Lounge (current)" : "Lounge"
                enabled: utility.app.ready
                    && (utility.app.roomTitle !== "Atrium"
                        || !utility.app.doorBlocked)
                onClicked: {
                    utility.app.selectFixedRoom("lounge")
                    utility.app.roomListOpen = false
                }
            }
        }
    }

    // Prop bag panel (compositional identity: wear/drop assigned prop).
    Rectangle {
        objectName: "palacePropBagPanel"
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        anchors.bottomMargin: 48
        width: 200
        height: 132
        radius: 4
        color: "#f0f0f0"
        border.color: "#404040"
        z: 25
        visible: utility.app.propBagOpen

        Column {
            anchors.fill: parent
            anchors.margins: 8
            spacing: 6

            Text {
                text: "Prop bag"
                font.bold: true
                font.pixelSize: 12
                color: "#000000"
            }
            Text {
                width: parent.width
                wrapMode: Text.Wrap
                font.pixelSize: 10
                color: "#222222"
                text: utility.app.availablePropId.length > 0
                    ? ("Assigned prop: " + utility.app.availablePropId
                       + (utility.app.localWornPropId
                          === utility.app.availablePropId
                          ? " (worn)" : " (in bag)"))
                    : "No assigned prop yet. Operator can set one in Assets."
            }
            Row {
                spacing: 6
                Button {
                    text: "Wear"
                    width: 72
                    height: 26
                    enabled: utility.app.ready
                        && utility.app.availablePropId.length > 0
                        && utility.app.localWornPropId
                            !== utility.app.availablePropId
                    onClicked: utility.app.wearProp(
                        utility.app.availablePropId)
                }
                Button {
                    text: "Remove"
                    width: 72
                    height: 26
                    enabled: utility.app.ready
                        && utility.app.availablePropId.length > 0
                        && utility.app.localWornPropId
                            === utility.app.availablePropId
                    onClicked: utility.app.removeProp(
                        utility.app.availablePropId)
                }
            }
            Button {
                text: "Close bag"
                width: 100
                height: 24
                onClicked: utility.app.propBagOpen = false
            }
        }
    }

    // Operator User List (classic compact list with Ban ≈ Kill).
    Rectangle {
        objectName: "palaceModerationPanel"
        anchors.right: parent.right
        anchors.top: parent.top
        anchors.margins: 8
        anchors.topMargin: 42
        width: 176
        height: Math.min(
            380, 300
            + (utility.app.availablePropId.length > 0 ? 28 : 0)
            + Math.min(utility.app.participants.length, 32) * 2)
        radius: 2
        color: "#f5f5f5"
        border.color: "#404040"
        z: 20
        visible: utility.app.userListOpen

        Column {
            anchors.fill: parent
            anchors.margins: 6
            spacing: 3

            Text {
                text: "User List"
                color: "#000000"
                font.bold: true
                font.pixelSize: 11
            }

            Flickable {
                objectName: "palaceModerationRoster"
                width: 164
                height: Math.max(
                    24,
                    parent.height
                    - 100
                    - (utility.app.availablePropId.length > 0 ? 28 : 0))
                contentWidth: width
                contentHeight: moderationRosterContent.implicitHeight
                clip: true
                boundsBehavior: Flickable.StopAtBounds
                flickableDirection: Flickable.VerticalFlick

                Column {
                    id: moderationRosterContent
                    width: parent.width
                    spacing: 3

                    Repeater {
                        model: utility.app.participants.length

                        delegate: Button {
                            id: rosterUser
                            objectName: "palaceModerationRosterUser"
                            required property int index
                            property var participant:
                                utility.app.participants[index] || ({})
                            property string participantName:
                                String(participant.displayName
                                       || participant.userId || "Unknown")
                            property string participantUserId:
                                String(participant.userId || "")
                            width: 164
                            height: 24
                            text: participantName
                            font.pixelSize: 10
                            checkable: true
                            checked: utility.app.selectedModerationUserId
                                === participantUserId
                            enabled: participantUserId.length > 0
                            Accessible.name: "Select " + participantName
                            ToolTip.visible: hovered
                            ToolTip.text: "Select " + participantName
                            onClicked: utility.app.selectedModerationUserId
                                = participantUserId

                            contentItem: Text {
                                anchors.left: parent.left
                                anchors.leftMargin: 6
                                anchors.right: parent.right
                                anchors.rightMargin: 6
                                anchors.verticalCenter: parent.verticalCenter
                                text: rosterUser.text
                                elide: Text.ElideRight
                                color: "#000000"
                                font: rosterUser.font
                                verticalAlignment: Text.AlignVCenter
                            }

                            background: Rectangle {
                                color: rosterUser.checked
                                    ? "#b6d8ff" : "transparent"
                                border.color: rosterUser.checked
                                    ? "#34699a" : "transparent"
                            }
                        }
                    }
                }
            }

            Rectangle {
                objectName: "palaceModerationUserFooter"
                width: 164
                // Label plus Ban and Delegate must remain separate hit targets.
                // At 46 px the Delegate button extended into the prop action
                // below it, so clicks could submit the wrong mutation.
                height: 70
                color: "#e8e8e8"
                border.color: "#9a9a9a"

                Column {
                    anchors.fill: parent
                    anchors.margins: 3
                    spacing: 2

                    Text {
                        width: parent.width
                        text: utility.app.selectedModerationUserName().length > 0
                            ? "Selected: "
                              + utility.app.selectedModerationUserName()
                            : "Select a user"
                        color: "#333333"
                        font.pixelSize: 9
                        elide: Text.ElideRight
                    }

                    Button {
                        objectName: "palaceBanUserButton"
                        property string subjectUserId:
                            utility.app.selectedModerationUserId
                        width: parent.width
                        height: 22
                        text: utility.app.selectedModerationUserName().length > 0
                            ? "Ban " + utility.app.selectedModerationUserName()
                            : "Ban selected user"
                        font.pixelSize: 9
                        visible: utility.app.canBanUser
                        enabled: utility.app.canBanUser
                            && subjectUserId.length === 64
                            && utility.app.selectedModerationUser() !== null
                        Accessible.name: text
                        ToolTip.visible: hovered
                        ToolTip.text: "Ban selected user"
                        onClicked: utility.app.banUser(subjectUserId)
                    }

                    Button {
                        objectName: "palaceDelegateModeratorButton"
                        property string subjectUserId:
                            utility.app.selectedModerationUserId
                        width: parent.width
                        height: 22
                        text: "Make moderator"
                        font.pixelSize: 9
                        visible: utility.app.canDelegateModerator
                        enabled: utility.app.canDelegateModerator
                            && subjectUserId.length === 64
                            && utility.app.selectedModerationUser() !== null
                        Accessible.name: text
                        ToolTip.visible: hovered
                        ToolTip.text: "Grant moderation and room-lock capability"
                        onClicked: utility.app.delegateModerator(subjectUserId)
                    }
                }
            }

            Button {
                objectName: "palaceBanAssignedPropButton"
                width: 148
                height: 24
                text: "Ban assigned prop"
                font.pixelSize: 9
                visible: utility.app.canBanProp
                    && utility.app.availablePropId.length > 0
                enabled: utility.app.canBanProp
                onClicked: utility.app.banProp(
                    utility.app.availablePropId)
            }

            Button {
                objectName: "palaceRoomLockButton"
                width: 148
                height: 24
                text: utility.app.roomLocked
                    ? "Unlock " + utility.app.roomTitle
                    : "Lock " + utility.app.roomTitle
                font.pixelSize: 9
                visible: utility.app.canSetRoomLock
                enabled: utility.app.canSetRoomLock
                onClicked: utility.app.setRoomLocked(
                    utility.app.roomTitle, !utility.app.roomLocked)
            }

            Text {
                objectName: "palaceModerationStatus"
                width: 164
                text: "Moderation: "
                    + (utility.app.encodedStatusValue(
                        utility.app.moderationState, "state") || "idle")
                color: "#333333"
                elide: Text.ElideRight
                font.pixelSize: 9
            }
        }
    }
}
