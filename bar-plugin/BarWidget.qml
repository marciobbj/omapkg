import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

// Omarchy bar widget for Update Central: polls installed sources (Omarchy,
// Pacman, AUR, Flatpak) via `omarchy-update-central summary --json`, lights up
// with an accent indicator when updates are waiting, and opens or focuses the
// desktop app on click.
BarWidget {
  id: root
  moduleName: "local.omarchy-update-central"

  property int updateCount: 0
  property bool checking: false

  function refresh() {
    if (checkProc.running) return
    root.checking = true
    checkProc.running = true
  }

  function applySummary(raw) {
    try {
      var parsed = JSON.parse(String(raw || "").trim())
      var count = Number(parsed && parsed.updatesCount)
      root.updateCount = isFinite(count) && count > 0 ? Math.floor(count) : 0
    } catch (e) {
      // Keep the previous count on transient network or parse failures.
    }
  }

  function openApp() {
    if (!root.bar) return
    root.bar.run("omarchy-launch-or-focus omarchy-update-central omarchy-update-central")
  }

  function localizedTooltip() {
    var lang = String(I18n.language || "en")
    var n = root.updateCount
    if (lang.indexOf("pt") === 0) {
      if (root.checking && n === 0) return "Central de Atualizações — Verificando…"
      return n > 0
        ? "Central de Atualizações — " + n + (n === 1 ? " atualização disponível" : " atualizações disponíveis")
        : "Central de Atualizações — Tudo atualizado"
    }
    if (lang.indexOf("es") === 0) {
      if (root.checking && n === 0) return "Central de Actualizaciones — Buscando…"
      return n > 0
        ? "Central de Actualizaciones — " + n + (n === 1 ? " actualización disponible" : " actualizaciones disponibles")
        : "Central de Actualizaciones — Todo actualizado"
    }
    if (lang === "da") {
      if (root.checking && n === 0) return "Opdateringscentral — Tjekker…"
      return n > 0
        ? "Opdateringscentral — " + n + (n === 1 ? " opdatering tilgængelig" : " opdateringer tilgængelige")
        : "Opdateringscentral — Alt er opdateret"
    }
    if (lang === "de") {
      if (root.checking && n === 0) return "Update-Zentrale — Prüfe…"
      return n > 0
        ? "Update-Zentrale — " + n + (n === 1 ? " Update verfügbar" : " Updates verfügbar")
        : "Update-Zentrale — Alles ist aktuell"
    }
    if (lang === "fr") {
      if (root.checking && n === 0) return "Centre de mises à jour — Vérification…"
      return n > 0
        ? "Centre de mises à jour — " + n + (n === 1 ? " mise à jour disponible" : " mises à jour disponibles")
        : "Centre de mises à jour — Tout est à jour"
    }
    if (root.checking && n === 0) return "Update Central — Checking…"
    return n > 0
      ? "Update Central — " + n + (n === 1 ? " update available" : " updates available")
      : "Update Central — Everything is up to date"
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  ShellIpc {
    target: "local.omarchy-update-central"

    function refresh(): void {
      root.broadcast("refresh")
    }
  }

  Process {
    id: checkProc
    command: ["omarchy-update-central", "summary", "--json"]
    stdout: StdioCollector {
      waitForEnd: true
      onStreamFinished: root.applySummary(text)
    }
    onExited: {
      root.checking = false
    }
  }

  Timer {
    interval: 1800000
    running: true
    repeat: true
    triggeredOnStart: true
    onTriggered: root.refresh()
  }

  BarIconButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    // Nerd Font package-up icon when updates exist, package box when up to date.
    text: root.updateCount > 0 ? "\udb81\udeb0" : "\udb80\udfd7"
    slotSize: Style.bar.statusSlot
    active: root.updateCount > 0
    useActiveColor: true
    tooltipText: root.localizedTooltip()
    onPressed: function(b) {
      if (b === Qt.MiddleButton) root.refresh()
      else root.openApp()
    }
  }

  Rectangle {
    id: updateBadge
    enabled: false
    visible: root.updateCount > 0
    z: button.z + 1
    anchors.right: button.right
    anchors.rightMargin: Style.space(3)
    anchors.top: button.top
    anchors.topMargin: Style.space(5)
    width: Style.space(6)
    height: width
    radius: width / 2
    color: Color.accent
  }
}
