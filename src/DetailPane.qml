import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

Rectangle {
    id: root

    required property var pkg
    required property var details
    required property bool loadingDetails
    required property bool busy

    signal updateRequested(string pkgId)
    signal removeRequested(string pkgId)
    signal launchRequested(string pkgId)

    color: theme.darkBackground

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
        return src || ""
    }

    function isSafeHttpUrl(url) {
        if (!url) return false
        var s = String(url).trim()
        return s.indexOf("https://") === 0 || s.indexOf("http://") === 0
    }

    // Empty state when nothing is selected.
    ColumnLayout {
        anchors.centerIn: parent
        width: parent.width - 48
        visible: !root.pkg || !root.pkg.pkgId
        spacing: 8

        Text {
            Layout.alignment: Qt.AlignHCenter
            text: qsTr("Select an app or package")
            color: theme.foreground
            font.pixelSize: 15
            font.weight: Font.DemiBold
        }

        Text {
            Layout.alignment: Qt.AlignHCenter
            Layout.fillWidth: true
            horizontalAlignment: Text.AlignHCenter
            wrapMode: Text.WordWrap
            text: qsTr("Inspect disk usage, update status, dependencies, or remove it cleanly.")
            color: theme.darkForeground
            font.pixelSize: 12
        }
    }

    Flickable {
        id: scroller
        anchors.fill: parent
        visible: root.pkg && !!root.pkg.pkgId
        contentWidth: width
        contentHeight: contentCol.implicitHeight + 36
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

        ColumnLayout {
            id: contentCol
            x: 18
            y: 18
            width: scroller.width - 36
            spacing: 16

            // Header with icon, name, package ID, and source badge.
            RowLayout {
                Layout.fillWidth: true
                spacing: 12

                Rectangle {
                    Layout.preferredWidth: 52
                    Layout.preferredHeight: 52
                    radius: Math.min(10, theme.radius)
                    color: Qt.rgba(Qt.color(root.sourceColor(root.pkg.source)).r,
                                   Qt.color(root.sourceColor(root.pkg.source)).g,
                                   Qt.color(root.sourceColor(root.pkg.source)).b, 0.18)

                    Image {
                        id: detailIcon
                        anchors.centerIn: parent
                        width: 36
                        height: 36
                        sourceSize.width: 48
                        sourceSize.height: 48
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                        source: root.pkg.iconName && root.pkg.iconName.length > 0
                                ? ("image://themeicon/" + encodeURIComponent(root.pkg.iconName))
                                : ""
                        visible: status === Image.Ready
                    }

                    Text {
                        anchors.centerIn: parent
                        visible: detailIcon.status !== Image.Ready
                        text: root.pkg.displayName ? root.pkg.displayName.charAt(0).toUpperCase() : "?"
                        color: root.sourceColor(root.pkg.source)
                        font.pixelSize: 22
                        font.bold: true
                    }
                }

                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: 4

                    Text {
                        Layout.fillWidth: true
                        text: root.pkg.displayName || ""
                        color: theme.brightForeground
                        font.pixelSize: 17
                        font.bold: true
                        wrapMode: Text.WordWrap
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.pkg.name || ""
                        color: theme.darkForeground
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Flow {
                        Layout.fillWidth: true
                        spacing: 6

                        Rectangle {
                            height: 20
                            width: srcText.implicitWidth + 12
                            radius: Math.min(4, theme.radius)
                            color: Qt.rgba(Qt.color(root.sourceColor(root.pkg.source)).r,
                                           Qt.color(root.sourceColor(root.pkg.source)).g,
                                           Qt.color(root.sourceColor(root.pkg.source)).b, 0.18)
                            Text {
                                id: srcText
                                anchors.centerIn: parent
                                text: root.sourceLabel(root.pkg.source) + (root.pkg.repository && root.pkg.repository !== root.pkg.source && root.pkg.repository !== "AUR" ? (" · " + root.pkg.repository) : "")
                                color: root.sourceColor(root.pkg.source)
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }

                        Rectangle {
                            visible: !!root.pkg.isOmarchyDefault
                            height: 20
                            width: coreText.implicitWidth + 12
                            radius: Math.min(4, theme.radius)
                            color: Qt.rgba(Qt.color(theme.yellow).r, Qt.color(theme.yellow).g,
                                           Qt.color(theme.yellow).b, 0.18)
                            Text {
                                id: coreText
                                anchors.centerIn: parent
                                text: qsTr("Omarchy Core")
                                color: theme.yellow
                                font.pixelSize: 11
                                font.bold: true
                            }
                        }
                    }
                }
            }

            // Action buttons.
            RowLayout {
                Layout.fillWidth: true
                spacing: 8

                Button {
                    id: detailUpdateBtn
                    visible: !!root.pkg.hasUpdate
                    Layout.fillWidth: true
                    text: qsTr("Update")
                    enabled: !root.busy
                    topInset: 0
                    bottomInset: 0
                    leftPadding: 14
                    rightPadding: 14
                    topPadding: 0
                    bottomPadding: 0
                    implicitHeight: 36
                    contentItem: Text {
                        text: detailUpdateBtn.text
                        color: theme.accentForeground
                        font.pixelSize: 13
                        font.weight: Font.DemiBold
                        horizontalAlignment: Text.AlignHCenter
                        verticalAlignment: Text.AlignVCenter
                    }
                    background: Rectangle {
                        implicitHeight: 36
                        radius: Math.min(6, theme.radius)
                        color: detailUpdateBtn.down ? Qt.darker(theme.accent, 1.15)
                               : (detailUpdateBtn.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent)
                        opacity: detailUpdateBtn.enabled ? 1.0 : 0.45
                    }
                    onClicked: root.updateRequested(root.pkg.pkgId)
                }

                Button {
                    visible: Boolean(root.pkg.desktopId && root.pkg.desktopId.length > 0)
                    Layout.fillWidth: !root.pkg.hasUpdate
                    text: qsTr("Open")
                    flat: true
                    Material.foreground: theme.foreground
                    Material.roundedScale: theme.radius
                    background: Rectangle {
                        radius: Math.min(6, theme.radius)
                        color: parent.hovered ? theme.lighterBackground : theme.background
                        border.width: 1
                        border.color: theme.selection
                    }
                    onClicked: root.launchRequested(root.pkg.pkgId)
                }

                Button {
                    Layout.fillWidth: !root.pkg.hasUpdate && !Boolean(root.pkg.desktopId && root.pkg.desktopId.length > 0)
                    text: qsTr("Remove")
                    enabled: !root.busy
                    flat: true
                    Material.foreground: theme.red
                    Material.roundedScale: theme.radius
                    background: Rectangle {
                        radius: Math.min(6, theme.radius)
                        color: parent.hovered
                               ? Qt.rgba(Qt.color(theme.red).r, Qt.color(theme.red).g, Qt.color(theme.red).b, 0.16)
                               : "transparent"
                        border.width: 1
                        border.color: Qt.rgba(Qt.color(theme.red).r, Qt.color(theme.red).g, Qt.color(theme.red).b, 0.5)
                    }
                    onClicked: root.removeRequested(root.pkg.pkgId)
                }
            }

            // Description card.
            Text {
                visible: Boolean(root.pkg.description && root.pkg.description.length > 0)
                Layout.fillWidth: true
                text: root.pkg.description || ""
                color: theme.foreground
                font.pixelSize: 13
                wrapMode: Text.WordWrap
                lineHeight: 1.35
            }

            // Security advisories banner (CVEs, revoked/retired plugins, OSV alerts).
            Rectangle {
                visible: Boolean(root.pkg.hasSecurityAlert && root.pkg.securityAdvisories && root.pkg.securityAdvisories.length > 0)
                Layout.fillWidth: true
                implicitHeight: secCol.implicitHeight + 20
                radius: Math.min(8, theme.radius)
                color: Qt.rgba(Qt.color(theme.red).r, Qt.color(theme.red).g, Qt.color(theme.red).b, 0.14)
                border.width: 1
                border.color: Qt.rgba(Qt.color(theme.red).r, Qt.color(theme.red).g, Qt.color(theme.red).b, 0.5)

                ColumnLayout {
                    id: secCol
                    anchors.fill: parent
                    anchors.margins: 10
                    spacing: 8

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text: qsTr("Security advisory")
                            color: theme.red
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            height: 18
                            width: sevBadgeText.implicitWidth + 10
                            radius: Math.min(4, theme.radius)
                            color: theme.red

                            Text {
                                id: sevBadgeText
                                anchors.centerIn: parent
                                text: root.pkg.securitySeverity ? root.pkg.securitySeverity.toUpperCase() : "ALERT"
                                color: "white"
                                font.pixelSize: 10
                                font.bold: true
                            }
                        }
                    }

                    Repeater {
                        model: root.pkg.securityAdvisories ? root.pkg.securityAdvisories.slice(0, 6) : []

                        delegate: ColumnLayout {
                            Layout.fillWidth: true
                            spacing: 2

                            RowLayout {
                                Layout.fillWidth: true
                                spacing: 6

                                Text {
                                    text: modelData.cves && modelData.cves.length > 0
                                          ? (modelData.id + " · " + modelData.cves.slice(0, 2).join(", "))
                                          : modelData.id
                                    color: theme.brightForeground
                                    font.pixelSize: 11
                                    font.family: "monospace"
                                    font.bold: true
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true

                                    MouseArea {
                                        anchors.fill: parent
                                        enabled: root.isSafeHttpUrl(modelData.url)
                                        cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                                        onClicked: if (root.isSafeHttpUrl(modelData.url)) Qt.openUrlExternally(modelData.url)
                                    }
                                }
                            }

                            Text {
                                Layout.fillWidth: true
                                text: modelData.summary || ""
                                color: theme.lightForeground
                                font.pixelSize: 11
                                wrapMode: Text.WordWrap
                            }

                            Text {
                                visible: Boolean(modelData.fixedVersion && modelData.fixedVersion.length > 0)
                                Layout.fillWidth: true
                                text: qsTr("Fixed in %1").arg(modelData.fixedVersion || "")
                                color: theme.green
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // Update callout banner.
            Rectangle {
                visible: !!root.pkg.hasUpdate
                Layout.fillWidth: true
                implicitHeight: updCol.implicitHeight + 18
                radius: Math.min(8, theme.radius)
                color: Qt.rgba(Qt.color(theme.green).r, Qt.color(theme.green).g, Qt.color(theme.green).b, 0.14)
                border.width: 1
                border.color: Qt.rgba(Qt.color(theme.green).r, Qt.color(theme.green).g, Qt.color(theme.green).b, 0.4)

                ColumnLayout {
                    id: updCol
                    anchors.fill: parent
                    anchors.margins: 9
                    spacing: 2

                    RowLayout {
                        Layout.fillWidth: true
                        spacing: 6

                        Text {
                            text: qsTr("Update available")
                            color: theme.green
                            font.pixelSize: 12
                            font.bold: true
                        }

                        Item { Layout.fillWidth: true }

                        Rectangle {
                            visible: root.pkg.source === "omarchy" || root.pkg.source === "pacman" || root.pkg.source === "omarchy-plugin"
                            height: 18
                            width: omarchyUpdateBadge.implicitWidth + 10
                            radius: Math.min(4, theme.radius)
                            color: Qt.rgba(Qt.color(theme.green).r, Qt.color(theme.green).g,
                                           Qt.color(theme.green).b, 0.2)

                            Text {
                                id: omarchyUpdateBadge
                                anchors.centerIn: parent
                                text: root.pkg.source === "omarchy-plugin" ? "omarchy-plugin-update" : "omarchy-update"
                                color: theme.green
                                font.pixelSize: 10
                                font.family: "monospace"
                                font.bold: true
                            }
                        }
                    }

                    Text {
                        Layout.fillWidth: true
                        text: root.pkg.availableVersion && root.pkg.availableVersion.length > 0
                              ? "%1 → %2".arg(root.pkg.version).arg(root.pkg.availableVersion)
                              : qsTr("A newer version is ready to install.")
                        color: theme.lightForeground
                        font.pixelSize: 12
                        wrapMode: Text.WrapAnywhere
                    }
                }
            }

            // Key metadata card.
            Rectangle {
                Layout.fillWidth: true
                implicitHeight: metaGrid.implicitHeight + 24
                radius: Math.min(8, theme.radius)
                color: theme.background

                GridLayout {
                    id: metaGrid
                    anchors.fill: parent
                    anchors.margins: 12
                    columns: 2
                    rowSpacing: 8
                    columnSpacing: 12

                    Text { text: qsTr("Disk usage"); color: theme.darkForeground; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: {
                            void translator.language
                            return translator.formatBytes(root.pkg.sizeBytes || 0)
                        }
                        color: theme.brightForeground
                        font.pixelSize: 12
                        font.bold: true
                    }

                    Text { text: qsTr("Version"); color: theme.darkForeground; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.pkg.version || "—"
                        color: theme.foreground
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Text { text: qsTr("Installed"); color: theme.darkForeground; font.pixelSize: 12 }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: {
                            void translator.language
                            return translator.formatDate(root.pkg.installDate || 0)
                        }
                        color: theme.foreground
                        font.pixelSize: 12
                    }

                    Text {
                        text: root.pkg.source === "omarchy-plugin" ? qsTr("Status") : qsTr("Reason")
                        color: theme.darkForeground
                        font.pixelSize: 12
                    }
                    Text {
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.pkg.source === "omarchy-plugin"
                              ? (root.details && root.details.pluginEnabled ? qsTr("Enabled") : qsTr("Disabled"))
                              : (root.pkg.isOrphan
                                 ? qsTr("Unused orphan")
                                 : (root.pkg.explicitInstall ? qsTr("Explicitly installed") : qsTr("Dependency")))
                        color: root.pkg.source === "omarchy-plugin"
                               ? (root.details && root.details.pluginEnabled ? theme.green : theme.darkForeground)
                               : (root.pkg.isOrphan ? theme.orange : theme.foreground)
                        font.pixelSize: 12
                    }

                    Text {
                        visible: Boolean(root.pkg.source === "omarchy-plugin" && root.details && root.details.packager && root.details.packager.length > 0)
                        text: qsTr("Author")
                        color: theme.darkForeground
                        font.pixelSize: 12
                    }
                    Text {
                        visible: Boolean(root.pkg.source === "omarchy-plugin" && root.details && root.details.packager && root.details.packager.length > 0)
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.details && root.details.packager ? root.details.packager : ""
                        color: theme.foreground
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: Boolean(root.pkg.source === "mise" && root.details && root.details.configPath && root.details.configPath.length > 0)
                        text: qsTr("Config")
                        color: theme.darkForeground
                        font.pixelSize: 12
                    }
                    Text {
                        visible: Boolean(root.pkg.source === "mise" && root.details && root.details.configPath && root.details.configPath.length > 0)
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.details && root.details.configPath ? root.details.configPath : ""
                        color: theme.foreground
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: Boolean(root.details && root.details.licenses && root.details.licenses.length > 0)
                        text: qsTr("License")
                        color: theme.darkForeground
                        font.pixelSize: 12
                    }
                    Text {
                        visible: Boolean(root.details && root.details.licenses && root.details.licenses.length > 0)
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.details && root.details.licenses ? root.details.licenses : ""
                        color: theme.foreground
                        font.pixelSize: 12
                        elide: Text.ElideRight
                    }

                    Text {
                        visible: Boolean(root.pkg.url && root.pkg.url.length > 0)
                        text: qsTr("Homepage")
                        color: theme.darkForeground
                        font.pixelSize: 12
                    }
                    Text {
                        visible: Boolean(root.pkg.url && root.pkg.url.length > 0)
                        Layout.fillWidth: true
                        horizontalAlignment: Text.AlignRight
                        text: root.pkg.url || ""
                        color: theme.accent
                        font.pixelSize: 12
                        font.underline: urlMouse.containsMouse
                        elide: Text.ElideRight

                        MouseArea {
                            id: urlMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            enabled: root.isSafeHttpUrl(root.pkg.url)
                            cursorShape: enabled ? Qt.PointingHandCursor : Qt.ArrowCursor
                            onClicked: if (root.isSafeHttpUrl(root.pkg.url)) Qt.openUrlExternally(root.pkg.url)
                        }
                    }
                }
            }

            // Installed versions section (for Mise packages).
            ColumnLayout {
                visible: Boolean(root.details && root.details.installedVersions && root.details.installedVersions.length > 0)
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Installed versions (%1)").arg(root.details.installedVersions ? root.details.installedVersions.length : 0)
                    color: theme.darkForeground
                    font.pixelSize: 12
                    font.bold: true
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 5

                    Repeater {
                        model: root.details.installedVersions ? root.details.installedVersions.slice(0, 18) : []
                        delegate: Rectangle {
                            height: 22
                            width: verText.implicitWidth + 12
                            radius: Math.min(4, theme.radius)
                            color: modelData === root.pkg.version
                                   ? Qt.rgba(Qt.color(theme.green).r, Qt.color(theme.green).g, Qt.color(theme.green).b, 0.18)
                                   : theme.background
                            border.width: 1
                            border.color: modelData === root.pkg.version ? theme.green : theme.selection

                            Text {
                                id: verText
                                anchors.centerIn: parent
                                text: modelData
                                color: modelData === root.pkg.version ? theme.green : theme.foreground
                                font.pixelSize: 11
                                font.bold: modelData === root.pkg.version
                            }
                        }
                    }
                }
            }

            // Required By section (important before uninstalling).
            ColumnLayout {
                visible: Boolean(root.details && root.details.requiredBy && root.details.requiredBy.length > 0)
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Required by (%1)").arg(root.details.requiredBy ? root.details.requiredBy.length : 0)
                    color: theme.darkForeground
                    font.pixelSize: 12
                    font.bold: true
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 5

                    Repeater {
                        model: root.details.requiredBy ? root.details.requiredBy.slice(0, 18) : []
                        delegate: Rectangle {
                            height: 22
                            width: reqText.implicitWidth + 12
                            radius: Math.min(4, theme.radius)
                            color: theme.background
                            border.width: 1
                            border.color: theme.selection

                            Text {
                                id: reqText
                                anchors.centerIn: parent
                                text: modelData
                                color: theme.foreground
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            // Dependencies section.
            ColumnLayout {
                visible: Boolean(root.details && root.details.dependsOn && root.details.dependsOn.length > 0)
                Layout.fillWidth: true
                spacing: 6

                Text {
                    text: qsTr("Dependencies (%1)").arg(root.details.dependsOn ? root.details.dependsOn.length : 0)
                    color: theme.darkForeground
                    font.pixelSize: 12
                    font.bold: true
                }

                Flow {
                    Layout.fillWidth: true
                    spacing: 5

                    Repeater {
                        model: root.details.dependsOn ? root.details.dependsOn.slice(0, 18) : []
                        delegate: Rectangle {
                            height: 22
                            width: depText.implicitWidth + 12
                            radius: Math.min(4, theme.radius)
                            color: theme.background

                            Text {
                                id: depText
                                anchors.centerIn: parent
                                text: modelData
                                color: theme.darkForeground
                                font.pixelSize: 11
                            }
                        }
                    }
                }
            }

            Text {
                visible: root.loadingDetails
                text: qsTr("Loading package details…")
                color: theme.darkForeground
                font.pixelSize: 11
            }
        }
    }
}
