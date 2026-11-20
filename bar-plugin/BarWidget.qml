import QtQuick
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

// Omarchy bar widget for Omapkg: polls installed sources (Omarchy,
// Pacman, AUR, Flatpak) via `omapkg summary --json`, lights up with
// an accent indicator when updates are waiting, and opens or focuses
// the desktop app on click.
BarWidget {
  id: root
  moduleName: "marciobbj.omapkg"

  property int updateCount: 0
  property bool checking: false
  readonly property string helperPath: decodeURIComponent(String(Qt.resolvedUrl("omapkg-bar-helper")).replace(/^file:\/\//, ""))

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
    Util.execArgv([root.helperPath, "open"])
  }

  function localizedTooltip() {
    var lang = String(I18n.language || "en")
    var n = root.updateCount
    if (lang.indexOf("pt") === 0) {
      if (root.checking && n === 0) return "Omapkg — Verificando…"
      return n > 0
        ? "Omapkg — " + n + (n === 1 ? " atualização disponível" : " atualizações disponíveis")
        : "Omapkg — Tudo atualizado"
    }
    if (lang.indexOf("es") === 0) {
      if (root.checking && n === 0) return "Omapkg — Buscando…"
      return n > 0
        ? "Omapkg — " + n + (n === 1 ? " actualización disponible" : " actualizaciones disponibles")
        : "Omapkg — Todo actualizado"
    }
    if (lang === "da") {
      if (root.checking && n === 0) return "Omapkg — Tjekker…"
      return n > 0
        ? "Omapkg — " + n + (n === 1 ? " opdatering tilgængelig" : " opdateringer tilgængelige")
        : "Omapkg — Alt er opdateret"
    }
    if (lang === "de") {
      if (root.checking && n === 0) return "Omapkg — Prüfe…"
      return n > 0
        ? "Omapkg — " + n + (n === 1 ? " Update verfügbar" : " Updates verfügbar")
        : "Omapkg — Alles ist aktuell"
    }
    if (lang === "fr") {
      if (root.checking && n === 0) return "Omapkg — Vérification…"
      return n > 0
        ? "Omapkg — " + n + (n === 1 ? " mise à jour disponible" : " mises à jour disponibles")
        : "Omapkg — Tout est à jour"
    }
    if (root.checking && n === 0) return "Omapkg — Checking…"
    return n > 0
      ? "Omapkg — " + n + (n === 1 ? " update available" : " updates available")
      : "Omapkg — Everything is up to date"
  }

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  ShellIpc {
    target: "marciobbj.omapkg"

    function refresh(): void {
      root.broadcast("refresh")
    }
  }

  Process {
    id: checkProc
    command: [root.helperPath, "summary"]
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
    pressFeedbackButtons: Qt.LeftButton
    tooltipText: root.localizedTooltip()
    onPressed: function(b) {
      if (b === Qt.LeftButton) root.openApp()
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
