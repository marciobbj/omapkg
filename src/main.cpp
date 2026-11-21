// omapkg — manage installed Omarchy, Pacman, AUR, Flatpak, Omarchy Plugin, and Mise packages.

#include <QCoreApplication>
#include <QGuiApplication>
#include <QIcon>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQuickStyle>
#include <QTextStream>
#include <QUrl>

#include "backend.h"
#include "iconprovider.h"
#include "theme.h"
#include "translator.h"

namespace {

int runCli(int argc, char *argv[], const QString &command) {
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    app.setApplicationName("omapkg");
    app.setApplicationVersion("0.2.0");

    bool asJson = false;
    for (int i = 2; i < argc; ++i)
        if (QString::fromLocal8Bit(argv[i]) == "--json")
            asJson = true;

    Backend backend(false);
    const bool needUpdates = (command == "updates" || command == "summary");
    const QJsonObject snap = backend.runCliSnapshot(needUpdates);

    QTextStream out(stdout);
    if (command == "summary") {
        QJsonObject summary = snap;
        summary.remove("packages");
        if (asJson) {
            out << QJsonDocument(summary).toJson(QJsonDocument::Indented);
        } else {
            out << "Total packages: " << summary.value("totalPackages").toInt() << "\n"
                << "Updates available: " << summary.value("updatesCount").toInt() << "\n"
                << "Orphans: " << summary.value("orphanCount").toInt() << "\n"
                << "Total bytes: " << summary.value("totalBytes").toVariant().toLongLong() << "\n";
        }
        return 0;
    }

    QJsonArray filtered;
    for (const auto &v : snap.value("packages").toArray()) {
        const QJsonObject obj = v.toObject();
        if (command == "updates" && !obj.value("hasUpdate").toBool())
            continue;
        filtered.append(obj);
    }

    if (asJson) {
        out << QJsonDocument(filtered).toJson(QJsonDocument::Indented);
    } else {
        for (const auto &v : filtered) {
            const QJsonObject o = v.toObject();
            out << o.value("source").toString() << "\t"
                << o.value("name").toString() << "\t"
                << o.value("version").toString();
            if (o.value("hasUpdate").toBool())
                out << " -> " << o.value("availableVersion").toString();
            out << "\t" << o.value("sizeBytes").toVariant().toLongLong() << " B\n";
        }
    }
    return 0;
}

} // namespace

int main(int argc, char *argv[]) {
    if (argc >= 2) {
        const QString cmd = QString::fromLocal8Bit(argv[1]);
        if (cmd == "list" || cmd == "updates" || cmd == "summary")
            return runCli(argc, argv, cmd);
        if (cmd == "--help" || cmd == "-h") {
            QTextStream(stdout)
                << "Usage: omapkg [open|list|updates|summary] [--json]\n";
            return 0;
        }
    }

    QGuiApplication app(argc, argv);
    app.setApplicationName("omapkg");
    app.setApplicationVersion("0.2.0");

    // Matches the window to omapkg.desktop so the compositor
    // (Wayland app_id) and app launcher pick up the installed icon.
    app.setDesktopFileName("omapkg");
    app.setWindowIcon(QIcon::fromTheme("omapkg"));

    QQuickStyle::setStyle("Material");

    // Installed before anything is translated, and every qsTr() binding is
    // re-evaluated when the Omarchy language changes.
    Translator translator;
    app.installTranslator(&translator);

    Theme theme;
    Backend backend(true);

    QQmlApplicationEngine engine;
    engine.addImageProvider("themeicon", new IconProvider());
    QObject::connect(&translator, &Translator::changed, &engine, &QQmlEngine::retranslate);
    engine.rootContext()->setContextProperty("translator", &translator);
    engine.rootContext()->setContextProperty("theme", &theme);
    engine.rootContext()->setContextProperty("backend", &backend);
    engine.load(QUrl("qrc:/Main.qml"));
    if (engine.rootObjects().isEmpty())
        return 1;

    return app.exec();
}
