import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root

    property bool open: false
    property string titleText: ""
    property string bodyText: ""
    property string warningText: ""
    property string confirmLabel: qsTr("Confirm")
    property bool destructive: false

    signal confirmed()
    signal cancelled()

    visible: open
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.58)
    z: 90

    MouseArea {
        anchors.fill: parent
        onClicked: root.cancelled()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(460, parent.width - 48)
        implicitHeight: dialogCol.implicitHeight + 36
        radius: Math.min(12, theme.radius)
        color: theme.background
        border.width: 1
        border.color: root.destructive ? theme.red : theme.selection

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: dialogCol
            anchors.fill: parent
            anchors.margins: 20
            spacing: 12

            Text {
                Layout.fillWidth: true
                text: root.titleText
                color: theme.brightForeground
                font.pixelSize: 16
                font.bold: true
                wrapMode: Text.WordWrap
            }

            Text {
                Layout.fillWidth: true
                text: root.bodyText
                color: theme.foreground
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                lineHeight: 1.35
            }

            Rectangle {
                visible: root.warningText.length > 0
                Layout.fillWidth: true
                implicitHeight: warnLabel.implicitHeight + 16
                radius: Math.min(6, theme.radius)
                color: Qt.rgba(Qt.color(theme.orange).r, Qt.color(theme.orange).g, Qt.color(theme.orange).b, 0.16)
                border.width: 1
                border.color: Qt.rgba(Qt.color(theme.orange).r, Qt.color(theme.orange).g, Qt.color(theme.orange).b, 0.45)

                Text {
                    id: warnLabel
                    anchors.fill: parent
                    anchors.margins: 8
                    text: root.warningText
                    color: theme.orange
                    font.pixelSize: 12
                    wrapMode: Text.WordWrap
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Layout.topMargin: 6
                spacing: 8

                Item { Layout.fillWidth: true }

                Button {
                    text: qsTr("Cancel")
                    flat: true
                    Material.foreground: theme.foreground
                    Material.roundedScale: theme.radius
                    onClicked: root.cancelled()
                }

                Button {
                    id: confirmBtn
                    text: root.confirmLabel
                    topInset: 0
                    bottomInset: 0
                    leftPadding: 14
                    rightPadding: 14
                    topPadding: 0
                    bottomPadding: 0
                    implicitHeight: 36
                    contentItem: Text {
                        text: confirmBtn.text
                        color: root.destructive ? "white" : theme.accentForeground
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitHeight: 36
                        readonly property color baseColor: root.destructive ? theme.red : theme.accent
                        radius: Math.min(6, theme.radius)
                        color: confirmBtn.down ? Qt.darker(baseColor, 1.15)
                               : (confirmBtn.hovered ? Qt.lighter(baseColor, 1.08) : baseColor)
                    }
                    onClicked: root.confirmed()
                }
            }
        }
    }
}
