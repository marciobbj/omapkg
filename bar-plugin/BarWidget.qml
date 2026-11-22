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
  property int securityAlertCount: 0
  property string securitySeverity: ""
  readonly property bool urgentSecurityAlert: root.securityAlertCount > 0 && (root.securitySeverity === "critical" || root.securitySeverity === "high")
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
      var secCount = Number(parsed && parsed.securityAlertsCount)
      root.securityAlertCount = isFinite(secCount) && secCount > 0 ? Math.floor(secCount) : 0
      root.securitySeverity = parsed && parsed.highestSecuritySeverity ? String(parsed.highestSecuritySeverity) : ""
    } catch (e) {
      // Keep the previous counts on transient network or parse failures.
    }
  }

  function openApp() {
    Util.execArgv([root.helperPath, "open"])
  }

  function localizedTooltip() {
    var lang = String(I18n.language || "en")
    var n = root.updateCount
    var s = root.securityAlertCount
    if (lang.indexOf("pt") === 0) {
      if (root.checking && n === 0 && s === 0) return "Omapkg — Verificando…"
      var partsPt = []
      if (s > 0) partsPt.push(s + (s === 1 ? " alerta de segurança" : " alertas de segurança"))
      if (n > 0) partsPt.push(n + (n === 1 ? " atualização disponível" : " atualizações disponíveis"))
      return partsPt.length > 0 ? "Omapkg — " + partsPt.join(" · ") : "Omapkg — Tudo atualizado"
    }
    if (lang.indexOf("es") === 0) {
      if (root.checking && n === 0 && s === 0) return "Omapkg — Buscando…"
      var partsEs = []
      if (s > 0) partsEs.push(s + (s === 1 ? " alerta de seguridad" : " alertas de seguridad"))
      if (n > 0) partsEs.push(n + (n === 1 ? " actualización disponible" : " actualizaciones disponibles"))
      return partsEs.length > 0 ? "Omapkg — " + partsEs.join(" · ") : "Omapkg — Todo actualizado"
    }
    if (lang === "da") {
      if (root.checking && n === 0 && s === 0) return "Omapkg — Tjekker…"
      var partsDa = []
      if (s > 0) partsDa.push(s + (s === 1 ? " sikkerhedsadvarsel" : " sikkerhedsadvarsler"))
      if (n > 0) partsDa.push(n + (n === 1 ? " opdatering tilgængelig" : " opdateringer tilgængelige"))
      return partsDa.length > 0 ? "Omapkg — " + partsDa.join(" · ") : "Omapkg — Alt er opdateret"
    }
    if (lang === "de") {
      if (root.checking && n === 0 && s === 0) return "Omapkg — Prüfe…"
      var partsDe = []
      if (s > 0) partsDe.push(s + (s === 1 ? " Sicherheitswarnung" : " Sicherheitswarnungen"))
      if (n > 0) partsDe.push(n + (n === 1 ? " Update verfügbar" : " Updates verfügbar"))
      return partsDe.length > 0 ? "Omapkg — " + partsDe.join(" · ") : "Omapkg — Alles ist aktuell"
    }
    if (lang === "fr") {
      if (root.checking && n === 0 && s === 0) return "Omapkg — Vérification…"
      var partsFr = []
      if (s > 0) partsFr.push(s + (s === 1 ? " alerte de sécurité" : " alertes de sécurité"))
      if (n > 0) partsFr.push(n + (n === 1 ? " mise à jour disponible" : " mises à jour disponibles"))
      return partsFr.length > 0 ? "Omapkg — " + partsFr.join(" · ") : "Omapkg — Tout est à jour"
    }
    if (root.checking && n === 0 && s === 0) return "Omapkg — Checking…"
    var partsEn = []
    if (s > 0) partsEn.push(s + (s === 1 ? " security alert" : " security alerts"))
    if (n > 0) partsEn.push(n + (n === 1 ? " update available" : " updates available"))
    return partsEn.length > 0 ? "Omapkg — " + partsEn.join(" · ") : "Omapkg — Everything is up to date"
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
    // Nerd Font package-up icon when updates or security alerts exist, package box when clean.
    text: (root.securityAlertCount > 0 || root.updateCount > 0) ? "\udb81\udeb0" : "\udb80\udfd7"
    slotSize: Style.bar.statusSlot
    active: root.securityAlertCount > 0 || root.updateCount > 0
    activeColor: root.urgentSecurityAlert ? Color.urgent : Color.accent
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
    visible: root.securityAlertCount > 0 || root.updateCount > 0
    z: button.z + 1
    anchors.right: button.right
    anchors.rightMargin: Style.space(3)
    anchors.top: button.top
    anchors.topMargin: Style.space(5)
    width: Style.space(6)
    height: width
    radius: width / 2
    color: root.urgentSecurityAlert ? Color.urgent : Color.accent
  }
}
