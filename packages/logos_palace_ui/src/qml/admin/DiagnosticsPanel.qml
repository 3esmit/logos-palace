import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import "../components"

ColumnLayout {
    id: diagnostics
    required property Item app
    objectName: "palaceDiagnosticsPanel"

    property string diagnosticSourceCid: ""
    property string diagnosticDerivativeCid: ""
    property string diagnosticByteLength: ""
    property string diagnosticContentSha256: ""
    property string diagnosticWidth: "1600"
    property string diagnosticHeight: "900"
    property bool rawAssetDetailsOpen: false

    spacing: 3

    Text {
        text: "Diagnostics"
        color: "#fff2cf"
        font.bold: true
        font.pixelSize: 13
    }
    Text {
        Layout.fillWidth: true
        text: "LEZ: " + app.lezState
            + "\nStorage: " + app.storageStatus
            + "\nDelivery: " + app.deliverySessionStatus
            + "\nRecovery: " + app.syncHealth
        color: "#c9b78e"
        elide: Text.ElideRight
        wrapMode: Text.Wrap
        font.pixelSize: 9
    }
    Button {
        objectName: "palaceDiagnosticsToggleRawAssets"
        Layout.fillWidth: true
        text: rawAssetDetailsOpen
            ? "Hide raw asset details" : "Show raw asset details"
        onClicked: diagnostics.rawAssetDetailsOpen =
            !diagnostics.rawAssetDetailsOpen
    }
    Text {
        objectName: "palaceDiagnosticsRawAssets"
        Layout.fillWidth: true
        visible: diagnostics.rawAssetDetailsOpen
        text: "Asset records: " + app.assetAuthoringState
        color: "#c9b78e"
        wrapMode: Text.Wrap
        font.pixelSize: 8
    }
    Button {
        objectName: "palaceAdminVerifyStorageRetention"
        Layout.fillWidth: true
        text: "Verify retained content"
        enabled: app.ready
        onClicked: app.storageVerifyRetention()
    }
    Text {
        Layout.fillWidth: true
        text: "Missing-content check"
        color: "#c9b78e"
        font.pixelSize: 10
    }
    TextField {
        objectName: "palaceStorageDiagnosticSourceCid"
        Layout.fillWidth: true
        placeholderText: "source CID"
        text: diagnostics.diagnosticSourceCid
        onTextEdited: diagnostics.diagnosticSourceCid = text
    }
    TextField {
        objectName: "palaceStorageDiagnosticDerivativeCid"
        Layout.fillWidth: true
        placeholderText: "derivative CID"
        text: diagnostics.diagnosticDerivativeCid
        onTextEdited: diagnostics.diagnosticDerivativeCid = text
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            objectName: "palaceStorageDiagnosticByteLength"
            Layout.fillWidth: true
            placeholderText: "bytes"
            text: diagnostics.diagnosticByteLength
            onTextEdited: diagnostics.diagnosticByteLength = text
        }
        TextField {
            objectName: "palaceStorageDiagnosticContentSha256"
            Layout.fillWidth: true
            placeholderText: "content SHA-256"
            text: diagnostics.diagnosticContentSha256
            onTextEdited: diagnostics.diagnosticContentSha256 = text
        }
    }
    RowLayout {
        Layout.fillWidth: true
        TextField {
            objectName: "palaceStorageDiagnosticWidth"
            Layout.fillWidth: true
            placeholderText: "width"
            text: diagnostics.diagnosticWidth
            onTextEdited: diagnostics.diagnosticWidth = text
        }
        TextField {
            objectName: "palaceStorageDiagnosticHeight"
            Layout.fillWidth: true
            placeholderText: "height"
            text: diagnostics.diagnosticHeight
            onTextEdited: diagnostics.diagnosticHeight = text
        }
    }
    RowLayout {
        Layout.fillWidth: true
        Button {
            objectName: "palaceStorageDiagnosticStatus"
            Layout.fillWidth: true
            text: "Check missing object"
            enabled: app.ready && diagnostics.diagnosticDerivativeCid.length > 0
            onClicked: app.storageAssetStatus(
                diagnostics.diagnosticDerivativeCid)
        }
        Button {
            objectName: "palaceStorageDiagnosticFetch"
            Layout.fillWidth: true
            text: "Fetch missing object"
            enabled: app.ready
                && diagnostics.diagnosticSourceCid.length > 0
                && diagnostics.diagnosticDerivativeCid.length > 0
                && diagnostics.diagnosticByteLength.length > 0
                && diagnostics.diagnosticContentSha256.length > 0
            onClicked: app.storageFetchPng(
                diagnostics.diagnosticSourceCid,
                diagnostics.diagnosticDerivativeCid,
                Number(diagnostics.diagnosticByteLength),
                diagnostics.diagnosticContentSha256,
                Number(diagnostics.diagnosticWidth),
                Number(diagnostics.diagnosticHeight))
        }
    }
    AsyncActionState {
        Layout.fillWidth: true
        receipt: app.lastActionReceipt
    }
}
