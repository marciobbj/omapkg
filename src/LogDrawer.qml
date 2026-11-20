import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root

    required property string logText
    required property bool busy
    required property string statusMessage
    required property bool lastSucceeded

    signal closeRequested()
    signal clearRequested()
    signal cancelRequested()

    color: theme.darkerBackground
    border.width: 1
    border.color: theme.selection

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // Drawer header bar.
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 36
            color: theme.darkBackground

            RowLayout {
                anchors.fill: parent
                anchors.leftMargin: 12
                anchors.rightMargin: 8
                spacing: 8

                Rectangle {
                    width: 8
                    height: 8
                    radius: 4
                    color: root.busy ? theme.accent : (root.lastSucceeded ? theme.green : theme.red)
                }

                Text {
                    text: root.statusMessage.length > 0 ? root.statusMessage : qsTr("Activity log")
                    color: theme.foreground
                    font.pixelSize: 12
                    font.weight: Font.Medium
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }

                Button {
                    visible: root.busy
                    text: qsTr("Cancel")
                    flat: true
                    implicitHeight: 26
                    Material.foreground: theme.red
                    Material.roundedScale: theme.radius
                    onClicked: root.cancelRequested()
                }

                Button {
                    visible: !root.busy && root.logText.length > 0
                    text: qsTr("Clear")
                    flat: true
                    implicitHeight: 26
                    Material.foreground: theme.darkForeground
                    Material.roundedScale: theme.radius
                    onClicked: root.clearRequested()
                }

                Button {
                    text: "×"
                    flat: true
                    implicitWidth: 28
                    implicitHeight: 26
                    Material.foreground: theme.darkForeground
                    Material.roundedScale: theme.radius
                    onClicked: root.closeRequested()
                }
            }
        }

        // Scrollable monospace command output.
        Flickable {
            id: logFlick
            Layout.fillWidth: true
            Layout.fillHeight: true
            contentWidth: width
            contentHeight: Math.max(height, logArea.implicitHeight + 16)
            clip: true
            boundsBehavior: Flickable.StopAtBounds
            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

            onContentHeightChanged: {
                if (contentHeight > height)
                    contentY = contentHeight - height
            }

            TextEdit {
                id: logArea
                x: 12
                y: 8
                width: logFlick.width - 24
                readOnly: true
                selectByMouse: true
                wrapMode: TextEdit.WrapAnywhere
                text: root.logText.length > 0 ? root.logText : qsTr("No recent package activity.")
                color: root.logText.length > 0 ? theme.lightForeground : theme.darkForeground
                font.family: "monospace"
                font.pixelSize: 12
            }
        }
    }
}
