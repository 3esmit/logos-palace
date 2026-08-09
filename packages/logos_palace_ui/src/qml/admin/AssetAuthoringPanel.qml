import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
id: panel
required property Item app
anchors.top: parent.top
anchors.right: parent.right
anchors.bottom: parent.bottom
width: Math.min(parent.width - 24, 560)
z: 90
onVisibleChanged: {
    if (!visible)
        app.resetAuthoringPreviewState()
}
objectName: "palaceBackgroundModeration"
radius: 8
color: "#f518130f"
border.color: "#d5b77a"
border.width: 2

ColumnLayout {
    anchors.fill: parent
    anchors.margins: 14
    spacing: 10

    RowLayout {
        Layout.fillWidth: true

        ColumnLayout {
            Layout.fillWidth: true
            spacing: 1

            Text {
                text: "Authoring · Palace assets"
                color: "#fff2cf"
                font.bold: true
                font.pixelSize: 18
            }
            Text {
                text: app.authoringAssets.length
                    + " staged PNG"
                    + (app.authoringAssets.length === 1
                       ? "" : "s")
                    + (app.canManageAssets
                       ? " · approve, upload, then assign"
                       : " · read-only catalog")
                color: "#c9b78e"
                font.pixelSize: 11
            }
            Text {
                Layout.fillWidth: true
                text: app.encodedStatusValue(
                    app.storageStatus, "storage")
                    === "running"
                    ? "Storage connected"
                    : "Start Storage in Logos Control, then connect it here."
                color: app.encodedStatusValue(
                    app.storageStatus, "storage")
                    === "running" ? "#a7e3a0" : "#e2c37b"
                font.pixelSize: 10
                elide: Text.ElideRight
            }
        }

        Button {
            objectName: "palaceConnectStorage"
            text: app.encodedStatusValue(
                app.storageStatus, "storage") === "running"
                ? "Storage connected" : "Connect Storage"
            enabled: app.ready && app.canManageAssets
                && app.encodedStatusValue(
                    app.storageStatus, "storage")
                    !== "running"
            ToolTip.visible: hovered
            ToolTip.text: "Start Storage in Logos Control first. Palace only connects to an already running node."
            onClicked: app.connectStorage()
        }

        Button {
            objectName: "palaceAssetSelectFile"
            text: app.assetImportRunning
                ? "Importing…" : "Add PNG…"
            enabled: app.ready
                && app.canManageAssets
                && !app.assetImportRunning
            onClicked: app.selectAssetFile()
        }

        Button {
            objectName: "palaceBackgroundModerationClose"
            text: "Close"
            onClicked: app.backgroundModerationOpen = false
        }
    }

    Rectangle {
        objectName: "palaceRoomSetupPublication"
        Layout.fillWidth: true
        Layout.preferredHeight: 44
        color: "#2b2118"
        border.color: "#8c7145"
        radius: 4

        RowLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 8

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 1

                Text {
                    text: "Room setup"
                    color: "#fff2cf"
                    font.bold: true
                    font.pixelSize: 11
                }
                Text {
                    objectName: "palaceRoomSetupPublishStatus"
                    Layout.fillWidth: true
                    text: app.roomSetupPublishMessage()
                    color: app.canPublishRoomSetup
                        ? "#a7e3a0"
                        : (app.roomSetupPublishReadiness
                           === "locked"
                           ? "#f3cf8a" : "#d8a18f")
                    font.pixelSize: 10
                    elide: Text.ElideRight
                }
            }

            BusyIndicator {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                running: app.roomSetupPublishReadiness
                    === "locked"
                visible: running
            }

            Button {
                objectName: "palacePublishRoomSetup"
                text: "Publish room setup"
                Accessible.name: text
                visible: app.canManageAssets
                enabled: app.canPublishRoomSetup
                ToolTip.visible: hovered && !enabled
                ToolTip.text: app.roomSetupPublishMessage()
                onClicked: {
                    if (app.onboardingPhase === "authoring-rooms")
                        app.publishRoomSetup()
                    else
                        app.storagePublishBundle()
                }
            }
        }
    }

    RowLayout {
        objectName: "palaceSharedInvitationRow"
        Layout.fillWidth: true
        spacing: 6
        visible: app.sharedPalaceInvitation.length > 0

        Text {
            text: "Invitation"
            color: "#c9b78e"
            font.pixelSize: 10
        }

        TextField {
            id: sharedInvitationField
            objectName: "palaceSharedInvitation"
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            maximumLength: 32768
            text: app.sharedPalaceInvitation
            Accessible.name: "Palace invitation"
            ToolTip.visible: hovered
            ToolTip.text: "Select and copy the invitation for Palace joiners."
        }

        Button {
            objectName: "palaceCopyInvitation"
            text: "Copy"
            Accessible.name: "Copy Palace invitation"
            enabled: sharedInvitationField.text.length > 0
            onClicked: {
                sharedInvitationField.selectAll()
                sharedInvitationField.copy()
            }
        }
    }

    RowLayout {
        objectName: "palaceSharedStorageCatalogRow"
        Layout.fillWidth: true
        spacing: 6
        visible: app.sharedStorageCatalog.length > 0

        Text {
            text: "Share catalog"
            color: "#c9b78e"
            font.pixelSize: 10
        }

        TextField {
            objectName: "palaceSharedStorageCatalog"
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            maximumLength: 16384
            text: app.sharedStorageCatalog
            Accessible.name: "Shared room catalog"
            ToolTip.visible: hovered
            ToolTip.text: "Select and copy this catalog for Palace joiners."
        }
    }

    RowLayout {
        objectName: "palaceSharedStoragePeerRow"
        Layout.fillWidth: true
        spacing: 6
        visible: app.sharedStoragePeerEndpoint.length > 0

        Text {
            text: "Share peer"
            color: "#c9b78e"
            font.pixelSize: 10
        }

        TextField {
            objectName: "palaceSharedStoragePeerEndpoint"
            Layout.fillWidth: true
            readOnly: true
            selectByMouse: true
            maximumLength: 16384
            text: app.sharedStoragePeerEndpoint
            Accessible.name: "Shared Storage peer endpoint"
            ToolTip.visible: hovered
            ToolTip.text: "Select and copy this endpoint for Palace joiners."
        }
    }

    RowLayout {
        Layout.fillWidth: true
        spacing: 6

        Text {
            text: "Prop placement"
            color: "#c9b78e"
            font.pixelSize: 11
        }

        TextField {
            objectName: "palaceAssetPropId"
            Layout.preferredWidth: 110
            placeholderText: "prop ID"
            enabled: app.canManageAssets
            text: app.propDraftId
            onTextEdited: app.propDraftId = text
        }

        TextField {
            objectName: "palaceAssetPropAnchorX"
            Layout.preferredWidth: 76
            placeholderText: "anchor X"
            inputMethodHints: Qt.ImhDigitsOnly
            enabled: app.canManageAssets
            text: app.propDraftAnchorX
            onTextEdited: app.propDraftAnchorX = text
        }

        TextField {
            objectName: "palaceAssetPropAnchorY"
            Layout.preferredWidth: 76
            placeholderText: "anchor Y"
            inputMethodHints: Qt.ImhDigitsOnly
            enabled: app.canManageAssets
            text: app.propDraftAnchorY
            onTextEdited: app.propDraftAnchorY = text
        }

        TextField {
            objectName: "palaceAssetPropLayer"
            Layout.preferredWidth: 92
            placeholderText: "layer"
            enabled: app.canManageAssets
            text: app.propDraftLayer
            onTextEdited: app.propDraftLayer = text
        }

        RowLayout {
            spacing: 3

            Repeater {
                model: ["head", "body", "hand", "back"]

                delegate: Button {
                    objectName:
                        "palaceAssetPropLayer-" + modelData
                    required property string modelData
                    text: modelData.slice(0, 1).toUpperCase()
                        + modelData.slice(1)
                    font.pixelSize: 9
                    Layout.preferredWidth: 46
                    checkable: true
                    checked:
                        app.propDraftLayer.trim() === modelData
                    enabled: app.canManageAssets
                    onClicked: app.propDraftLayer = modelData
                }
            }
        }

        Text {
            Layout.fillWidth: true
            text: app.canManageAssets
                ? (app.propDraftReady()
                   ? "Ready to assign"
                   : "Pick layer, then click prop preview to set hot spot")
                : app.assetAuthoringReadOnlyMessage()
            color: app.canManageAssets
                ? (app.propDraftReady()
                   ? "#a7e3a0" : "#8f826a")
                : "#d8a18f"
            elide: Text.ElideRight
            font.pixelSize: 10
        }
    }

    RowLayout {
        Layout.fillWidth: true
        visible: app.authoringAssets.length > 0

        Text {
            Layout.fillWidth: true
            text: "Asset actions"
            color: "#c9b78e"
            font.pixelSize: 10
        }

        Button {
            objectName: "palaceAssetShowTop"
            text: "First assets"
            enabled: app.authoringAssets.length > 0
            Accessible.name: "Show first asset actions"
            onClicked: backgroundGrid.contentY = 0
        }

        Button {
            objectName: "palaceAssetShowAll"
            text: backgroundGrid.contentHeight
                > backgroundGrid.height
                ? "Last assets" : "All assets visible"
            enabled: app.authoringAssets.length > 0
            Accessible.name: "Show last asset actions"
            onClicked: backgroundGrid.contentY = Math.max(
                0, backgroundGrid.contentHeight
                    - backgroundGrid.height)
        }
    }

    // Use a Flow/Repeater instead of GridView so every card and its
    // moderation controls stay instantiated for e2e discovery.
    // GridView recycling left later "Set Atrium/Lounge" buttons
    // unfindable after the first visible row was assigned.
    Flickable {
        id: backgroundGrid
        objectName: "palaceBackgroundGrid"
        Layout.fillWidth: true
        Layout.fillHeight: true
        clip: true
        contentWidth: width
        contentHeight: backgroundFlow.implicitHeight
        boundsBehavior: Flickable.StopAtBounds
        flickableDirection: Flickable.VerticalFlick

        Flow {
            id: backgroundFlow
            width: backgroundGrid.width
            spacing: 10

            Repeater {
                model: app.authoringAssets

                Rectangle {
                    id: backgroundCard
                    required property var modelData
                    property var asset: modelData || ({})
                    property string handle:
                        String(asset.handle || "")
                    property string publicationState:
                        String(asset.publicationState
                               || "not-uploaded")
                    property var assignedRooms:
                        Array.isArray(asset.roomAssignments)
                        ? asset.roomAssignments : []
                    property var assignedProps:
                        Array.isArray(asset.propAssignments)
                        ? asset.propAssignments : []
                    property bool previewCounted: false
                    // Two wide cards keep all three assignment
                    // actions readable; four narrow cards elide
                    // their labels before they can be operated.
                    property int cardWidth: Math.max(
                        320,
                        Math.floor(
                            (backgroundGrid.width
                             - backgroundFlow.spacing) / 2))
                    width: cardWidth
                    height: 334
                    radius: 8
                    color: "#292018"
                    border.color: publicationState === "published"
                        ? "#7ecb78" : "#62513b"

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 8
                        spacing: 5

                        Image {
                            id: backgroundPreview
                            Layout.fillWidth: true
                            Layout.preferredHeight: 112
                            source: String(
                                backgroundCard.asset.handle
                                || "").length === 64
                                ? "image://basecamp-verified/"
                                  + String(
                                      backgroundCard.asset.handle)
                                : ""
                            fillMode: Image.PreserveAspectCrop
                            smooth: true
                            asynchronous: false
                            onStatusChanged: {
                                if (status === Image.Ready) {
                                    backgroundCard.previewCounted =
                                        app.noteAuthoringPreviewReady(
                                            backgroundCard
                                                .previewCounted)
                                } else {
                                    backgroundCard.previewCounted =
                                        app.noteAuthoringPreviewLost(
                                            backgroundCard
                                                .previewCounted)
                                }
                                ++app.backgroundPreviewEpoch
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Text {
                                Layout.fillWidth: true
                                text: String(
                                    backgroundCard.asset.label
                                    || backgroundCard.handle)
                                color: "#fff2cf"
                                font.bold: true
                                elide: Text.ElideRight
                                font.pixelSize: 12
                            }

                            Text {
                                visible:
                                    backgroundCard.assignedRooms
                                        .length > 0
                                    || backgroundCard
                                        .assignedProps.length > 0
                                text: backgroundCard.assignedRooms
                                    .concat(
                                        backgroundCard
                                            .assignedProps)
                                    .join(" · ").toUpperCase()
                                color: "#a7e3a0"
                                font.bold: true
                                font.pixelSize: 9
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: Number(
                                backgroundCard.asset.width)
                                + "×"
                                + Number(
                                    backgroundCard.asset.height)
                                + " · "
                                + String(
                                    backgroundCard.asset
                                        .reviewState)
                                + " · "
                                + backgroundCard.publicationState
                            color: "#c9b78e"
                            elide: Text.ElideRight
                            font.pixelSize: 10
                        }

                        Text {
                            Layout.fillWidth: true
                            text: backgroundCard.publicationState
                                === "published"
                                ? "Published to Storage"
                                : backgroundCard.publicationState
                                    === "publishing"
                                ? "Publishing to Storage"
                                : "Selected asset · awaiting approval"
                            color: "#8f826a"
                            elide: Text.ElideRight
                            font.pixelSize: 9
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Button {
                                objectName:
                                    "palaceAssetApprove-"
                                    + backgroundCard.handle
                                Layout.fillWidth: true
                                Layout.minimumWidth: 156
                                text: backgroundCard
                                        .publicationState
                                        === "published"
                                    ? "Uploaded"
                                    : "Approve & upload"
                                enabled: app.ready
                                    && app.canManageAssets
                                    && backgroundCard
                                        .publicationState
                                        !== "published"
                                    && backgroundCard.handle
                                        .length > 0
                                onClicked:
                                    app.reviewAndPublishAsset(
                                        backgroundCard.handle)
                            }

                            Button {
                                objectName:
                                    "palaceAssetReject-"
                                    + backgroundCard.handle
                                Layout.minimumWidth: 84
                                text: "Reject"
                                enabled: app.ready
                                    && app.canManageAssets
                                    && backgroundCard
                                        .publicationState
                                        !== "published"
                                onClicked:
                                    app.reviewAsset(
                                        backgroundCard.handle,
                                        "reject")
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true

                            Button {
                                objectName:
                                    "palaceBackgroundAssignAtrium-"
                                    + backgroundCard.handle
                                Layout.fillWidth: true
                                Layout.minimumWidth: 96
                                text: "Set Atrium"
                                enabled: app.ready
                                    && app.canManageAssets
                                    && backgroundCard
                                        .publicationState
                                        === "published"
                                    && backgroundCard.handle
                                        .length === 64
                                onClicked:
                                    app.assignRoomBackground(
                                        "atrium",
                                        backgroundCard.handle)
                            }

                            Button {
                                objectName:
                                    "palaceAssetAssignProp-"
                                    + backgroundCard.handle
                                Layout.fillWidth: true
                                Layout.minimumWidth: 96
                                text: "Set prop"
                                enabled: app.ready
                                    && app.canManageAssets
                                    && backgroundCard
                                        .publicationState
                                        === "published"
                                    && backgroundCard.handle
                                        .length === 64
                                    && app.propDraftReady()
                                onClicked:
                                    app.assignPropAsset(
                                        app.propDraftId.trim(),
                                        backgroundCard.handle,
                                        app.parsedAssetAnchor(
                                            app.propDraftAnchorX),
                                        app.parsedAssetAnchor(
                                            app.propDraftAnchorY),
                                        app.propDraftLayer.trim())
                            }

                            Button {
                                objectName:
                                    "palaceBackgroundAssignLounge-"
                                    + backgroundCard.handle
                                Layout.fillWidth: true
                                Layout.minimumWidth: 96
                                text: "Set Lounge"
                                enabled: app.ready
                                    && app.canManageAssets
                                    && backgroundCard
                                        .publicationState
                                        === "published"
                                    && backgroundCard.handle
                                        .length === 64
                                onClicked:
                                    app.assignRoomBackground(
                                        "lounge",
                                        backgroundCard.handle)
                            }
                        }

                        RowLayout {
                            Layout.fillWidth: true
                            spacing: 8
                            visible: backgroundCard
                                .publicationState
                                === "published"
                                && backgroundCard.handle.length
                                === 64

                            Rectangle {
                                id: propPlacementStage
                                objectName:
                                    "palaceAssetPropPreview-"
                                    + backgroundCard.handle
                                property real sourceWidth:
                                    Math.max(
                                        1,
                                        Number(backgroundCard.asset
                                            .width))
                                property real sourceHeight:
                                    Math.max(
                                        1,
                                        Number(backgroundCard.asset
                                            .height))
                                property real previewScale:
                                    app.propPreviewScale(
                                        sourceWidth,
                                        sourceHeight)
                                property real targetX:
                                    app.propPreviewTargetX(
                                        app.propDraftLayer,
                                        width)
                                property real targetY:
                                    app.propPreviewTargetY(
                                        app.propDraftLayer,
                                        height)
                                Layout.preferredWidth: 152
                                Layout.preferredHeight: 86
                                radius: 5
                                color: "#18110c"
                                border.color:
                                    app.propDraftReady()
                                    ? "#7ecb78" : "#62513b"

                                Rectangle {
                                    width: 34
                                    height: 34
                                    radius: 17
                                    color: "#ffe566"
                                    border.color: "#222222"
                                    border.width: 2
                                    anchors.horizontalCenter:
                                        parent.horizontalCenter
                                    anchors.verticalCenter:
                                        parent.verticalCenter
                                }

                                Rectangle {
                                    width: 26
                                    height: 18
                                    radius: 7
                                    color: "#ffe566"
                                    border.color: "#222222"
                                    border.width: 2
                                    anchors.horizontalCenter:
                                        parent.horizontalCenter
                                    anchors.top:
                                        parent.verticalCenter
                                    anchors.topMargin: 8
                                }

                                Item {
                                    id: propPlacementPreview
                                    width: propPlacementStage
                                        .sourceWidth
                                        * propPlacementStage
                                            .previewScale
                                    height: propPlacementStage
                                        .sourceHeight
                                        * propPlacementStage
                                            .previewScale
                                    x: propPlacementStage.targetX
                                        - app.clampAnchorToAsset(
                                            app.parsedAssetAnchor(
                                                app.propDraftAnchorX),
                                            propPlacementStage
                                                .sourceWidth)
                                          * propPlacementStage
                                                .previewScale
                                    y: propPlacementStage.targetY
                                        - app.clampAnchorToAsset(
                                            app.parsedAssetAnchor(
                                                app.propDraftAnchorY),
                                            propPlacementStage
                                                .sourceHeight)
                                          * propPlacementStage
                                                .previewScale
                                    z: app.propDraftLayer.trim()
                                        === "back" ? 0 : 2

                                    Image {
                                        anchors.fill: parent
                                        source:
                                            "image://basecamp-verified/"
                                            + backgroundCard.handle
                                        fillMode: Image.Stretch
                                        smooth: true
                                        asynchronous: false
                                        opacity: root
                                            .propDraftReady()
                                            ? 0.95 : 0.72
                                    }

                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: "#ffcc44"
                                        border.color: "#3b2c14"
                                        border.width: 1
                                        x: app.clampAnchorToAsset(
                                            app.parsedAssetAnchor(
                                                app.propDraftAnchorX),
                                            propPlacementStage
                                                .sourceWidth)
                                           * propPlacementStage
                                                .previewScale
                                           - width / 2
                                        y: app.clampAnchorToAsset(
                                            app.parsedAssetAnchor(
                                                app.propDraftAnchorY),
                                            propPlacementStage
                                                .sourceHeight)
                                           * propPlacementStage
                                                .previewScale
                                           - height / 2
                                    }

                                    MouseArea {
                                        objectName:
                                            "palaceAssetPropPreviewHit-"
                                            + backgroundCard.handle
                                        anchors.fill: parent
                                        enabled: app.canManageAssets
                                        cursorShape: enabled
                                            ? Qt.CrossCursor
                                            : Qt.ArrowCursor
                                        onClicked:
                                            app.setPropDraftAnchorFromPreview(
                                                mouse.x,
                                                mouse.y,
                                                propPlacementStage
                                                    .previewScale,
                                                propPlacementStage
                                                    .sourceWidth,
                                                propPlacementStage
                                                    .sourceHeight)
                                    }
                                }

                                Rectangle {
                                    width: 14
                                    height: 14
                                    radius: 7
                                    color: "#ffcc44"
                                    border.color: "#3b2c14"
                                    border.width: 1
                                    x: propPlacementStage.targetX
                                        - width / 2
                                    y: propPlacementStage.targetY
                                        - height / 2
                                    z: 3
                                }
                            }

                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 3

                                Text {
                                    Layout.fillWidth: true
                                    text: "Prop preview"
                                    color: "#fff2cf"
                                    font.bold: true
                                    font.pixelSize: 10
                                }

                                Text {
                                    Layout.fillWidth: true
                                    wrapMode: Text.Wrap
                                    text: app.propDraftReady()
                                        ? "Hot spot follows yellow pin."
                                        : "Set prop ID/layer, then click prop to place hot spot."
                                    color: "#c9b78e"
                                    font.pixelSize: 9
                                }

                                Text {
                                    Layout.fillWidth: true
                                    text: "Anchor "
                                        + app.propDraftAnchorX
                                        + ","
                                        + app.propDraftAnchorY
                                        + " · "
                                        + (app.propDraftLayer
                                            .length > 0
                                           ? app.propDraftLayer
                                           : "layer?")
                                    color: "#a7e3a0"
                                    font.pixelSize: 9
                                    elide: Text.ElideRight
                                }
                            }
                        }
                    }

                    Component.onDestruction: {
                        previewCounted =
                            app.noteAuthoringPreviewLost(
                                previewCounted)
                    }
                }
            }
        }
    }

    Text {
        Layout.fillWidth: true
        text: app.canManageAssets
            ? (app.lastActionReceipt.length > 0
               ? app.lastActionReceipt
               : "Draft assignment becomes authority when Palace creation finalizes.")
            : app.assetAuthoringReadOnlyMessage()
        color: app.canManageAssets
            ? (app.lastActionReceipt.indexOf("rejected=") === 0
               ? "#ff9c8f" : "#a7e3a0")
            : "#d8a18f"
        elide: Text.ElideRight
        font.pixelSize: 10
    }
}
        }
