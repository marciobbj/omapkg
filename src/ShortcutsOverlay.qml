import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root

    property bool open: false
    signal closeRequested()

    visible: open
    anchors.fill: parent
    color: Qt.rgba(0, 0, 0, 0.62)
    z: 95

    MouseArea {
        anchors.fill: parent
        onClicked: root.closeRequested()
    }

    Rectangle {
        anchors.centerIn: parent
        width: Math.min(480, parent.width - 48)
        implicitHeight: overlayCol.implicitHeight + 36
        radius: Math.min(12, theme.radius)
        color: theme.background
        border.width: 1
        border.color: theme.selection

        MouseArea { anchors.fill: parent }

        ColumnLayout {
            id: overlayCol
            anchors.fill: parent
            anchors.margins: 20
            spacing: 14

            RowLayout {
                Layout.fillWidth: true

                Text {
                    text: qsTr("Keyboard shortcuts")
                    color: theme.brightForeground
                    font.pixelSize: 16
                    font.bold: true
                    Layout.fillWidth: true
                }

                Text {
                    text: "?"
                    color: theme.accent
                    font.pixelSize: 14
                    font.bold: true
                }
            }

            GridLayout {
                Layout.fillWidth: true
                columns: 2
                rowSpacing: 8
                columnSpacing: 16

                Repeater {
                    model: [
                        { key: "/  ·  Ctrl+F", desc: qsTr("Search packages") },
                        { key: "J / K  ·  ↑ / ↓", desc: qsTr("Move selection") },
                        { key: "Space", desc: qsTr("Update selected package or check updates") },
                        { key: "U", desc: qsTr("Update all outdated packages") },
                        { key: "R", desc: qsTr("Refresh list and check updates") },
                        { key: "Delete", desc: qsTr("Remove selected package") },
                        { key: "1 – 9", desc: qsTr("Switch package source filter") },
                        { key: "L", desc: qsTr("Toggle activity log") },
                        { key: "?", desc: qsTr("Show or hide shortcuts") },
                        { key: "Esc", desc: qsTr("Clear search or close dialog") },
                        { key: "Q", desc: qsTr("Quit") }
                    ]

                    delegate: RowLayout {
                        Layout.columnSpan: 2
                        Layout.fillWidth: true
                        spacing: 12

                        Rectangle {
                            Layout.preferredWidth: 118
                            height: 24
                            radius: Math.min(4, theme.radius)
                            color: theme.darkBackground
                            border.width: 1
                            border.color: theme.selection

                            Text {
                                anchors.centerIn: parent
                                text: modelData.key
                                color: theme.accent
                                font.family: "monospace"
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        Text {
                            Layout.fillWidth: true
                            text: modelData.desc
                            color: theme.foreground
                            font.pixelSize: 13
                            elide: Text.ElideRight
                        }
                    }
                }
            }

            RowLayout {
                Layout.fillWidth: true
                Item { Layout.fillWidth: true }
                Button {
                    text: qsTr("Close")
                    flat: true
                    Material.foreground: theme.accent
                    Material.roundedScale: theme.radius
                    onClicked: root.closeRequested()
                }
            }
        }
    }
}
