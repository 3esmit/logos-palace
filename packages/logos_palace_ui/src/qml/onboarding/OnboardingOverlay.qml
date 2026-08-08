import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: overlay

    required property Item app

    objectName: "palaceOnboardingOverlay"
    anchors.fill: parent
    color: "#10141de8"
    z: 100

    onVisibleChanged: {
        if (!visible || !overlay.app.ready)
            return
        if (overlay.app.onboardingMode === "choose")
            return
        if (overlay.app.onboardingPhase === "authoring-rooms")
            onboardingDetails.focusTitle()
        else
            onboardingDetails.focusPassword()
    }

    MouseArea {
        anchors.fill: parent
        onClicked: {
        }
    }

    Rectangle {
        id: onboardingCard
        objectName: "palaceOnboardingCard"
        anchors.centerIn: parent
        width: Math.min(parent.width - 32, 540)
        height: onboardingContent.implicitHeight + 36
        color: "#f5f0e6"
        border.color: "#a88451"
        border.width: 2
        radius: 8
        z: 1

        ColumnLayout {
            id: onboardingContent
            anchors.fill: parent
            anchors.margins: 18
            spacing: 9

            Text {
                Layout.fillWidth: true
                text: "Open a Palace"
                color: "#2b2016"
                font.bold: true
                font.pixelSize: 23
            }

            Text {
                Layout.fillWidth: true
                text: "Configure and start network nodes in Logos Control. Palace uses the running modules but does not manage them."
                color: "#463b2d"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            Text {
                Layout.fillWidth: true
                text: "Create a new Palace or join one with an invitation. Storage and LEZ remain controlled by Logos Control."
                color: "#463b2d"
                font.pixelSize: 13
                wrapMode: Text.Wrap
            }

            PalaceWelcome {
                app: overlay.app
                visible: overlay.app.onboardingMode === "choose"
                onCreateRequested: {
                    overlay.app.onboardingMode = "create"
                    overlay.app.onboardingError = ""
                    overlay.app.resetOnboardingAfterEdit()
                    Qt.callLater(function () {
                        onboardingDetails.focusPassword()
                    })
                }
                onJoinRequested: {
                    overlay.app.onboardingMode = "join"
                    overlay.app.onboardingError = ""
                    overlay.app.resetOnboardingAfterEdit()
                    Qt.callLater(function () {
                        onboardingDetails.focusPassword()
                    })
                }
                onRecoverRequested: {
                    overlay.app.onboardingMode = "recover"
                    overlay.app.onboardingError = ""
                    overlay.app.resetOnboardingAfterEdit()
                    Qt.callLater(function () {
                        onboardingDetails.focusPassword()
                    })
                }
            }

            CreatePalaceFlow { app: overlay.app }
            JoinPalaceFlow { app: overlay.app }
            RecoverPalaceFlow { app: overlay.app }

            OnboardingDetails {
                id: onboardingDetails
                app: overlay.app
            }
        }
    }
}
