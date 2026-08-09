import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Rectangle {
    id: drawer

    required property Item app
    signal closed()

    objectName: "palaceAdminDrawer"
    anchors.top: parent.top
    anchors.right: parent.right
    anchors.bottom: parent.bottom
    width: Math.min(parent.width - 24, 340)
    z: 80
    color: "#2a2119ee"
    border.color: "#a88451"

    Flickable {
        objectName: "palaceAdminContent"
        anchors.fill: parent
        anchors.margins: 12
        contentWidth: width
        contentHeight: drawerContent.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds

        ColumnLayout {
            id: drawerContent
            width: parent.width
            spacing: 12

            RowLayout {
                Layout.fillWidth: true
                Text {
                    Layout.fillWidth: true
                    text: "Admin"
                    color: "#fff2cf"
                    font.bold: true
                    font.pixelSize: 20
                }
                Button {
                    objectName: "palaceAdminClose"
                    text: "Close"
                    onClicked: drawer.closed()
                }
            }

            RoomSettings {
                Layout.fillWidth: true
                app: drawer.app
                onOpenAssets: {
                    drawer.app.adminDrawerOpen = false
                    drawer.app.backgroundModerationOpen = true
                }
            }
            AssetLibrary {
                Layout.fillWidth: true
                app: drawer.app
                onOpenAuthoring: {
                    drawer.app.adminDrawerOpen = false
                    drawer.app.backgroundModerationOpen = true
                }
            }
            PeopleModeration {
                Layout.fillWidth: true
                app: drawer.app
            }
            PropSettings {
                Layout.fillWidth: true
                app: drawer.app
            }
            DiagnosticsPanel {
                Layout.fillWidth: true
                app: drawer.app
            }
        }
    }
}
