import QtQuick

Rectangle {
    id: statusBar

    property bool ready: false
    property int peerCount: 0
    property string deliveryState: "offline"
    property string syncState: "recovering"
    property string storageState: "offline"
    property bool lezReady: false
    property string palaceState: "closed"
    property bool localDevelopment: false
    property string doorPhase: "idle"

    objectName: "palaceConnectionStatus"
    color: "#d4d0c8"
    border.color: "#808080"
    height: 16

    Text {
        anchors.fill: parent
        anchors.leftMargin: 4
        anchors.rightMargin: 4
        verticalAlignment: Text.AlignVCenter
        elide: Text.ElideRight
        font.pixelSize: 9
        color: "#222222"
        text: (statusBar.ready ? "Delivery " + statusBar.deliveryState
                               : "Connecting")
            + " · peers " + statusBar.peerCount
            + " · sync " + statusBar.syncState
            + " · Storage " + statusBar.storageState
            + " · LEZ " + (statusBar.lezReady ? "ok" : "off")
            + " · Palace " + statusBar.palaceState
            + (statusBar.localDevelopment ? " · local development" : "")
            + " · door " + statusBar.doorPhase
    }
}
