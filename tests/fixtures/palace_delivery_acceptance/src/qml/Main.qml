pragma ComponentBehavior: Bound

import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Item {
    id: root
    objectName: "palaceDeliveryAcceptanceRoot"

    readonly property var backend:
        logos.module("palace_delivery_acceptance")
    property bool ready: false
    property string invocationError: ""

    readonly property string acceptanceStatus: backend
        ? backend.status : "state=unavailable;callbacks=0"
    readonly property string acceptanceReceipt:
        invocationError.length > 0
            ? invocationError
            : (backend ? backend.receipt : "")
    readonly property string acceptanceNodeEvidence: backend
        ? backend.nodeEvidence
        : "{\"success\":false,\"reason\":\"node-not-created\"}"

    function rejectedNotReady() {
        invocationError = "rejected=acceptance-ui-not-ready"
        return invocationError
    }

    function watchAcceptance(pendingCall) {
        invocationError = ""
        logos.watch(pendingCall, function () {
        }, function (error) {
            invocationError =
                "rejected=acceptance-ui-remote-call;" + String(error)
        })
        return "pending"
    }

    function acceptanceStart(entryNode, tcpPort) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAcceptance(
            backend.start(String(entryNode), Number(tcpPort)))
    }

    function acceptanceInject(scenario) {
        if (!ready || !backend)
            return rejectedNotReady()
        return watchAcceptance(backend.inject(String(scenario)))
    }

    Connections {
        target: logos

        function onViewModuleReadyChanged(moduleName, isReady) {
            if (moduleName === "palace_delivery_acceptance")
                root.ready = isReady && root.backend !== null
        }
    }

    Component.onCompleted: {
        ready = backend !== null
            && logos.isViewModuleReady("palace_delivery_acceptance")
    }

    Rectangle {
        anchors.fill: parent
        color: "#111827"

        ColumnLayout {
            anchors.centerIn: parent
            spacing: 12

            Label {
                text: "Palace Delivery Acceptance"
                color: "#f9fafb"
                font.pixelSize: 24
                Layout.alignment: Qt.AlignHCenter
            }

            Label {
                text: root.acceptanceStatus
                color: root.acceptanceStatus.indexOf("state=failed") === 0
                    ? "#f87171" : "#9ca3af"
                font.pixelSize: 13
                Layout.alignment: Qt.AlignHCenter
            }
        }
    }
}
