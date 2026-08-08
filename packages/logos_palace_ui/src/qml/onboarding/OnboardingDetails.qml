import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

ColumnLayout {
    id: details

    required property Item app

    spacing: 9

    function focusPassword() {
        onboardingPasswordInput.forceActiveFocus()
    }

    function focusTitle() {
        onboardingPalaceTitleInput.forceActiveFocus()
    }

    Text {
        Layout.fillWidth: true
        visible: details.app.onboardingMode !== "choose"
        text: "LEZ password"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 12
    }

    TextField {
        id: onboardingPasswordInput
        objectName: "palaceOnboardingLezPassword"
        Layout.fillWidth: true
        placeholderText: "Password for LEZ on this device"
        echoMode: TextInput.Password
        inputMethodHints: Qt.ImhNoPredictiveText
            | Qt.ImhSensitiveData
        maximumLength: 1024
        text: details.app.onboardingPassword
        visible: details.app.onboardingMode !== "choose"
        enabled: details.app.ready && !details.app.onboardingWorking
            && details.app.onboardingPhase !== "waiting-palace"
            && details.app.onboardingPhase !== "waiting-created-palace"
        Accessible.name: "LEZ password"
        onTextEdited: {
            details.app.onboardingPassword = text
            details.app.resetOnboardingAfterEdit()
        }
    }

    Text {
        Layout.fillWidth: true
        visible: details.app.onboardingMode !== "choose"
        text: "Display name"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 12
    }

    TextField {
        id: onboardingDisplayNameInput
        objectName: "palaceOnboardingDisplayName"
        Layout.fillWidth: true
        placeholderText: "Name shown in the Palace"
        maximumLength: 48
        text: details.app.onboardingDisplayName
        visible: details.app.onboardingMode !== "choose"
        enabled: details.app.ready && !details.app.onboardingWorking
            && details.app.onboardingPhase !== "waiting-palace"
            && details.app.onboardingPhase !== "waiting-created-palace"
        Accessible.name: "Display name"
        onTextEdited: {
            details.app.onboardingDisplayName = text
            details.app.resetOnboardingAfterEdit()
        }
    }

    Text {
        visible: details.app.onboardingMode === "recover"
            || (details.app.onboardingMode === "join"
                && details.app.onboardingInvitation.trim().length === 0)
        text: "Palace address"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 12
    }

    TextField {
        id: onboardingPalaceAddressInput
        objectName: "palaceOnboardingPalaceAddress"
        Layout.fillWidth: true
        placeholderText: "palace://existing-palace-id"
        maximumLength: 80
        text: details.app.onboardingPalaceAddress
        visible: details.app.onboardingMode === "recover"
            || (details.app.onboardingMode === "join"
                && details.app.onboardingInvitation.trim().length === 0)
        enabled: details.app.ready && !details.app.onboardingWorking
            && details.app.onboardingPhase !== "waiting-palace"
            && details.app.onboardingPhase !== "waiting-created-palace"
        Accessible.name: "Palace address"
        onTextEdited: {
            details.app.onboardingPalaceAddress = text
            details.app.resetOnboardingAfterEdit()
        }
        onAccepted: details.app.activateOnboarding()
    }

    Text {
        text: "Shared room catalog (required for an existing Palace)"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 12
        visible: details.app.onboardingMode === "join"
            && details.app.onboardingInvitation.trim().length === 0
            || details.app.onboardingMode === "recover"
    }

    TextField {
        id: onboardingStorageCatalogInput
        objectName: "palaceOnboardingStorageCatalog"
        Layout.fillWidth: true
        placeholderText: "Paste the catalog value shared by the Palace creator"
        maximumLength: 16384
        text: details.app.onboardingStorageCatalog
        visible: details.app.onboardingMode === "recover"
            || (details.app.onboardingMode === "join"
                && details.app.onboardingInvitation.trim().length === 0)
        enabled: details.app.ready && !details.app.onboardingWorking
            && details.app.onboardingPhase !== "waiting-palace"
            && details.app.onboardingPhase !== "waiting-created-palace"
        Accessible.name: "Shared room catalog"
        onTextEdited: {
            details.app.onboardingStorageCatalog = text
            details.app.resetOnboardingAfterEdit()
        }
    }

    Text {
        text: "Storage peer endpoint (required for an existing Palace)"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 12
        visible: details.app.onboardingMode === "join"
            && details.app.onboardingInvitation.trim().length === 0
    }

    TextField {
        id: onboardingStoragePeerEndpointInput
        objectName: "palaceOnboardingStoragePeerEndpoint"
        Layout.fillWidth: true
        placeholderText: "Paste the endpoint shared by the Palace creator"
        maximumLength: 16384
        text: details.app.onboardingStoragePeerEndpoint
        visible: details.app.onboardingMode === "join"
            && details.app.onboardingInvitation.trim().length === 0
        enabled: details.app.ready && !details.app.onboardingWorking
            && details.app.onboardingPhase !== "waiting-palace"
            && details.app.onboardingPhase !== "waiting-created-palace"
        Accessible.name: "Storage peer endpoint"
        onTextEdited: {
            details.app.onboardingStoragePeerEndpoint = text
            details.app.resetOnboardingAfterEdit()
        }
    }

    Text {
        visible: details.app.onboardingMode === "create"
        text: "New Palace title"
        color: "#2b2016"
        font.bold: true
        font.pixelSize: 12
    }

    TextField {
        id: onboardingPalaceTitleInput
        objectName: "palaceOnboardingPalaceTitle"
        Layout.fillWidth: true
        placeholderText: "Name for a new Palace"
        text: details.app.onboardingPalaceTitle
        visible: details.app.onboardingMode === "create"
        enabled: details.app.ready && !details.app.onboardingWorking
            && details.app.onboardingPhase !== "waiting-palace"
            && details.app.onboardingPhase !== "waiting-created-palace"
        Accessible.name: "New Palace title"
        onTextEdited: {
            details.app.onboardingPalaceTitle = text
            details.app.resetOnboardingAfterEdit()
        }
        onAccepted: details.app.activateOnboarding()
    }

    Rectangle {
        Layout.fillWidth: true
        Layout.preferredHeight: 42
        color: details.app.onboardingPhase === "error"
            ? "#f9dedc" : "#e7edf3"
        border.color: details.app.onboardingPhase === "error"
            ? "#b54a43" : "#8197ad"
        radius: 4

        RowLayout {
            anchors.fill: parent
            anchors.margins: 7
            spacing: 7

            BusyIndicator {
                Layout.preferredWidth: 22
                Layout.preferredHeight: 22
                running: details.app.onboardingWorking
                    || details.app.onboardingPhase === "waiting-palace"
                    || details.app.onboardingPhase
                        === "waiting-created-palace"
                visible: running
            }

            Text {
                Layout.fillWidth: true
                text: details.app.onboardingProgressText()
                color: details.app.onboardingPhase === "error"
                    ? "#741c18" : "#24394d"
                font.pixelSize: 12
                wrapMode: Text.Wrap
                verticalAlignment: Text.AlignVCenter
            }
        }
    }

    Button {
        objectName: "palaceOnboardingOpenButton"
        Layout.alignment: Qt.AlignRight
        text: details.app.onboardingButtonText()
        enabled: details.app.ready
            && details.app.onboardingMode !== "choose"
            && !details.app.onboardingWorking
            && details.app.onboardingPhase !== "waiting-palace"
            && details.app.onboardingPhase !== "waiting-created-palace"
            && (details.app.onboardingPhase === "authoring-rooms"
                || details.app.onboardingPhase === "error"
                || details.app.onboardingInputReady())
        Accessible.name: text
        onClicked: details.app.activateOnboarding()
    }
}
