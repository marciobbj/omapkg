import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Item {
    id: root

    required property int index
    required property string pkgId
    required property string name
    required property string displayName
    required property string version
    required property string availableVersion
    required property string source
    required property string repository
    required property string description
    required property real sizeBytes
    required property bool hasUpdate
    required property bool isOrphan
    required property bool hasSecurityAlert
    required property string securitySeverity
    required property string iconName

    required property bool selected
    required property bool busy

    signal clicked()
    signal updateRequested()

    height: 68
    width: ListView.view ? ListView.view.width : 400

    function sourceColor(src) {
        if (src === "omarchy") return theme.accent
        if (src === "pacman") return theme.blue
        if (src === "aur") return theme.magenta
        if (src === "flatpak") return theme.cyan
        if (src === "omarchy-plugin") return theme.yellow
        if (src === "mise") return theme.green
        return theme.muted
    }

    function sourceLabel(src) {
        if (src === "omarchy") return qsTr("Omarchy")
        if (src === "pacman") return qsTr("Pacman")
        if (src === "aur") return qsTr("AUR")
        if (src === "flatpak") return qsTr("Flatpak")
        if (src === "omarchy-plugin") return qsTr("Omarchy Plugin")
        if (src === "mise") return qsTr("Mise")
        return src
    }

    Rectangle {
        id: card
        anchors.fill: parent
        anchors.leftMargin: 10
        anchors.rightMargin: 10
        anchors.topMargin: 3
        anchors.bottomMargin: 3
        radius: Math.min(8, theme.radius)
        clip: true
        color: root.selected
               ? theme.selection
               : (hoverArea.containsMouse ? theme.lighterBackground : theme.background)
        border.width: root.selected ? 1 : 0
        border.color: theme.accent

        MouseArea {
            id: hoverArea
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: root.clicked()
        }

        RowLayout {
            anchors.fill: parent
            anchors.leftMargin: 12
            anchors.rightMargin: 12
            spacing: 10

            // App icon with styled source-colored fallback monogram.
            Rectangle {
                Layout.preferredWidth: 36
                Layout.minimumWidth: 36
                Layout.maximumWidth: 36
                Layout.preferredHeight: 36
                Layout.alignment: Qt.AlignVCenter
                radius: Math.min(8, theme.radius)
                color: Qt.rgba(Qt.color(root.sourceColor(root.source)).r,
                               Qt.color(root.sourceColor(root.source)).g,
                               Qt.color(root.sourceColor(root.source)).b, 0.16)

                Image {
                    id: appIcon
                    anchors.centerIn: parent
                    width: 26
                    height: 26
                    sourceSize.width: 32
                    sourceSize.height: 32
                    fillMode: Image.PreserveAspectFit
                    asynchronous: true
                    source: root.iconName.length > 0
                            ? ("image://themeicon/" + encodeURIComponent(root.iconName))
                            : ""
                    visible: status === Image.Ready
                }

                Text {
                    anchors.centerIn: parent
                    visible: appIcon.status !== Image.Ready
                    text: root.displayName.length > 0 ? root.displayName.charAt(0).toUpperCase() : "?"
                    color: root.sourceColor(root.source)
                    font.pixelSize: 15
                    font.bold: true
                }
            }

            // Title, source badge, and one-line description.
            ColumnLayout {
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.alignment: Qt.AlignVCenter
                spacing: 3

                RowLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    spacing: 6

                    Text {
                        text: root.displayName
                        color: theme.brightForeground
                        font.pixelSize: 14
                        font.weight: Font.DemiBold
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 40
                        Layout.maximumWidth: Math.ceil(implicitWidth) + 6
                    }

                    Text {
                        visible: root.displayName.toLowerCase() !== root.name.toLowerCase() && root.width >= 520
                        text: root.name
                        color: theme.darkForeground
                        font.pixelSize: 11
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                        Layout.maximumWidth: Math.min(160, Math.ceil(implicitWidth) + 4)
                    }

                    // Source pill.
                    Rectangle {
                        height: 18
                        Layout.preferredWidth: sourceTag.implicitWidth + 10
                        Layout.minimumWidth: sourceTag.implicitWidth + 10
                        radius: Math.min(4, theme.radius)
                        color: Qt.rgba(Qt.color(root.sourceColor(root.source)).r,
                                       Qt.color(root.sourceColor(root.source)).g,
                                       Qt.color(root.sourceColor(root.source)).b, 0.18)

                        Text {
                            id: sourceTag
                            anchors.centerIn: parent
                            text: root.sourceLabel(root.source)
                            color: root.sourceColor(root.source)
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }

                    // Security advisory pill.
                    Rectangle {
                        visible: root.hasSecurityAlert
                        height: 18
                        Layout.preferredWidth: secTag.implicitWidth + 10
                        Layout.minimumWidth: secTag.implicitWidth + 10
                        radius: Math.min(4, theme.radius)
                        color: Qt.rgba(Qt.color(theme.red).r, Qt.color(theme.red).g,
                                       Qt.color(theme.red).b, 0.22)

                        Text {
                            id: secTag
                            anchors.centerIn: parent
                            text: root.securitySeverity.length > 0
                                  ? qsTr("Security · %1").arg(root.securitySeverity.toUpperCase())
                                  : qsTr("Security")
                            color: theme.red
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }

                    // Orphan pill.
                    Rectangle {
                        visible: root.isOrphan
                        height: 18
                        Layout.preferredWidth: orphanTag.implicitWidth + 10
                        Layout.minimumWidth: orphanTag.implicitWidth + 10
                        radius: Math.min(4, theme.radius)
                        color: Qt.rgba(Qt.color(theme.orange).r, Qt.color(theme.orange).g,
                                       Qt.color(theme.orange).b, 0.2)

                        Text {
                            id: orphanTag
                            anchors.centerIn: parent
                            text: qsTr("Orphan")
                            color: theme.orange
                            font.pixelSize: 10
                            font.bold: true
                        }
                    }

                    Item {
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                }

                Text {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    text: root.hasUpdate
                          ? (root.availableVersion.length > 0
                             ? qsTr("Update: %1 → %2").arg(root.version).arg(root.availableVersion)
                             : qsTr("Update available"))
                          : (root.description.length > 0 ? root.description : root.version)
                    color: root.hasUpdate ? theme.green : theme.darkForeground
                    font.pixelSize: 12
                    elide: Text.ElideRight
                }
            }

            // Disk size readout.
            Text {
                Layout.preferredWidth: 74
                Layout.minimumWidth: 64
                Layout.alignment: Qt.AlignVCenter
                horizontalAlignment: Text.AlignRight
                text: {
                    void translator.language
                    return translator.formatBytes(root.sizeBytes)
                }
                color: theme.lightForeground
                font.pixelSize: 12
                font.weight: Font.Medium
            }

            // Inline quick-update button when an update is available.
            Rectangle {
                visible: root.hasUpdate
                Layout.preferredWidth: updateBtnLabel.implicitWidth + 18
                Layout.minimumWidth: updateBtnLabel.implicitWidth + 18
                Layout.preferredHeight: 28
                Layout.alignment: Qt.AlignVCenter
                radius: Math.min(6, theme.radius)
                color: updateBtnArea.containsMouse && !root.busy ? theme.green : theme.accent
                opacity: root.busy ? 0.45 : 1.0

                Text {
                    id: updateBtnLabel
                    anchors.centerIn: parent
                    text: qsTr("Update")
                    color: theme.accentForeground
                    font.pixelSize: 12
                    font.bold: true
                }

                MouseArea {
                    id: updateBtnArea
                    anchors.fill: parent
                    hoverEnabled: true
                    enabled: !root.busy
                    cursorShape: Qt.PointingHandCursor
                    onClicked: function(mouse) {
                        mouse.accepted = true
                        root.updateRequested()
                    }
                }
            }
        }
    }
}
