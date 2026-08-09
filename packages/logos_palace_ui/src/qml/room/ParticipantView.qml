import QtQuick

Item {
    id: participantView

    required property Item app
    required property var participant
    required property int participantIndex
    required property int participantCount

    objectName: "palaceParticipant"
    width: 94
    height: 116

    property string participantUserId: String(participant.userId || "")
    property string participantDisplayName:
        String(participant.displayName || participantUserId)
    property real participantMotionX:
        participant.x === null || participant.x === undefined
            ? -1 : Number(participant.x)
    property real participantMotionY:
        participant.y === null || participant.y === undefined
            ? -1 : Number(participant.y)
    property real layoutMotionX: app.protocolCoordinate(
        participant.x, participantIndex, participantCount)
    property real layoutMotionY: app.protocolCoordinate(
        participant.y, participantCount - participantIndex - 1,
        participantCount)
    property string participantSpeech: String(participant.speech || "")
    property var participantPropList: Array.isArray(participant.props)
        ? participant.props : []

    x: app.roomX(layoutMotionX) - width / 2
    y: app.roomY(layoutMotionY) - height / 2

    Behavior on x {
        NumberAnimation {
            duration: 180
            easing.type: Easing.OutQuad
        }
    }
    Behavior on y {
        NumberAnimation {
            duration: 180
            easing.type: Easing.OutQuad
        }
    }

    SpeechBubble {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: remoteAvatar.top
        anchors.bottomMargin: 9
        text: participantView.participantSpeech
        maximumHeight: participantView.app.roomCanvasSpeechMaximumHeight
    }

    // Classic "roundhead" presence disc (Palace default face).
    Rectangle {
        id: remoteAvatar
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.verticalCenter: parent.verticalCenter
        width: 54
        height: 54
        radius: width / 2
        color: "#ffe566"
        border.color: "#222222"
        border.width: 2

        Row {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.top: parent.top
            anchors.topMargin: 14
            spacing: 10
            Rectangle { width: 7; height: 9; radius: 3; color: "#111111" }
            Rectangle { width: 7; height: 9; radius: 3; color: "#111111" }
        }
        Rectangle {
            anchors.horizontalCenter: parent.horizontalCenter
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 12
            width: 18
            height: 9
            radius: 9
            color: "transparent"
            border.color: "#111111"
            border.width: 2
        }
        Text {
            anchors.bottom: parent.bottom
            anchors.bottomMargin: 6
            anchors.horizontalCenter: parent.horizontalCenter
            text: "ᴗ"
            color: "#111111"
            font.pixelSize: 16
            font.bold: true
        }
        Rectangle {
            anchors.centerIn: parent
            width: parent.width + 10
            height: parent.height + 10
            radius: width / 2
            color: "transparent"
            border.width: 2
            border.color: participantView.app.selectedModerationUserId
                === participantView.participantUserId
                ? "#2b6aa4" : "transparent"
        }
    }

    MouseArea {
        objectName: "palaceParticipantSelect"
        anchors.fill: remoteAvatar
        enabled: participantView.participantUserId.length > 0
        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
        acceptedButtons: Qt.LeftButton
        onClicked: participantView.app.selectedModerationUserId
            = participantView.participantUserId
    }

    Item {
        id: wornProp
        objectName: "palaceWornProp"
        property string participantUserId: participantView.participantUserId
        property var activeProp: participantView.app.activePropAsset
        property string propId: activeProp.available === true
            ? String(activeProp.propId)
            : (participantView.participantPropList.length > 0
               ? String(participantView.participantPropList[0]) : "")
        property string assetHandle: activeProp.available === true
            ? String(activeProp.handle) : ""
        property bool assetAvailable: activeProp.available === true
            && assetHandle.length === 64
        property real sourceWidth: assetAvailable
            ? Number(activeProp.width) : 42
        property real sourceHeight: assetAvailable
            ? Number(activeProp.height) : 18
        property real renderScale: assetAvailable
            ? Math.min(1, 72 / Math.max(1, sourceWidth),
                       72 / Math.max(1, sourceHeight)) : 1
        property real targetX: String(activeProp.layer) === "hand"
            ? remoteAvatar.x + remoteAvatar.width
            : (String(activeProp.layer) === "back"
               ? remoteAvatar.x
               : remoteAvatar.x + remoteAvatar.width)
        property real targetY: String(activeProp.layer) === "head"
            ? remoteAvatar.y + 8
            : (String(activeProp.layer) === "body"
               ? remoteAvatar.y + remoteAvatar.height / 2
               : remoteAvatar.y + remoteAvatar.height * 0.62)
        property string renderState: assetAvailable
            ? "verified-image" : "asset-pending"

        x: assetAvailable
            ? targetX - Number(activeProp.anchorX) * renderScale
            : remoteAvatar.x + (remoteAvatar.width - width) / 2
        y: assetAvailable
            ? targetY - Number(activeProp.anchorY) * renderScale
            : remoteAvatar.y - height + 8
        width: sourceWidth * renderScale
        height: sourceHeight * renderScale
        visible: participantView.participantPropList.indexOf(propId) !== -1
        z: assetAvailable && String(activeProp.layer) === "back" ? -1 : 1

        Image {
            anchors.fill: parent
            visible: wornProp.assetAvailable
            source: visible ? "image://basecamp-verified/" + wornProp.assetHandle : ""
            fillMode: Image.Stretch
            asynchronous: false
            smooth: true
        }
        Rectangle {
            anchors.fill: parent
            visible: !wornProp.assetAvailable
            radius: 4
            color: "#3b342cdd"
            border.color: "#f3c36b"
            Text {
                anchors.centerIn: parent
                text: "asset pending"
                color: "#fff2cf"
                font.pixelSize: 7
            }
        }
    }

    Rectangle {
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.top: remoteAvatar.bottom
        anchors.topMargin: 6
        width: Math.min(120, participantName.implicitWidth + 18)
        height: 24
        radius: 12
        color: "#211a14dd"

        Text {
            id: participantName
            anchors.centerIn: parent
            text: participantView.participantDisplayName
            color: "#fff2cf"
            font.pixelSize: 12
            font.bold: true
        }
    }
}
