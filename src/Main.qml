import QtQuick
import QtQuick.Controls
import QtQuick.Controls.Material
import QtQuick.Layouts

ApplicationWindow {
    id: win
    width: 1160
    height: 740
    minimumWidth: 820
    minimumHeight: 540
    visible: true
    title: "Omapkg"
    color: theme.darkerBackground

    Material.theme: theme.isLight ? Material.Light : Material.Dark
    Material.accent: theme.accent
    Material.roundedScale: theme.radius

    property string selectedId: ""
    property var selectedPkg: ({})
    property bool helpVisible: false
    property bool logVisible: false
    property string pendingAction: ""
    property string pendingPkgId: ""

    readonly property bool modalOpen: helpVisible || confirmDialog.open
    readonly property bool typingInSearch: searchInput.activeFocus

    function selectByIndex(row) {
        if (row < 0 || row >= backend.packages.visibleCount) {
            selectedId = ""
            selectedPkg = ({})
            return
        }
        var item = backend.packages.get(row)
        selectedId = item.pkgId || ""
        selectedPkg = item
        packageList.currentIndex = row
        packageList.positionViewAtIndex(row, ListView.Contain)
        backend.loadPackageDetails(selectedId)
    }

    function syncSelection() {
        if (backend.packages.visibleCount === 0) {
            selectedId = ""
            selectedPkg = ({})
            return
        }
        var idx = selectedId.length > 0 ? backend.packages.indexOfId(selectedId) : -1
        if (idx >= 0) {
            selectedPkg = backend.packages.get(idx)
            packageList.currentIndex = idx
        } else {
            selectByIndex(0)
        }
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

    function promptRemove(pkgId) {
        var pkg = backend.packages.getById(pkgId)
        if (!pkg || !pkg.pkgId)
            return
        pendingAction = "remove"
        pendingPkgId = pkgId
        confirmDialog.titleText = qsTr("Remove %1?").arg(pkg.displayName)
        confirmDialog.bodyText = qsTr("This will uninstall %1 (%2) and free %3 of disk space.")
            .arg(pkg.name)
            .arg(sourceLabel(pkg.source))
            .arg(translator.formatBytes(pkg.sizeBytes || 0))

        var warnings = []
        if (pkg.isOmarchyDefault)
            warnings.push(qsTr("This package is part of Omarchy's default system set."))
        if (selectedId === pkgId && backend.selectedDetails.requiredBy && backend.selectedDetails.requiredBy.length > 0) {
            warnings.push(qsTr("Required by %1 other installed packages: %2")
                .arg(backend.selectedDetails.requiredBy.length)
                .arg(backend.selectedDetails.requiredBy.slice(0, 6).join(", ")))
        }
        confirmDialog.warningText = warnings.join(" ")
        confirmDialog.confirmLabel = qsTr("Remove")
        confirmDialog.destructive = true
        confirmDialog.open = true
    }

    function promptCleanOrphans() {
        if (backend.packages.orphanCount <= 0)
            return
        pendingAction = "orphans"
        pendingPkgId = ""
        confirmDialog.titleText = qsTr("Remove unused orphan packages?")
        confirmDialog.bodyText = qsTr("Remove %1 unused dependencies and reclaim %2 of disk space.")
            .arg(translator.formatNumber(backend.packages.orphanCount))
            .arg(translator.formatBytes(backend.packages.orphanBytes))
        confirmDialog.warningText = ""
        confirmDialog.confirmLabel = qsTr("Clean orphans")
        confirmDialog.destructive = true
        confirmDialog.open = true
    }

    function requestQuit() {
        if (backend.busy) {
            pendingAction = "quit"
            pendingPkgId = ""
            confirmDialog.titleText = qsTr("An operation is still running")
            confirmDialog.bodyText = qsTr("Closing now will stop the active package operation.")
            confirmDialog.warningText = qsTr("Interrupting pacman mid-transaction may leave the package database locked.")
            confirmDialog.confirmLabel = qsTr("Quit anyway")
            confirmDialog.destructive = true
            confirmDialog.open = true
            return
        }
        Qt.quit()
    }

    onClosing: function(close) {
        if (backend.busy && !confirmDialog.open) {
            close.accepted = false
            requestQuit()
        }
    }

    Component.onCompleted: syncSelection()

    Connections {
        target: backend.packages
        function onStatsChanged() { win.syncSelection() }
        function onModelReset() { win.syncSelection() }
    }

    Connections {
        target: backend
        function onOperationChanged() {
            if (backend.busy)
                win.logVisible = true
        }
    }

    // Keyboard shortcuts.
    Shortcut {
        sequences: [StandardKey.Find]
        context: Qt.ApplicationShortcut
        onActivated: {
            if (!win.modalOpen) {
                searchInput.forceActiveFocus()
                searchInput.selectAll()
            }
        }
    }
    Shortcut {
        sequence: "/"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen
        onActivated: {
            searchInput.forceActiveFocus()
            searchInput.selectAll()
        }
    }
    Shortcut {
        sequence: "?"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !confirmDialog.open
        onActivated: win.helpVisible = !win.helpVisible
    }
    Shortcut {
        sequence: "Escape"
        context: Qt.ApplicationShortcut
        onActivated: {
            if (confirmDialog.open) {
                confirmDialog.open = false
            } else if (win.helpVisible) {
                win.helpVisible = false
            } else if (searchInput.activeFocus || searchInput.text.length > 0) {
                searchInput.text = ""
                searchInput.focus = false
            } else if (win.logVisible && !backend.busy) {
                win.logVisible = false
            }
        }
    }
    Shortcut {
        sequence: "Q"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen
        onActivated: win.requestQuit()
    }
    Shortcut {
        sequence: "R"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen
        onActivated: backend.reloadPackages(true)
    }
    Shortcut {
        sequence: "U"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen && !backend.busy && backend.packages.updatesCount > 0
        onActivated: backend.updateAll()
    }
    Shortcut {
        sequence: "L"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen
        onActivated: win.logVisible = !win.logVisible
    }
    Shortcut {
        sequence: "Space"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen && !backend.busy
        onActivated: {
            if (win.selectedPkg && win.selectedPkg.hasUpdate)
                backend.updatePackage(win.selectedPkg.pkgId)
            else
                backend.checkUpdates()
        }
    }
    Shortcut {
        sequence: "Delete"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen && !backend.busy && win.selectedId.length > 0
        onActivated: win.promptRemove(win.selectedId)
    }
    Shortcut {
        sequence: "J"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen
        onActivated: win.selectByIndex(Math.min(backend.packages.visibleCount - 1, packageList.currentIndex + 1))
    }
    Shortcut {
        sequence: "Down"
        context: Qt.ApplicationShortcut
        enabled: !win.modalOpen
        onActivated: win.selectByIndex(Math.min(backend.packages.visibleCount - 1, packageList.currentIndex + 1))
    }
    Shortcut {
        sequence: "K"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen
        onActivated: win.selectByIndex(Math.max(0, packageList.currentIndex - 1))
    }
    Shortcut {
        sequence: "Up"
        context: Qt.ApplicationShortcut
        enabled: !win.modalOpen
        onActivated: win.selectByIndex(Math.max(0, packageList.currentIndex - 1))
    }
    Shortcut { sequence: "1"; context: Qt.ApplicationShortcut; enabled: !win.typingInSearch && !win.modalOpen; onActivated: backend.packages.filterSource = "all" }
    Shortcut { sequence: "2"; context: Qt.ApplicationShortcut; enabled: !win.typingInSearch && !win.modalOpen; onActivated: backend.packages.filterSource = "updates" }
    Shortcut { sequence: "3"; context: Qt.ApplicationShortcut; enabled: !win.typingInSearch && !win.modalOpen; onActivated: backend.packages.filterSource = "omarchy" }
    Shortcut { sequence: "4"; context: Qt.ApplicationShortcut; enabled: !win.typingInSearch && !win.modalOpen; onActivated: backend.packages.filterSource = "pacman" }
    Shortcut { sequence: "5"; context: Qt.ApplicationShortcut; enabled: !win.typingInSearch && !win.modalOpen; onActivated: backend.packages.filterSource = "aur" }
    Shortcut {
        sequence: "6"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen && (backend.hasFlatpak || backend.packages.flatpakCount > 0)
        onActivated: backend.packages.filterSource = "flatpak"
    }
    Shortcut {
        sequence: "7"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen && (backend.hasPlugins || backend.packages.pluginCount > 0)
        onActivated: backend.packages.filterSource = "omarchy-plugin"
    }
    Shortcut {
        sequence: "8"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen && (backend.hasMise || backend.packages.miseCount > 0)
        onActivated: backend.packages.filterSource = "mise"
    }
    Shortcut {
        sequence: "9"
        context: Qt.ApplicationShortcut
        enabled: !win.typingInSearch && !win.modalOpen && backend.packages.orphanCount > 0
        onActivated: backend.packages.filterSource = "orphans"
    }

    // Right-to-left languages mirror the window; Translator sets the layout direction.
    Item {
        anchors.fill: parent
        LayoutMirroring.enabled: Qt.application.layoutDirection === Qt.RightToLeft
        LayoutMirroring.childrenInherit: true

        ColumnLayout {
            anchors.fill: parent
            spacing: 0

            // Top Header Bar.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 62
                color: theme.darkBackground

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 16
                    spacing: 12

                    // App title and live disk/count summary.
                    ColumnLayout {
                        spacing: 2

                        Text {
                            text: "Omapkg"
                            color: theme.brightForeground
                            font.pixelSize: 18
                            font.bold: true
                        }

                        Text {
                            text: {
                                void translator.language
                                return qsTr("%1 used · %2")
                                    .arg(translator.formatBytes(backend.packages.totalBytes))
                                    .arg(qsTr("%n packages", "", backend.packages.scopeCount))
                            }
                            color: theme.darkForeground
                            font.pixelSize: 12
                        }
                    }

                    Item { Layout.fillWidth: true }

                    // Check updates / refresh button.
                    Button {
                        text: backend.checkingUpdates ? qsTr("Checking…") : qsTr("Check updates")
                        enabled: !backend.loading && !backend.checkingUpdates && !backend.busy
                        flat: true
                        Material.foreground: theme.foreground
                        Material.roundedScale: theme.radius
                        background: Rectangle {
                            radius: Math.min(6, theme.radius)
                            color: parent.hovered ? theme.lighterBackground : theme.background
                            border.width: 1
                            border.color: theme.selection
                        }
                        onClicked: backend.reloadPackages(true)
                    }

                    // Clean orphans button when orphans exist.
                    Button {
                        visible: backend.packages.orphanCount > 0
                        text: {
                            void translator.language
                            return qsTr("Clean orphans (%1)").arg(translator.formatBytes(backend.packages.orphanBytes))
                        }
                        enabled: !backend.busy
                        flat: true
                        Material.foreground: theme.orange
                        Material.roundedScale: theme.radius
                        background: Rectangle {
                            radius: Math.min(6, theme.radius)
                            color: parent.hovered
                                   ? Qt.rgba(Qt.color(theme.orange).r, Qt.color(theme.orange).g, Qt.color(theme.orange).b, 0.18)
                                   : theme.background
                            border.width: 1
                            border.color: Qt.rgba(Qt.color(theme.orange).r, Qt.color(theme.orange).g, Qt.color(theme.orange).b, 0.5)
                        }
                        onClicked: win.promptCleanOrphans()
                    }

                    // Primary Update All button.
                    Button {
                        id: updateAllBtn
                        text: backend.packages.updatesCount > 0
                              ? qsTr("Update all (%1)").arg(translator.formatNumber(backend.packages.updatesCount))
                              : qsTr("Up to date")
                        enabled: !backend.busy && backend.packages.updatesCount > 0
                        topInset: 0
                        bottomInset: 0
                        leftPadding: 14
                        rightPadding: 14
                        topPadding: 0
                        bottomPadding: 0
                        implicitHeight: 36
                        contentItem: Text {
                            text: updateAllBtn.text
                            color: updateAllBtn.enabled ? theme.accentForeground : theme.darkForeground
                            font.pixelSize: 13
                            font.weight: Font.DemiBold
                            horizontalAlignment: Text.AlignHCenter
                            verticalAlignment: Text.AlignVCenter
                        }
                        background: Rectangle {
                            implicitHeight: 36
                            radius: Math.min(6, theme.radius)
                            color: updateAllBtn.enabled
                                   ? (updateAllBtn.down ? Qt.darker(theme.accent, 1.15)
                                      : (updateAllBtn.hovered ? Qt.lighter(theme.accent, 1.08) : theme.accent))
                                   : theme.background
                            border.width: updateAllBtn.enabled ? 0 : 1
                            border.color: theme.selection
                        }
                        onClicked: backend.updateAll()
                    }

                    // Shortcuts overlay trigger.
                    Button {
                        text: "?"
                        implicitWidth: 36
                        flat: true
                        Material.foreground: theme.darkForeground
                        Material.roundedScale: theme.radius
                        ToolTip.visible: hovered
                        ToolTip.text: qsTr("Keyboard shortcuts")
                        onClicked: win.helpVisible = !win.helpVisible
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: theme.selection
                }
            }

            // Segmented Disk Usage Breakdown Bar.
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: Math.max(42, legendFlow.implicitHeight + 20)
                color: theme.background
                clip: true

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 18
                    spacing: 14

                    // Segmented bar.
                    Rectangle {
                        Layout.preferredWidth: Math.min(180, Math.max(64, win.width * 0.14))
                        Layout.preferredHeight: 8
                        Layout.alignment: Qt.AlignVCenter
                        radius: Math.min(4, theme.radius)
                        color: theme.darkBackground
                        clip: true

                        Row {
                            anchors.fill: parent
                            readonly property real total: Math.max(1, backend.packages.totalBytes)

                            Rectangle {
                                height: parent.height
                                width: parent.width * (backend.packages.omarchyBytes / parent.total)
                                color: theme.accent
                            }
                            Rectangle {
                                height: parent.height
                                width: parent.width * (backend.packages.pacmanBytes / parent.total)
                                color: theme.blue
                            }
                            Rectangle {
                                height: parent.height
                                width: parent.width * (backend.packages.aurBytes / parent.total)
                                color: theme.magenta
                            }
                            Rectangle {
                                height: parent.height
                                width: parent.width * (backend.packages.flatpakBytes / parent.total)
                                color: theme.cyan
                            }
                            Rectangle {
                                height: parent.height
                                width: parent.width * (backend.packages.pluginBytes / parent.total)
                                color: theme.yellow
                            }
                            Rectangle {
                                height: parent.height
                                width: parent.width * (backend.packages.miseBytes / parent.total)
                                color: theme.green
                            }
                        }
                    }

                    // Clickable legend items.
                    Flow {
                        id: legendFlow
                        Layout.fillWidth: true
                        Layout.alignment: Qt.AlignVCenter
                        spacing: 12

                        Repeater {
                            model: [
                                { id: "omarchy", label: qsTr("Omarchy"), color: theme.accent, bytes: backend.packages.omarchyBytes, show: true },
                                { id: "pacman", label: qsTr("Pacman"), color: theme.blue, bytes: backend.packages.pacmanBytes, show: true },
                                { id: "aur", label: qsTr("AUR"), color: theme.magenta, bytes: backend.packages.aurBytes, show: true },
                                { id: "flatpak", label: qsTr("Flatpak"), color: theme.cyan, bytes: backend.packages.flatpakBytes, show: backend.hasFlatpak || backend.packages.flatpakCount > 0 },
                                { id: "omarchy-plugin", label: qsTr("Omarchy Plugins"), color: theme.yellow, bytes: backend.packages.pluginBytes, show: backend.hasPlugins || backend.packages.pluginCount > 0 },
                                { id: "mise", label: qsTr("Mise"), color: theme.green, bytes: backend.packages.miseBytes, show: backend.hasMise || backend.packages.miseCount > 0 }
                            ]

                            delegate: Row {
                                visible: modelData.show
                                spacing: 5

                                Rectangle {
                                    width: 8
                                    height: 8
                                    radius: 4
                                    anchors.verticalCenter: parent.verticalCenter
                                    color: modelData.color
                                }

                                Text {
                                    anchors.verticalCenter: parent.verticalCenter
                                    text: {
                                        void translator.language
                                        return modelData.label + " " + translator.formatBytes(modelData.bytes)
                                    }
                                    color: backend.packages.filterSource === modelData.id
                                           ? theme.brightForeground
                                           : theme.darkForeground
                                    font.pixelSize: 12
                                    font.weight: backend.packages.filterSource === modelData.id ? Font.DemiBold : Font.Normal

                                    MouseArea {
                                        anchors.fill: parent
                                        cursorShape: Qt.PointingHandCursor
                                        onClicked: backend.packages.filterSource =
                                            backend.packages.filterSource === modelData.id ? "all" : modelData.id
                                    }
                                }
                            }
                        }
                    }
                }

                Rectangle {
                    anchors.left: parent.left
                    anchors.right: parent.right
                    anchors.bottom: parent.bottom
                    height: 1
                    color: theme.selection
                }
            }

            // Error or active operation banner.
            Rectangle {
                visible: backend.errorMessage.length > 0 || backend.busy
                Layout.fillWidth: true
                Layout.preferredHeight: 38
                color: backend.errorMessage.length > 0
                       ? Qt.rgba(Qt.color(theme.red).r, Qt.color(theme.red).g, Qt.color(theme.red).b, 0.18)
                       : Qt.rgba(Qt.color(theme.accent).r, Qt.color(theme.accent).g, Qt.color(theme.accent).b, 0.14)

                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 18
                    anchors.rightMargin: 16
                    spacing: 10

                    BusyIndicator {
                        visible: backend.busy
                        running: backend.busy
                        implicitWidth: 20
                        implicitHeight: 20
                        Material.accent: theme.accent
                    }

                    Text {
                        Layout.fillWidth: true
                        text: backend.errorMessage.length > 0 ? backend.errorMessage : backend.statusMessage
                        color: backend.errorMessage.length > 0 ? theme.red : theme.brightForeground
                        font.pixelSize: 12
                        font.weight: Font.Medium
                        elide: Text.ElideRight
                    }

                    Button {
                        visible: backend.busy
                        text: win.logVisible ? qsTr("Hide log") : qsTr("View log")
                        flat: true
                        implicitHeight: 28
                        Material.foreground: theme.accent
                        Material.roundedScale: theme.radius
                        onClicked: win.logVisible = !win.logVisible
                    }

                    Button {
                        visible: backend.busy
                        text: qsTr("Cancel")
                        flat: true
                        implicitHeight: 28
                        Material.foreground: theme.red
                        Material.roundedScale: theme.radius
                        onClicked: backend.cancelOperation()
                    }
                }
            }

            // Main 3-column workspace (Sidebar | Package List | Detail Pane).
            RowLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                spacing: 0

                // Left Sidebar.
                Rectangle {
                    Layout.preferredWidth: win.width < 940 ? 204 : 212
                    Layout.fillHeight: true
                    color: theme.darkBackground

                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 10
                        spacing: 4

                        Text {
                            text: qsTr("Sources")
                            color: theme.darkForeground
                            font.pixelSize: 11
                            font.bold: true
                            Layout.leftMargin: 8
                            Layout.topMargin: 4
                            Layout.bottomMargin: 4
                        }

                        Repeater {
                            model: [
                                { id: "all", label: qsTr("All"), count: backend.packages.scopeCount, dot: theme.foreground, show: true },
                                { id: "updates", label: qsTr("Updates"), count: backend.packages.updatesCount, dot: theme.green, show: true },
                                { id: "omarchy", label: qsTr("Omarchy"), count: backend.packages.omarchyCount, dot: theme.accent, show: true },
                                { id: "pacman", label: qsTr("Pacman"), count: backend.packages.pacmanCount, dot: theme.blue, show: true },
                                { id: "aur", label: qsTr("AUR"), count: backend.packages.aurCount, dot: theme.magenta, show: true },
                                { id: "flatpak", label: qsTr("Flatpak"), count: backend.packages.flatpakCount, dot: theme.cyan, show: backend.hasFlatpak || backend.packages.flatpakCount > 0 },
                                { id: "omarchy-plugin", label: qsTr("Omarchy Plugins"), count: backend.packages.pluginCount, dot: theme.yellow, show: backend.hasPlugins || backend.packages.pluginCount > 0 },
                                { id: "mise", label: qsTr("Mise"), count: backend.packages.miseCount, dot: theme.green, show: backend.hasMise || backend.packages.miseCount > 0 },
                                { id: "orphans", label: qsTr("Orphans"), count: backend.packages.orphanCount, dot: theme.orange, show: backend.packages.orphanCount > 0 }
                            ]

                            delegate: Rectangle {
                                visible: modelData.show
                                Layout.fillWidth: true
                                Layout.preferredHeight: 36
                                radius: Math.min(6, theme.radius)
                                readonly property bool active: backend.packages.filterSource === modelData.id
                                color: active
                                       ? theme.selection
                                       : (navArea.containsMouse ? theme.lighterBackground : "transparent")

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 10
                                    spacing: 8

                                    Rectangle {
                                        width: 8
                                        height: 8
                                        radius: 4
                                        color: modelData.dot
                                    }

                                    Text {
                                        Layout.fillWidth: true
                                        text: modelData.label
                                        color: active ? theme.brightForeground : theme.foreground
                                        font.pixelSize: 13
                                        font.weight: active ? Font.DemiBold : Font.Normal
                                        elide: Text.ElideRight
                                    }

                                    Rectangle {
                                        height: 20
                                        implicitWidth: Math.max(24, countText.implicitWidth + 10)
                                        radius: Math.min(10, theme.radius)
                                        color: modelData.id === "updates" && modelData.count > 0
                                               ? theme.green
                                               : theme.background

                                        Text {
                                            id: countText
                                            anchors.centerIn: parent
                                            text: {
                                                void translator.language
                                                return translator.formatNumber(modelData.count)
                                            }
                                            color: modelData.id === "updates" && modelData.count > 0
                                                   ? (theme.isLight ? "white" : "black")
                                                   : theme.darkForeground
                                            font.pixelSize: 11
                                            font.bold: modelData.id === "updates" && modelData.count > 0
                                        }
                                    }
                                }

                                MouseArea {
                                    id: navArea
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: backend.packages.filterSource = modelData.id
                                }
                            }
                        }

                        Item { Layout.fillHeight: true }

                        Rectangle {
                            Layout.fillWidth: true
                            height: 1
                            color: theme.selection
                        }

                        // Explicit apps vs all dependencies toggle.
                        RowLayout {
                            Layout.fillWidth: true
                            Layout.leftMargin: 6
                            Layout.rightMargin: 4
                            Layout.topMargin: 4

                            Text {
                                Layout.fillWidth: true
                                text: qsTr("Dependencies")
                                color: theme.foreground
                                font.pixelSize: 12
                                elide: Text.ElideRight
                            }

                            Switch {
                                checked: backend.packages.showDependencies
                                Material.accent: theme.accent
                                onToggled: backend.packages.showDependencies = checked
                            }
                        }

                        // Activity log drawer button.
                        Button {
                            Layout.fillWidth: true
                            text: win.logVisible ? qsTr("Hide activity log") : qsTr("Activity log")
                            flat: true
                            Material.foreground: win.logVisible ? theme.accent : theme.darkForeground
                            Material.roundedScale: theme.radius
                            onClicked: win.logVisible = !win.logVisible
                        }
                    }

                    Rectangle {
                        anchors.top: parent.top
                        anchors.bottom: parent.bottom
                        anchors.right: parent.right
                        width: 1
                        color: theme.selection
                    }
                }

                // Center Package List + Collapsible Log Drawer.
                ColumnLayout {
                    Layout.fillWidth: true
                    Layout.minimumWidth: 260
                    Layout.fillHeight: true
                    spacing: 0

                    // Search and Sort bar.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 52
                        color: theme.darkerBackground

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 12
                            anchors.rightMargin: 12
                            spacing: 10

                            // Search box.
                            Rectangle {
                                Layout.fillWidth: true
                                Layout.minimumWidth: 100
                                Layout.preferredHeight: 34
                                radius: Math.min(6, theme.radius)
                                color: theme.background
                                border.width: 1
                                border.color: searchInput.activeFocus ? theme.accent : theme.selection

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 10
                                    anchors.rightMargin: 8
                                    spacing: 6

                                    TextInput {
                                        id: searchInput
                                        Layout.fillWidth: true
                                        color: theme.brightForeground
                                        selectionColor: theme.accent
                                        selectedTextColor: theme.accentForeground
                                        font.pixelSize: 13
                                        clip: true
                                        onTextChanged: backend.packages.searchQuery = text

                                        Text {
                                            anchors.verticalCenter: parent.verticalCenter
                                            visible: searchInput.text.length === 0
                                            text: qsTr("Search apps and packages… (/)")
                                            color: theme.darkForeground
                                            font.pixelSize: 13
                                        }
                                    }

                                    Text {
                                        visible: searchInput.text.length > 0
                                        text: "×"
                                        color: theme.darkForeground
                                        font.pixelSize: 16
                                        MouseArea {
                                            anchors.fill: parent
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: searchInput.text = ""
                                        }
                                    }
                                }
                            }

                            // Sort dropdown.
                            ComboBox {
                                id: sortBox
                                Layout.preferredWidth: 168
                                implicitHeight: 34
                                Material.accent: theme.accent
                                Material.roundedScale: theme.radius
                                textRole: "label"
                                valueRole: "key"
                                model: {
                                    void translator.language
                                    return [
                                        { key: "size", label: qsTr("Sort: Largest") },
                                        { key: "updates", label: qsTr("Sort: Updates") },
                                        { key: "name", label: qsTr("Sort: Name") },
                                        { key: "date", label: qsTr("Sort: Newest") }
                                    ]
                                }
                                currentIndex: {
                                    var s = backend.packages.sortBy
                                    if (s === "updates") return 1
                                    if (s === "name") return 2
                                    if (s === "date") return 3
                                    return 0
                                }
                                onActivated: backend.packages.sortBy = currentValue
                            }
                        }
                    }

                    // List summary bar.
                    Rectangle {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 26
                        color: theme.darkerBackground

                        RowLayout {
                            anchors.fill: parent
                            anchors.leftMargin: 14
                            anchors.rightMargin: 14

                            Text {
                                text: {
                                    void translator.language
                                    return qsTr("%1 · %2")
                                        .arg(qsTr("%n items", "", backend.packages.visibleCount))
                                        .arg(translator.formatBytes(backend.packages.visibleBytes))
                                }
                                color: theme.darkForeground
                                font.pixelSize: 11
                            }

                            Item { Layout.fillWidth: true }

                            Text {
                                visible: backend.checkingUpdates
                                text: qsTr("Checking for updates…")
                                color: theme.accent
                                font.pixelSize: 11
                            }
                        }
                    }

                    // Scrollable Package ListView.
                    Item {
                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        ListView {
                            id: packageList
                            anchors.fill: parent
                            anchors.topMargin: 4
                            anchors.bottomMargin: 6
                            clip: true
                            model: backend.packages
                            boundsBehavior: Flickable.StopAtBounds
                            ScrollBar.vertical: ScrollBar { policy: ScrollBar.AsNeeded }

                            delegate: PackageRow {
                                selected: pkgId === win.selectedId
                                busy: backend.busy
                                onClicked: win.selectByIndex(index)
                                onUpdateRequested: backend.updatePackage(pkgId)
                            }
                        }

                        // Loading or Empty state.
                        ColumnLayout {
                            anchors.centerIn: parent
                            visible: backend.loading || backend.packages.visibleCount === 0
                            spacing: 8

                            BusyIndicator {
                                Layout.alignment: Qt.AlignHCenter
                                visible: backend.loading
                                running: backend.loading
                                Material.accent: theme.accent
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                text: backend.loading
                                      ? qsTr("Loading installed packages…")
                                      : (backend.packages.filterSource === "updates"
                                         ? qsTr("Everything is up to date")
                                         : qsTr("No packages match this filter"))
                                color: theme.foreground
                                font.pixelSize: 15
                                font.weight: Font.DemiBold
                            }

                            Text {
                                Layout.alignment: Qt.AlignHCenter
                                visible: !backend.loading && backend.packages.filterSource === "updates"
                                text: qsTr("Press R anytime to check all package sources again.")
                                color: theme.darkForeground
                                font.pixelSize: 12
                            }
                        }
                    }

                    // Collapsible Activity Log Drawer.
                    LogDrawer {
                        visible: win.logVisible
                        Layout.fillWidth: true
                        Layout.preferredHeight: 190
                        logText: backend.logText
                        busy: backend.busy
                        statusMessage: backend.statusMessage
                        lastSucceeded: backend.lastOperationSucceeded
                        onCloseRequested: win.logVisible = false
                        onClearRequested: backend.clearLog()
                        onCancelRequested: backend.cancelOperation()
                    }
                }

                // Right Detail Inspector.
                Rectangle {
                    visible: win.width >= 760
                    Layout.preferredWidth: 1
                    Layout.fillHeight: true
                    color: theme.selection
                }

                DetailPane {
                    visible: win.width >= 760
                    Layout.preferredWidth: win.width < 1020 ? 276 : 336
                    Layout.fillHeight: true
                    pkg: win.selectedPkg
                    details: backend.selectedDetails
                    loadingDetails: backend.loadingDetails
                    busy: backend.busy
                    onUpdateRequested: function(id) { backend.updatePackage(id) }
                    onRemoveRequested: function(id) { win.promptRemove(id) }
                    onLaunchRequested: function(id) { backend.launchApp(id) }
                }
            }
        }

        // Confirmation dialog overlay.
        ConfirmDialog {
            id: confirmDialog
            onCancelled: confirmDialog.open = false
            onConfirmed: {
                confirmDialog.open = false
                if (win.pendingAction === "remove")
                    backend.removePackage(win.pendingPkgId)
                else if (win.pendingAction === "orphans")
                    backend.cleanOrphans()
                else if (win.pendingAction === "quit") {
                    backend.cancelOperation()
                    Qt.quit()
                }
            }
        }

        // Keyboard shortcuts overlay.
        ShortcutsOverlay {
            open: win.helpVisible
            onCloseRequested: win.helpVisible = false
        }
    }
}
