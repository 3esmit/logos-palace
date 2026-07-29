pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "palaceGate2Root"

    readonly property var backend: logos.module("logos_palace_ui")
    readonly property string roomTitle: backend ? backend.roomTitle : "Connecting..."
    readonly property string roomBackgroundHandle: backend ? backend.roomBackgroundHandle : ""
    readonly property string syncHealth: backend ? backend.syncHealth : "recovering"
    readonly property string deliveryStatus: backend
        ? backend.deliverySessionStatus : "state=unavailable"
    readonly property string storageStatus: backend
        ? backend.storageStatus : "storage=unavailable"
    readonly property string lezState: backend
        ? backend.lezState : "wallet=closed;ready=0"
    readonly property string identityState: backend
        ? backend.identityState : "identity=none"
    readonly property string palaceState: backend
        ? backend.palaceState : "palace=closed;reason=not-opened"
    readonly property string moderationState: backend
        ? backend.moderationState
        : "state=idle;kind=;action=;target="
    readonly property string spotState: backend
        ? backend.spotState
        : "vm=idle;action=;navigation=0;reason=not-started"
    readonly property string spotActionId: backend
        ? backend.spotActionId : ""
    readonly property string spotReceipt: backend
        ? backend.spotReceipt : ""
    readonly property string participantProjection: backend
        ? backend.participantProjection : "[]"
    readonly property string nodeEvidence: backend
        ? backend.deliveryNodeEvidence
        : "{\"success\":false,\"reason\":\"node-not-created\"}"
    property string invocationError: ""
    property int invocationSequence: 0
    property string acceptanceRoundTripResponse: ""
    readonly property string lastActionReceipt: invocationError.length > 0
        ? invocationError : (backend ? backend.lastActionReceipt : "")

    // Stable inspector contract used by the compiled Gate 2 harness.
    readonly property string gate2Status: deliveryStatus
    readonly property string gate2Projection: participantProjection
    readonly property string gate2NodeEvidence: nodeEvidence
    readonly property string gate2Receipt: lastActionReceipt
    readonly property int gate2ParticipantCount: participants.length
    readonly property bool gate2Ready: ready

    // Stable inspector contract used by the compiled Gate 3 harness.
    readonly property string gate3Status: storageStatus
    readonly property string gate3Receipt: lastActionReceipt
    readonly property string gate3RoomHandle: roomBackgroundHandle
    readonly property bool gate3Ready: ready

    // Stable inspector contract used by the compiled Gate 4 harness.
    readonly property string gate4LezState: lezState
    readonly property string gate4IdentityState: identityState
    readonly property string gate4PalaceState: palaceState
    readonly property string gate4Receipt: lastActionReceipt
    readonly property string gate4ModerationState: moderationState
    readonly property bool gate4Ready: ready

    // Stable inspector contract used by the compiled Gate 5 harness.
    readonly property string gate5Action: spotActionId
    readonly property string gate5Status: spotState
    readonly property string gate5RoomTitle: roomTitle
    readonly property string gate5Receipt: invocationError.length > 0
        ? invocationError : spotReceipt
    readonly property bool gate5Ready: ready
    readonly property string gate5VmPhase:
        encodedStatusValue(spotState, "vm")
    readonly property bool gate5DoorBlocked:
        spotActionId.length > 0
        && gate5VmPhase !== "promoted"
        && gate5VmPhase !== "idle"

    readonly property var participants: parseParticipants(participantProjection)
    readonly property int connectedPeerCount: parseConnectedPeerCount(nodeEvidence)
    property bool ready: false
    property int localMotionX: 5000
    property int localMotionY: 6200
    property bool localWearingHat: false

    function parseParticipants(encoded) {
        try {
            var decoded = JSON.parse(encoded)
            if (!Array.isArray(decoded))
                return []
            var present = []
            for (var index = 0; index < decoded.length; ++index) {
                if (decoded[index] && decoded[index].present !== false)
                    present.push(decoded[index])
            }
            return present
        } catch (error) {
            return []
        }
    }

    function parseConnectedPeerCount(encoded) {
        try {
            var evidence = JSON.parse(encoded)
            if (!evidence || evidence.success !== true)
                return 0
            var peers = evidence.connectedPeers
            if (Array.isArray(peers))
                return peers.length
            if (!peers || typeof peers !== "object")
                return 0
            var count = 0
            for (var peerId in peers) {
                if (Object.prototype.hasOwnProperty.call(peers, peerId))
                    ++count
            }
            return count
        } catch (error) {
            return 0
        }
    }

    function encodedStatusValue(encoded, name) {
        var fields = String(encoded).split(";")
        var prefix = name + "="
        for (var index = 0; index < fields.length; ++index) {
            if (fields[index].indexOf(prefix) === 0)
                return fields[index].slice(prefix.length)
        }
        return ""
    }

    function statusValue(name) {
        return encodedStatusValue(deliveryStatus, name)
    }

    function clampCoordinate(value) {
        var coordinate = Number(value)
        if (isNaN(coordinate) || !isFinite(coordinate))
            return 0
        return Math.max(0, Math.min(10000, Math.round(coordinate)))
    }

    function protocolCoordinate(value, fallbackIndex, count) {
        if (value === null || value === undefined || value === "") {
            return Math.round((fallbackIndex + 1) * 10000
                              / Math.max(2, count + 1))
        }
        var coordinate = Number(value)
        if (isNaN(coordinate) || !isFinite(coordinate))
            return 5000
        return clampCoordinate(coordinate)
    }

    function roomX(coordinate) {
        var usableWidth = Math.max(1, roomCanvas.width - 180)
        return 90 + clampCoordinate(coordinate) * usableWidth / 10000
    }

    function roomY(coordinate) {
        var usableHeight = Math.max(1, roomCanvas.height - 330)
        return 110 + clampCoordinate(coordinate) * usableHeight / 10000
    }

    function rejectedNotReady() {
        invocationError = "rejected=ui-not-ready"
        ++invocationSequence
        return invocationError
    }

    function watchAction(pendingCall, onAccepted) {
        invocationError = ""
        logos.watch(pendingCall, function (value) {
            var receipt = String(value)
            if (receipt.indexOf("rejected=") !== 0 && onAccepted)
                onAccepted(receipt)
            ++invocationSequence
        }, function (error) {
            invocationError = "rejected=ui-remote-call;" + String(error)
            ++invocationSequence
        })
        return "pending"
    }

    function acceptanceApplicationRoundTrip(payload) {
        if (!ready || !backend) {
            acceptanceRoundTripResponse = rejectedNotReady()
            return acceptanceRoundTripResponse
        }
        invocationError = ""
        logos.watch(
            backend.applicationRoundTrip(String(payload)),
            function (value) {
                acceptanceRoundTripResponse = String(value)
                ++invocationSequence
            },
            function (error) {
                acceptanceRoundTripResponse =
                    "rejected=ui-remote-call;" + String(error)
                invocationError = acceptanceRoundTripResponse
                ++invocationSequence
            })
        return "pending"
    }

    function gate2Start(configJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.startDelivery(String(configJson)), null)
    }

    function gate1EnterRoom(roomId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.enterRoom(String(roomId)), null)
    }

    function gate2Say(text) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.say(String(text)), null)
    }

    function gate2Move(x, y) {
        if (!ready || !backend)
            return rejectedNotReady()
        var nextX = Math.round(Number(x))
        var nextY = Math.round(Number(y))
        return watchAction(backend.moveAvatar(nextX, nextY), function () {
            localMotionX = clampCoordinate(nextX)
            localMotionY = clampCoordinate(nextY)
        })
    }

    function gate2Wear(propId) {
        if (!ready || !backend)
            return rejectedNotReady()
        var selectedProp = String(propId)
        return watchAction(backend.wearProp(selectedProp), function () {
            if (selectedProp === "hat")
                localWearingHat = true
        })
    }

    function gate2Remove(propId) {
        if (!ready || !backend)
            return rejectedNotReady()
        var selectedProp = String(propId)
        return watchAction(backend.removeProp(selectedProp), function () {
            if (selectedProp === "hat")
                localWearingHat = false
        })
    }

    function gate2RefreshPresence() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshPresence(), null)
    }

    function gate3StartStorage(configJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.startStorage(String(configJson)), null)
    }

    function gate3FetchPng(sourceCid, derivativeCid, byteLength,
                           contentSha256, width, height) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.fetchPngDerivative(
                String(sourceCid),
                String(derivativeCid),
                Math.round(Number(byteLength)),
                String(contentSha256),
                Math.round(Number(width)),
                Math.round(Number(height))),
            null)
    }

    function gate3AssetStatus(derivativeCid) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.assetStatus(String(derivativeCid)), null)
    }

    function gate3PublishPng(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.publishVerifiedPng(String(handle)), null)
    }

    function gate3PublicationStatus(handle) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.publicationStatus(String(handle)), null)
    }

    function gate3PublishBundle() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.publishMvpStorageBundle(), null)
    }

    function gate3BundleStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.mvpStorageBundleStatus(), null)
    }

    function gate3FetchBundle(catalogBase64) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.fetchMvpStorageBundle(String(catalogBase64)), null)
    }

    function gate3VerifyRetention() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.verifyMvpStorageRetention(), null)
    }

    function gate3ObjectStatus(objectId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.storageObjectStatus(String(objectId)), null)
    }

    function gate3StorageStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.storageSessionStatus(), null)
    }

    function gate4StartLez(password) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.startLez(String(password)), null)
    }

    function gate4CreateIdentity(displayName) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.createIdentity(String(displayName)), null)
    }

    function gate4OpenPalace(palaceUri) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.openPalace(String(palaceUri)), null)
    }

    function gate4PalaceStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshPalace(), null)
    }

    function gate4RefreshLez() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshLez(), null)
    }

    function gate4RefreshIdentity() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshIdentity(), null)
    }

    function gate4BanUser(subjectUserIdHex) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.banUser(String(subjectUserIdHex)), null)
    }

    function gate4BanProp(propId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.banProp(String(propId)), null)
    }

    function gate4RefreshModeration() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshModeration(), null)
    }

    function gate4Submit(actionId, stateAccountIdHex,
                         callerAccountIdHex, programIdHex,
                         transitionJson) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.submitPalaceTransition(
                String(actionId),
                String(stateAccountIdHex),
                String(callerAccountIdHex),
                String(programIdHex),
                String(transitionJson)),
            null)
    }

    function gate4Observe(actionId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.observePalaceTransition(String(actionId)), null)
    }

    function gate4Reconcile(actionId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.reconcilePalaceTransition(String(actionId)), null)
    }

    function gate4ActionStatus(actionId) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.actionStatus(String(actionId)), null)
    }

    function gate5UseDoor() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.useSpot("door"), null)
    }

    function gate5PreviewDoor() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.previewSpot("door"), null)
    }

    function gate5ActionStatus() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.refreshSpot(), null)
    }

    function gate5Reconcile() {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(backend.reconcileSpot(), null)
    }

    function gate5VmTurnMetrics(actionId, phase) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAction(
            backend.vmTurnMetrics(
                String(actionId), String(phase)),
            null)
    }

    Connections {
        target: logos
        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "logos_palace_ui")
                root.ready = isReady && root.backend !== null
        }
    }

    Component.onCompleted: {
        root.ready = root.backend !== null
            && logos.isViewModuleReady("logos_palace_ui")
    }

    Rectangle {
        anchors.fill: parent
        color: "#15110d"

        Rectangle {
            id: roomCanvas
            objectName: "palaceRoomCanvas"
            anchors.fill: parent
            anchors.margins: 20
            color: "#312a24"
            radius: 10
            border.color: "#d5b77a"
            border.width: 3
            clip: true

            Image {
                id: roomBackground
                objectName: "palaceRoomBackground"
                anchors.fill: parent
                source: root.roomBackgroundHandle.length === 64
                    ? "image://basecamp-verified/" + root.roomBackgroundHandle
                    : ""
                fillMode: Image.PreserveAspectCrop
                smooth: false
            }

            Rectangle {
                objectName: "palaceRoomBackgroundPlaceholder"
                anchors.fill: parent
                color: "#312a24"
                visible: roomBackground.status !== Image.Ready

                Text {
                    anchors.centerIn: parent
                    text: "Verified room art unavailable"
                    color: "#f3c36b"
                    font.pixelSize: 16
                }
            }

            Rectangle {
                anchors.fill: parent
                color: "#1b130d"
                opacity: roomBackground.status === Image.Ready ? 0.18 : 0
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 20
                text: root.roomTitle
                font.pixelSize: 30
                font.bold: true
                color: "#fff2cf"
                z: 8
            }

            Text {
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.top: parent.top
                anchors.topMargin: 58
                text: root.participants.length + " participant"
                    + (root.participants.length === 1 ? "" : "s")
                color: "#e5d2aa"
                font.pixelSize: 12
                z: 8
            }

            Item {
                id: participantLayer
                objectName: "palaceParticipants"
                anchors.fill: parent
                z: 4

                Repeater {
                    model: root.participants.length

                    delegate: Item {
                        id: participantDelegate
                        objectName: "palaceParticipant"
                        width: 94
                        height: 116

                        required property int index
                        property var participant:
                            root.participants[index] || ({})
                        property string participantUserId:
                            String(participant.userId || "")
                        property string participantDisplayName:
                            String(participant.displayName || participantUserId)
                        property real participantMotionX:
                            participant.x === null || participant.x === undefined
                                ? -1 : Number(participant.x)
                        property real participantMotionY:
                            participant.y === null || participant.y === undefined
                                ? -1 : Number(participant.y)
                        property real participantX: participantMotionX
                        property real participantY: participantMotionY
                        property real layoutMotionX: root.protocolCoordinate(
                            participant.x, index, root.participants.length)
                        property real layoutMotionY: root.protocolCoordinate(
                            participant.y, root.participants.length - index - 1,
                            root.participants.length)
                        property string participantSpeech:
                            String(participant.speech || "")
                        property var participantPropList:
                            Array.isArray(participant.props)
                                ? participant.props : []
                        property string participantProps:
                            JSON.stringify(participantPropList)

                        x: root.roomX(layoutMotionX) - width / 2
                        y: root.roomY(layoutMotionY) - height / 2

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

                        Rectangle {
                            id: remoteSpeechCard
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.bottom: remoteAvatar.top
                            anchors.bottomMargin: 9
                            width: Math.min(190, Math.max(
                                88, remoteSpeechText.implicitWidth + 24))
                            height: remoteSpeechText.implicitHeight + 16
                            radius: 12
                            color: "#fff8e7"
                            border.color: "#8c7145"
                            visible: participantDelegate.participantSpeech.length > 0

                            Text {
                                id: remoteSpeechText
                                objectName: "palaceSpeechBubble"
                                property string participantUserId:
                                    participantDelegate.participantUserId
                                anchors.centerIn: parent
                                width: Math.min(164, implicitWidth)
                                text: participantDelegate.participantSpeech
                                color: "#2b2016"
                                font.pixelSize: 13
                                wrapMode: Text.Wrap
                                horizontalAlignment: Text.AlignHCenter
                            }
                        }

                        Rectangle {
                            id: remoteAvatar
                            anchors.horizontalCenter: parent.horizontalCenter
                            anchors.verticalCenter: parent.verticalCenter
                            width: 66
                            height: 66
                            radius: width / 2
                            color: "#68a3a0"
                            border.color: "#d9ffef"
                            border.width: 3

                            Text {
                                anchors.centerIn: parent
                                text: participantDelegate.participantDisplayName.length > 0
                                    ? participantDelegate.participantDisplayName
                                          .charAt(0).toUpperCase()
                                    : "?"
                                color: "#102322"
                                font.pixelSize: 24
                                font.bold: true
                            }
                        }

                        Item {
                            objectName: "palaceWornProp"
                            property string participantUserId:
                                participantDelegate.participantUserId
                            property string propId: "hat"
                            anchors.horizontalCenter: remoteAvatar.horizontalCenter
                            anchors.bottom: remoteAvatar.top
                            anchors.bottomMargin: -8
                            width: 54
                            height: 29
                            visible: participantDelegate.participantPropList
                                .indexOf("hat") !== -1

                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.top: parent.top
                                width: 34
                                height: 19
                                radius: 3
                                color: "#8b4f2f"
                                border.color: "#f6cc78"
                            }
                            Rectangle {
                                anchors.horizontalCenter: parent.horizontalCenter
                                anchors.bottom: parent.bottom
                                width: 54
                                height: 8
                                radius: 4
                                color: "#8b4f2f"
                                border.color: "#f6cc78"
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
                                text: participantDelegate.participantDisplayName
                                color: "#fff2cf"
                                font.pixelSize: 12
                                font.bold: true
                            }
                        }
                    }
                }
            }

            Button {
                objectName: "palaceRoomDoor"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: actionDock.top
                anchors.bottomMargin: 12
                text: root.roomTitle !== "Atrium"
                    ? "Door to Atrium"
                    : (root.gate5DoorBlocked
                       ? "Door finalizing…" : "Door to Lounge")
                enabled: root.ready
                    && (root.roomTitle !== "Atrium"
                        || !root.gate5DoorBlocked)
                z: 10
                onClicked: {
                    if (root.roomTitle === "Atrium") {
                        root.gate5UseDoor()
                    } else {
                        root.watchAction(
                            root.backend.enterRoom("atrium"), null)
                    }
                }
            }

            Rectangle {
                id: actionDock
                objectName: "palaceActionDock"
                anchors.horizontalCenter: parent.horizontalCenter
                anchors.bottom: parent.bottom
                anchors.bottomMargin: 14
                width: Math.max(320, Math.min(parent.width - 32, 820))
                height: 112
                radius: 14
                color: "#211a14ed"
                border.color: "#846b45"
                z: 10

                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 12
                    spacing: 14

                    ColumnLayout {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 160
                        spacing: 5

                        Text {
                            text: "Talk in the room"
                            color: "#fff2cf"
                            font.bold: true
                            font.pixelSize: 12
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            TextField {
                                id: chatInput
                                objectName: "palaceChatInput"
                                Layout.fillWidth: true
                                placeholderText: "Say something"
                                maximumLength: 280
                                enabled: root.ready
                                onAccepted: {
                                    if (text.length > 0) {
                                        root.gate2Say(text)
                                        clear()
                                    }
                                }
                            }

                            Button {
                                objectName: "palaceSayButton"
                                text: "Send"
                                enabled: root.ready && chatInput.text.length > 0
                                onClicked: {
                                    root.gate2Say(chatInput.text)
                                    chatInput.clear()
                                }
                            }
                        }

                        Text {
                            objectName: "palaceLastActionReceipt"
                            Layout.fillWidth: true
                            text: root.lastActionReceipt.length > 0
                                ? root.lastActionReceipt : "No action yet"
                            color: root.lastActionReceipt.indexOf("rejected=") === 0
                                   || root.lastActionReceipt.indexOf(
                                       "degraded;reason=") === 0
                                ? "#ff9c8f" : "#b9dcae"
                            font.pixelSize: 10
                            elide: Text.ElideRight
                        }
                    }

                    GridLayout {
                        columns: 3
                        rowSpacing: 2
                        columnSpacing: 2

                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                        Button {
                            objectName: "palaceMoveUp"
                            text: "↑"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX, root.localMotionY - 750)
                        }
                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                        Button {
                            objectName: "palaceMoveLeft"
                            text: "←"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX - 750, root.localMotionY)
                        }
                        Rectangle {
                            Layout.preferredWidth: 12
                            Layout.preferredHeight: 12
                            radius: 6
                            color: "#d5b77a"
                        }
                        Button {
                            objectName: "palaceMoveRight"
                            text: "→"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX + 750, root.localMotionY)
                        }
                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                        Button {
                            objectName: "palaceMoveDown"
                            text: "↓"
                            Layout.preferredWidth: 38
                            Layout.preferredHeight: 28
                            enabled: root.ready
                            onClicked: root.gate2Move(
                                root.localMotionX, root.localMotionY + 750)
                        }
                        Item { Layout.preferredWidth: 30; Layout.preferredHeight: 26 }
                    }

                    ColumnLayout {
                        spacing: 5

                        Button {
                            objectName: "palaceWearHat"
                            text: "Wear hat"
                            enabled: root.ready && !root.localWearingHat
                            onClicked: root.gate2Wear("hat")
                        }
                        Button {
                            objectName: "palaceRemoveHat"
                            text: "Remove hat"
                            enabled: root.ready && root.localWearingHat
                            onClicked: root.gate2Remove("hat")
                        }
                    }
                }
            }
        }

        Rectangle {
            anchors.left: parent.left
            anchors.top: parent.top
            anchors.margins: 34
            width: 132
            height: 76
            color: "#211a14dd"
            radius: 8
            z: 20

            Column {
                anchors.centerIn: parent
                spacing: 5
                Text {
                    text: "Rooms"
                    color: "#fff2cf"
                    font.bold: true
                }
                Text {
                    text: "Atrium\nLounge"
                    color: "#f0dfba"
                    font.pixelSize: 12
                }
            }
        }

        Rectangle {
            objectName: "palaceModerationPanel"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.rightMargin: 34
            anchors.topMargin: 190
            width: 230
            height: 214
            radius: 8
            color: "#211a14ee"
            border.color: "#846b45"
            z: 20

            Column {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 5

                Text {
                    text: "Moderation"
                    color: "#fff2cf"
                    font.bold: true
                    font.pixelSize: 13
                }

                Repeater {
                    model: root.participants.length

                    delegate: Row {
                        required property int index
                        property var participant:
                            root.participants[index] || ({})
                        property string participantName:
                            String(participant.displayName
                                   || participant.userId || "Unknown")
                        property string participantUserId:
                            String(participant.userId || "")
                        width: 210
                        height: 28
                        spacing: 6

                        Text {
                            width: 126
                            anchors.verticalCenter: parent.verticalCenter
                            text: parent.participantName
                            elide: Text.ElideRight
                            color: "#e5d2aa"
                            font.pixelSize: 11
                        }

                        Button {
                            objectName: "palaceBanUserButton"
                            property string subjectUserId:
                                parent.participantUserId
                            width: 72
                            height: 26
                            text: "Ban user"
                            enabled: root.ready
                                && subjectUserId.length === 64
                            onClicked: root.gate4BanUser(subjectUserId)
                        }
                    }
                }

                Button {
                    objectName: "palaceBanHatButton"
                    width: 104
                    height: 28
                    text: "Ban hat prop"
                    enabled: root.ready
                    onClicked: root.gate4BanProp("hat")
                }

                Text {
                    objectName: "palaceModerationStatus"
                    width: 210
                    text: "Status: "
                        + (root.encodedStatusValue(
                            root.moderationState, "state") || "idle")
                        + (root.encodedStatusValue(
                            root.moderationState, "action").length > 0
                           ? " · action "
                             + root.encodedStatusValue(
                                 root.moderationState, "action")
                           : "")
                    color: root.encodedStatusValue(
                        root.moderationState, "state") === "finalized"
                        ? "#a7e3a0"
                        : (root.encodedStatusValue(
                               root.moderationState, "state") === "rejected"
                           ? "#ff9c8f" : "#f3c36b")
                    elide: Text.ElideRight
                    font.pixelSize: 10
                }
            }
        }

        Rectangle {
            objectName: "palaceDeliveryStatus"
            anchors.right: parent.right
            anchors.top: parent.top
            anchors.margins: 34
            width: 196
            height: 140
            radius: 8
            color: "#211a14dd"
            z: 20

            Column {
                anchors.centerIn: parent
                spacing: 3

                Text {
                    id: deliveryStatusText
                    text: root.ready
                        ? "Delivery: " + (root.statusValue("state") || "offline")
                        : "Connecting"
                    color: root.statusValue("state") === "online"
                        ? "#a7e3a0" : "#f3c36b"
                    font.bold: true
                    font.pixelSize: 12
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: root.connectedPeerCount + " connected peer"
                        + (root.connectedPeerCount === 1 ? "" : "s")
                    color: "#e5d2aa"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "LEZ: "
                        + (root.encodedStatusValue(
                            root.lezState, "ready") === "1"
                           ? "synchronized" : "offline")
                    color: root.encodedStatusValue(
                        root.lezState, "ready") === "1"
                        ? "#a7e3a0" : "#f3c36b"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Palace: "
                        + (root.encodedStatusValue(
                            root.palaceState, "palace") || "closed")
                    color: root.encodedStatusValue(
                        root.palaceState, "palace") === "open"
                        ? "#a7e3a0" : "#f3c36b"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Assets: "
                        + (root.lastActionReceipt.indexOf(
                               "degraded;reason=") === 0
                           ? "degraded"
                           : (root.encodedStatusValue(
                                  root.storageStatus, "state")
                              || root.encodedStatusValue(
                                  root.storageStatus, "storage")
                              || "offline"))
                    color: root.lastActionReceipt.indexOf(
                               "degraded;reason=") === 0
                           || root.storageStatus.indexOf("degraded") !== -1
                        ? "#ff9c8f" : "#e5d2aa"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Projection: " + root.syncHealth
                    color: root.syncHealth === "fully_synchronized"
                        ? "#a7e3a0" : "#f3c36b"
                    font.pixelSize: 10
                }
                Text {
                    anchors.horizontalCenter: parent.horizontalCenter
                    text: "Door: " + (root.gate5VmPhase || "idle")
                    color: root.gate5VmPhase === "degraded"
                        ? "#ff9c8f"
                        : (root.gate5VmPhase === "promoted"
                           ? "#a7e3a0" : "#e5d2aa")
                    font.pixelSize: 10
                }
            }
        }
    }
}
