#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "backend.h"
#include "packagemodel.h"
#include "theme.h"
#include "translator.h"

class OmapkgTests : public QObject {
    Q_OBJECT

private slots:
    void themeFallbackAndPalette() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());

        // Missing theme directory falls back cleanly to dark defaults.
        Theme fallback(dir.filePath("missing"), nullptr, "/nonexistent-hyprctl");
        QCOMPARE(fallback.isLight(), false);
        QCOMPARE(fallback.accent(), QString("#FFD60A"));
        QCOMPARE(fallback.radius(), 0);

        // Light mode colors.toml with custom accent and contrast foreground.
        const QString themeDir = dir.filePath("theme");
        QVERIFY(QDir().mkpath(themeDir));
        QFile colors(themeDir + "/colors.toml");
        QVERIFY(colors.open(QIODevice::WriteOnly | QIODevice::Text));
        colors.write(
            "mode = \"light\"\n"
            "accent = \"#1E66F5\"\n"
            "background = \"#EFF1F5\"\n"
            "foreground = \"#4C4F69\"\n"
            "color1 = \"#D20F39\"\n"
            "color2 = \"#40A02B\"\n");
        colors.close();

        // Fake hyprctl returning rounding = 10.
        const QString fakeHyprctl = dir.filePath("hyprctl");
        QFile script(fakeHyprctl);
        QVERIFY(script.open(QIODevice::WriteOnly | QIODevice::Text));
        script.write("#!/bin/sh\necho '{\"int\": 10}'\n");
        script.close();
        QVERIFY(script.setPermissions(QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner));

        Theme themed(dir.path(), nullptr, fakeHyprctl);
        QCOMPARE(themed.isLight(), true);
        QCOMPARE(themed.accent(), QString("#1E66F5"));
        QCOMPARE(themed.accentForeground(), QString("white"));
        QCOMPARE(themed.background(), QString("#EFF1F5"));
        QCOMPARE(themed.red(), QString("#D20F39"));
        QCOMPARE(themed.green(), QString("#40A02B"));
        QTRY_COMPARE(themed.radius(), 10);

        QCOMPARE(Theme::contrastForeground("#FFD60A"), QString("black"));
        QCOMPARE(Theme::contrastForeground("#112233"), QString("white"));
    }

    void translatorCatalogsAndPlurals() {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString langFile = dir.filePath("language");

        auto writeLang = [&](const QByteArray &code) {
            QFile f(langFile);
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate));
            f.write(code);
            f.close();
        };

        // Brazilian Portuguese from embedded :/i18n/pt-BR.json
        writeLang("pt-BR\n");
        Translator trPtBr(langFile, ":/i18n");
        QCOMPARE(trPtBr.language(), QString("pt-BR"));
        QCOMPARE(trPtBr.isRtl(), false);
        QCOMPARE(trPtBr.translate("Main", "Check updates"), QString("Verificar atualizações"));
        QCOMPARE(trPtBr.translate("Main", "%n packages", nullptr, 1), QString("%n pacote"));
        QCOMPARE(trPtBr.translate("Main", "%n packages", nullptr, 5), QString("%n pacotes"));

        // Arabic RTL detection
        writeLang("ar\n");
        Translator trAr(langFile, ":/i18n");
        QCOMPARE(trAr.language(), QString("ar"));
        QCOMPARE(trAr.isRtl(), true);

        // CLDR plural rules across families
        QCOMPARE(Translator::pluralCategory("en", 1), QString("one"));
        QCOMPARE(Translator::pluralCategory("en", 0), QString("other"));
        QCOMPARE(Translator::pluralCategory("pt-BR", 0), QString("one"));
        QCOMPARE(Translator::pluralCategory("pt-BR", 1), QString("one"));
        QCOMPARE(Translator::pluralCategory("pt-BR", 2), QString("other"));
        QCOMPARE(Translator::pluralCategory("ru", 1), QString("one"));
        QCOMPARE(Translator::pluralCategory("ru", 2), QString("few"));
        QCOMPARE(Translator::pluralCategory("ru", 5), QString("many"));
        QCOMPARE(Translator::pluralCategory("ru", 11), QString("many"));
        QCOMPARE(Translator::pluralCategory("ru", 21), QString("one"));
        QCOMPARE(Translator::pluralCategory("pl", 1), QString("one"));
        QCOMPARE(Translator::pluralCategory("pl", 2), QString("few"));
        QCOMPARE(Translator::pluralCategory("pl", 5), QString("many"));
        QCOMPARE(Translator::pluralCategory("ar", 0), QString("zero"));
        QCOMPARE(Translator::pluralCategory("ar", 1), QString("one"));
        QCOMPARE(Translator::pluralCategory("ar", 2), QString("two"));
        QCOMPARE(Translator::pluralCategory("ar", 5), QString("few"));
        QCOMPARE(Translator::pluralCategory("ar", 15), QString("many"));
        QCOMPARE(Translator::pluralCategory("ar", 100), QString("other"));
        QCOMPARE(Translator::pluralCategory("ja", 1), QString("other"));

        // Custom region -> base fallback
        const QString customDir = dir.filePath("i18n");
        QVERIFY(QDir().mkpath(customDir));
        {
            QFile f(customDir + "/es.json");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"Save":"Guardar","Cancel":"Cancelar"})");
        }
        {
            QFile f(customDir + "/es-MX.json");
            QVERIFY(f.open(QIODevice::WriteOnly));
            f.write(R"({"Save":"Guardar MX"})");
        }
        writeLang("es-MX\n");
        Translator trCustom(langFile, customDir);
        QCOMPARE(trCustom.translate("Main", "Save"), QString("Guardar MX"));
        QCOMPARE(trCustom.translate("Main", "Cancel"), QString("Cancelar"));

        // Byte formatting sanity
        QVERIFY(!trPtBr.formatBytes(0).isEmpty());
        QVERIFY(!trPtBr.formatBytes(26'900'000).isEmpty());
    }

    void backendParsers() {
        // Size parser including Unicode NBSP (\u00A0), narrow NBSP (\u202F), and comma decimals
        QCOMPARE(Backend::parseSizeBytes("26.9 MB"), 26'900'000LL);
        QCOMPARE(Backend::parseSizeBytes(QString::fromUtf8("26,9\u00A0MB")), 26'900'000LL);
        QCOMPARE(Backend::parseSizeBytes(QString::fromUtf8("1,3\u202FGB")), 1'300'000'000LL);
        QCOMPARE(Backend::parseSizeBytes("512 KiB"), 512LL * 1024LL);
        QCOMPARE(Backend::parseSizeBytes("2 GiB"), 2LL * 1024LL * 1024LL * 1024LL);
        QCOMPARE(Backend::parseSizeBytes("invalid"), 0LL);

        // Expac repos: first-wins ([omarchy] beats [extra])
        const auto repos = Backend::parseExpacRepos(
            "omarchy\tomarchy-shell\n"
            "omarchy\tchromium\n"
            "extra\tchromium\n"
            "core\tlinux\n");
        QCOMPARE(repos.value("omarchy-shell"), QString("omarchy"));
        QCOMPARE(repos.value("chromium"), QString("omarchy"));
        QCOMPARE(repos.value("linux"), QString("core"));

        // Expac installed
        const QString expacQ =
            "omarchy-shell\t1.2.0-1\t42000000\texplicit\t1700000000\thttps://omarchy.org\tOmarchy shell\n"
            "linux\t6.12.1-1\t150000000\tdependency\t1700000100\thttps://kernel.org\tLinux kernel\n"
            "yay-bin\t12.4.2-1\t9500000\texplicit\t1700000200\thttps://github.com/Jguer/yay\tAUR helper\n"
            "old-lib\t0.1.0-1\t5000000\tdependency\t1700000300\t\tUnused dependency\n";
        const auto items = Backend::parseExpacInstalled(
            expacQ, repos, {"old-lib"}, {"omarchy-shell"});
        QCOMPARE(items.size(), 4);
        QCOMPARE(items[0].source, QString("omarchy"));
        QCOMPARE(items[0].isOmarchyDefault, true);
        QCOMPARE(items[1].source, QString("pacman"));
        QCOMPARE(items[1].explicitInstall, false);
        QCOMPARE(items[2].source, QString("aur"));
        QCOMPARE(items[2].repository, QString("AUR"));
        QCOMPARE(items[3].isOrphan, true);

        // Updates output (checkupdates + yay -Qua)
        const auto updates = Backend::parseUpdatesOutput(
            "-> warning: ignored line\n"
            "chromium 131.0-1 -> 132.0-1\n"
            "yay-bin 12.4.2-1 -> 12.4.3-1 [3d2h]\n");
        QCOMPARE(updates.size(), 2);
        QCOMPARE(updates.value("chromium"), QString("132.0-1"));
        QCOMPARE(updates.value("yay-bin"), QString("12.4.3-1"));

        // Flatpak list & updates
        const auto fpItems = Backend::parseFlatpakList(
            QString::fromUtf8(
                "com.stremio.Stremio\tStremio\t4.4.168\t269,4\u00A0MB\tFreedom to Stream\tflathub\n"
                "org.onlyoffice.desktopeditors\tONLYOFFICE\t9.2.1\t1.3\u00A0GB\tOffice Suite\tflathub\n"));
        QCOMPARE(fpItems.size(), 2);
        QCOMPARE(fpItems[0].id, QString("flatpak:com.stremio.Stremio"));
        QCOMPARE(fpItems[0].sizeBytes, 269'400'000LL);
        QCOMPARE(fpItems[1].sizeBytes, 1'300'000'000LL);

        const auto fpUpdates = Backend::parseFlatpakUpdates(
            "com.stremio.Stremio\t4.4.169\n"
            "org.onlyoffice.desktopeditors\t\n");
        QCOMPARE(fpUpdates.size(), 2);
        QCOMPARE(fpUpdates.value("com.stremio.Stremio"), QString("4.4.169"));
        QVERIFY(fpUpdates.contains("org.onlyoffice.desktopeditors"));

        // Pacman -Qi details
        const auto details = Backend::parsePacmanDetails(
            "Name            : omarchy-shell\n"
            "Licenses        : MIT\n"
            "Architecture    : x86_64\n"
            "Depends On      : qt6-base  qt6-declarative\n"
            "                  hyprland\n"
            "Required By     : None\n"
            "Packager        : Omarchy Build\n");
        QCOMPARE(details.value("licenses").toString(), QString("MIT"));
        QCOMPARE(details.value("architecture").toString(), QString("x86_64"));
        QCOMPARE(details.value("dependsOn").toStringList(),
                 QStringList({"qt6-base", "qt6-declarative", "hyprland"}));
        QCOMPARE(details.value("requiredBy").toStringList(), QStringList());
    }

    void packageModelFilteringSortingAndStats() {
        PackageModel model;

        auto makePkg = [](QString source, QString name, qint64 bytes, bool explicitInstall, bool orphan, qint64 date) {
            PackageItem p;
            p.id = source + ":" + name;
            p.name = name;
            p.displayName = name;
            p.source = source;
            p.repository = (source == "aur") ? "AUR" : source;
            p.sizeBytes = bytes;
            p.explicitInstall = explicitInstall;
            p.isOrphan = orphan;
            p.installDate = date;
            return p;
        };

        model.setPackages({
            makePkg("omarchy", "omarchy-shell", 40'000'000, true, false, 100),
            makePkg("pacman", "firefox", 250'000'000, true, false, 200),
            makePkg("pacman", "glibc", 50'000'000, false, false, 50),
            makePkg("pacman", "orphan-lib", 15'000'000, false, true, 75),
            makePkg("aur", "yay-bin", 10'000'000, true, false, 300),
            makePkg("flatpak", "com.stremio.Stremio", 300'000'000, true, false, 250),
        });

        QCOMPARE(model.totalCount(), 6);
        // Default: explicit only (omarchy 40M + firefox 250M + yay-bin 10M + stremio 300M = 600M)
        QCOMPARE(model.showDependencies(), false);
        QCOMPARE(model.totalBytes(), 600'000'000LL);
        QCOMPARE(model.omarchyBytes(), 40'000'000LL);
        QCOMPARE(model.pacmanBytes(), 250'000'000LL);
        QCOMPARE(model.aurBytes(), 10'000'000LL);
        QCOMPARE(model.flatpakBytes(), 300'000'000LL);
        QCOMPARE(model.orphanCount(), 1);
        QCOMPARE(model.orphanBytes(), 15'000'000LL);

        // Sorted by size descending
        QCOMPARE(model.visibleCount(), 4);
        QCOMPARE(model.get(0).value("name").toString(), QString("com.stremio.Stremio"));
        QCOMPARE(model.get(1).value("name").toString(), QString("firefox"));

        // Orphans tab shows orphans even when showDependencies is false
        model.setFilterSource("orphans");
        QCOMPARE(model.visibleCount(), 1);
        QCOMPARE(model.get(0).value("name").toString(), QString("orphan-lib"));

        // Apply updates including a dependency package (glibc)
        model.applyUpdates({{"glibc", "2.41-1"}, {"firefox", "134.0-1"}},
                           {{"yay-bin", "12.5.0-1"}},
                           {{"com.stremio.Stremio", "4.4.169"}});
        QCOMPARE(model.updatesCount(), 4);

        // Updates tab shows ALL outdated packages (including glibc dependency)
        model.setFilterSource("updates");
        QCOMPARE(model.visibleCount(), 4);

        // Enabling showDependencies includes glibc + orphan-lib in scope totals
        model.setFilterSource("all");
        model.setShowDependencies(true);
        QCOMPARE(model.visibleCount(), 6);
        QCOMPARE(model.totalBytes(), 665'000'000LL);
        QCOMPARE(model.pacmanBytes(), 315'000'000LL);
        model.setSearchQuery("strem");
        QCOMPARE(model.visibleCount(), 1);
        QCOMPARE(model.get(0).value("source").toString(), QString("flatpak"));

        // Sort by name
        model.setSearchQuery("");
        model.setSortBy("name");
        QCOMPARE(model.get(0).value("name").toString(), QString("com.stremio.Stremio"));

        // Sort by date descending
        model.setSortBy("date");
        QCOMPARE(model.get(0).value("name").toString(), QString("yay-bin"));
    }

    void systemUpdateRouting() {
        // Omarchy and Pacman updates must always route through Omarchy's full update flow
        // rather than performing unsupported Arch partial upgrades (`pacman -Sy <pkg>`).
        QCOMPARE(Backend::planPackageUpdate("omarchy", false), PackageUpdateStrategy::OmarchyFullUpdate);
        QCOMPARE(Backend::planPackageUpdate("omarchy", true), PackageUpdateStrategy::OmarchyFullUpdate);
        QCOMPARE(Backend::planPackageUpdate("pacman", false), PackageUpdateStrategy::OmarchyFullUpdate);
        QCOMPARE(Backend::planPackageUpdate("pacman", true), PackageUpdateStrategy::OmarchyFullUpdate);

        // AUR packages route through the full update flow when system updates are waiting,
        // and update individually only once system packages are up to date.
        QCOMPARE(Backend::planPackageUpdate("aur", true), PackageUpdateStrategy::OmarchyFullUpdate);
        QCOMPARE(Backend::planPackageUpdate("aur", false), PackageUpdateStrategy::AurPackageUpdate);

        // Flatpak apps are sandboxed and can always update independently.
        QCOMPARE(Backend::planPackageUpdate("flatpak", true), PackageUpdateStrategy::FlatpakPackageUpdate);
        QCOMPARE(Backend::planPackageUpdate("flatpak", false), PackageUpdateStrategy::FlatpakPackageUpdate);

        // Launch resolution prefers Omarchy's floating presentation terminal wrapper.
        const auto primary = Backend::resolveOmarchyUpdateLaunch(
            "/usr/share/omarchy/bin/omarchy-launch-floating-terminal-with-presentation",
            "/usr/share/omarchy/bin/omarchy-update",
            "/usr/bin/xdg-terminal-exec");
        QCOMPARE(primary.program, QString("/usr/share/omarchy/bin/omarchy-launch-floating-terminal-with-presentation"));
        QCOMPARE(primary.arguments, QStringList({"omarchy-update"}));

        // Falls back to xdg-terminal-exec when the presentation wrapper is absent.
        const auto fallback = Backend::resolveOmarchyUpdateLaunch(
            "",
            "/usr/share/omarchy/bin/omarchy-update",
            "/usr/bin/xdg-terminal-exec");
        QCOMPARE(fallback.program, QString("/usr/bin/xdg-terminal-exec"));
        QCOMPARE(fallback.arguments, QStringList({"-e", "/usr/share/omarchy/bin/omarchy-update"}));

        // Refuses to construct a launch when omarchy-update is unavailable.
        const auto missing = Backend::resolveOmarchyUpdateLaunch(
            "/usr/share/omarchy/bin/omarchy-launch-floating-terminal-with-presentation",
            "",
            "/usr/bin/xdg-terminal-exec");
        QVERIFY(missing.program.isEmpty());
    }
};

QTEST_MAIN(OmapkgTests)
#include "omapkg_tests.moc"
