#include <QtTest>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include "backend.h"
#include "packagemodel.h"
#include "security.h"
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

        // du -sb parser and git URL / ls-remote helpers
        const auto duSizes = Backend::parseDuSizes(
            "1261369\t/tmp/plugins/demo.plugin/\n"
            "1746720127\t/tmp/mise/installs/node\n");
        QCOMPARE(duSizes.value("/tmp/plugins/demo.plugin"), 1261369LL);
        QCOMPARE(duSizes.value("/tmp/mise/installs/node"), 1746720127LL);
        QCOMPARE(Backend::normalizeGitRemoteUrl("git@github.com:marciobbj/omapkg.git"),
                 QString("https://github.com/marciobbj/omapkg"));
        QCOMPARE(Backend::normalizeGitRemoteUrl("https://github.com/omacom/demo.git"),
                 QString("https://github.com/omacom/demo"));
        QCOMPARE(Backend::parsePluginRemoteHead("0123456789abcdef0123456789abcdef01234567\tHEAD\n"),
                 QString("0123456789abcdef0123456789abcdef01234567"));

        // Omarchy plugins parser (skips reserved omarchy.* namespace and reads manifest.json)
        QTemporaryDir pluginsTmp;
        QVERIFY(pluginsTmp.isValid());
        const QString pluginDir = pluginsTmp.filePath("acme.weather");
        QVERIFY(QDir().mkpath(pluginDir));
        {
            QFile f(pluginDir + "/manifest.json");
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write(
                "{\n"
                "  \"schemaVersion\": 1,\n"
                "  \"id\": \"acme.weather\",\n"
                "  \"name\": \"Acme Weather\",\n"
                "  \"version\": \"1.2.3\",\n"
                "  \"author\": \"Acme\",\n"
                "  \"license\": \"MIT\",\n"
                "  \"homepage\": \"https://github.com/acme/weather\",\n"
                "  \"description\": \"Weather bar widget\",\n"
                "  \"kinds\": [\"bar-widget\"]\n"
                "}\n");
        }
        const QString reservedDir = pluginsTmp.filePath("omarchy.reserved");
        QVERIFY(QDir().mkpath(reservedDir));
        {
            QFile f(reservedDir + "/manifest.json");
            QVERIFY(f.open(QIODevice::WriteOnly | QIODevice::Text));
            f.write("{\"schemaVersion\":1,\"id\":\"omarchy.reserved\",\"name\":\"Reserved\",\"version\":\"1.0\"}");
        }
        QHash<QString, PluginMeta> pluginMeta;
        const QString pluginListJson =
            "[\n"
            "  {\"id\":\"acme.weather\",\"enabled\":true,\"firstParty\":false},\n"
            "  {\"id\":\"omarchy.bar\",\"enabled\":true,\"firstParty\":true}\n"
            "]\n";
        const auto pluginItems = Backend::parseOmarchyPlugins(
            pluginsTmp.path(), pluginListJson, {{QFileInfo(pluginDir).canonicalFilePath(), 4096}}, &pluginMeta);
        QCOMPARE(pluginItems.size(), 1);
        QCOMPARE(pluginItems[0].id, QString("omarchy-plugin:acme.weather"));
        QCOMPARE(pluginItems[0].source, QString("omarchy-plugin"));
        QCOMPARE(pluginItems[0].displayName, QString("Acme Weather"));
        QCOMPARE(pluginItems[0].version, QString("1.2.3"));
        QCOMPARE(pluginItems[0].repository, QString("bar-widget"));
        QCOMPARE(pluginItems[0].sizeBytes, 4096LL);
        QCOMPARE(pluginMeta.value("acme.weather").enabled, true);
        QCOMPARE(pluginMeta.value("acme.weather").author, QString("Acme"));

        // Mise installed & outdated parsers
        const QString miseJson =
            "{\n"
            "  \"node\": [\n"
            "    {\"version\": \"25.9.0\", \"install_path\": \"/tmp/mise/installs/node/25.9.0\", \"installed\": true, \"active\": false},\n"
            "    {\"version\": \"26.13.1\", \"requested_version\": \"latest\", \"install_path\": \"/tmp/mise/installs/node/26.13.1\",\n"
            "     \"source\": {\"type\": \"mise.toml\", \"path\": \"/home/test/.config/mise/config.toml\"},\n"
            "     \"installed\": true, \"active\": true}\n"
            "  ],\n"
            "  \"uninstalled-tool\": [\n"
            "    {\"version\": \"1.0.0\", \"installed\": false, \"active\": false}\n"
            "  ]\n"
            "}\n";
        QHash<QString, MiseToolMeta> miseMeta;
        const auto miseItems = Backend::parseMiseInstalled(
            miseJson, {{"/tmp/mise/installs/node", 1746720127LL}}, &miseMeta);
        QCOMPARE(miseItems.size(), 1);
        QCOMPARE(miseItems[0].id, QString("mise:node"));
        QCOMPARE(miseItems[0].source, QString("mise"));
        QCOMPARE(miseItems[0].version, QString("26.13.1"));
        QCOMPARE(miseItems[0].repository, QString("latest"));
        QCOMPARE(miseItems[0].sizeBytes, 1746720127LL);
        QCOMPARE(miseMeta.value("node").installedVersions, QStringList({"25.9.0", "26.13.1"}));
        QCOMPARE(miseMeta.value("node").configPath, QString("/home/test/.config/mise/config.toml"));

        const auto miseUpdates = Backend::parseMiseOutdated(
            "{\n"
            "  \"node\": {\"name\": \"node\", \"requested\": \"latest\", \"current\": \"26.13.1\", \"latest\": \"26.14.0\"},\n"
            "  \"codex\": {\"name\": \"codex\", \"requested\": \"latest\", \"current\": \"0.171.0\", \"latest\": \"0.171.0\"}\n"
            "}\n");
        QCOMPARE(miseUpdates.size(), 1);
        QCOMPARE(miseUpdates.value("node"), QString("26.14.0"));
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
            makePkg("omarchy-plugin", "acme.weather", 2'000'000, true, false, 275),
            makePkg("mise", "node", 100'000'000, true, false, 290),
        });

        QCOMPARE(model.totalCount(), 8);
        // Default: explicit only (omarchy 40M + firefox 250M + yay-bin 10M + stremio 300M + plugin 2M + mise 100M = 702M)
        QCOMPARE(model.showDependencies(), false);
        QCOMPARE(model.totalBytes(), 702'000'000LL);
        QCOMPARE(model.omarchyBytes(), 40'000'000LL);
        QCOMPARE(model.pacmanBytes(), 250'000'000LL);
        QCOMPARE(model.aurBytes(), 10'000'000LL);
        QCOMPARE(model.flatpakBytes(), 300'000'000LL);
        QCOMPARE(model.pluginBytes(), 2'000'000LL);
        QCOMPARE(model.miseBytes(), 100'000'000LL);
        QCOMPARE(model.pluginCount(), 1);
        QCOMPARE(model.miseCount(), 1);
        QCOMPARE(model.orphanCount(), 1);
        QCOMPARE(model.orphanBytes(), 15'000'000LL);

        // Sorted by size descending
        QCOMPARE(model.visibleCount(), 6);
        QCOMPARE(model.get(0).value("name").toString(), QString("com.stremio.Stremio"));
        QCOMPARE(model.get(1).value("name").toString(), QString("firefox"));

        // Orphans tab shows orphans even when showDependencies is false
        model.setFilterSource("orphans");
        QCOMPARE(model.visibleCount(), 1);
        QCOMPARE(model.get(0).value("name").toString(), QString("orphan-lib"));

        // Apply updates including a dependency package (glibc), an Omarchy plugin, and a Mise package
        model.applyUpdates({{"glibc", "2.41-1"}, {"firefox", "134.0-1"}},
                           {{"yay-bin", "12.5.0-1"}},
                           {{"com.stremio.Stremio", "4.4.169"}},
                           {{"acme.weather", "a1b2c3d"}},
                           {{"node", "26.14.0"}});
        QCOMPARE(model.updatesCount(), 6);

        // Updates tab shows ALL outdated packages (including glibc dependency, plugins, and mise)
        model.setFilterSource("updates");
        QCOMPARE(model.visibleCount(), 6);

        // Enabling showDependencies includes glibc + orphan-lib in scope totals
        model.setFilterSource("all");
        model.setShowDependencies(true);
        QCOMPARE(model.visibleCount(), 8);
        QCOMPARE(model.totalBytes(), 767'000'000LL);
        QCOMPARE(model.pacmanBytes(), 315'000'000LL);
        model.setSearchQuery("strem");
        QCOMPARE(model.visibleCount(), 1);
        QCOMPARE(model.get(0).value("source").toString(), QString("flatpak"));

        // Filter Omarchy Plugins and Mise tabs
        model.setSearchQuery("");
        model.setFilterSource("omarchy-plugin");
        QCOMPARE(model.visibleCount(), 1);
        QCOMPARE(model.get(0).value("name").toString(), QString("acme.weather"));
        model.setFilterSource("mise");
        QCOMPARE(model.visibleCount(), 1);
        QCOMPARE(model.get(0).value("name").toString(), QString("node"));
        model.setFilterSource("all");

        // Sort by name
        model.setSortBy("name");
        QCOMPARE(model.get(0).value("name").toString(), QString("acme.weather"));

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

        // Flatpak apps, Omarchy plugins, and Mise packages can update independently.
        QCOMPARE(Backend::planPackageUpdate("flatpak", true), PackageUpdateStrategy::FlatpakPackageUpdate);
        QCOMPARE(Backend::planPackageUpdate("flatpak", false), PackageUpdateStrategy::FlatpakPackageUpdate);
        QCOMPARE(Backend::planPackageUpdate("omarchy-plugin", true), PackageUpdateStrategy::PluginPackageUpdate);
        QCOMPARE(Backend::planPackageUpdate("omarchy-plugin", false), PackageUpdateStrategy::PluginPackageUpdate);
        QCOMPARE(Backend::planPackageUpdate("mise", true), PackageUpdateStrategy::MisePackageUpdate);
        QCOMPARE(Backend::planPackageUpdate("mise", false), PackageUpdateStrategy::MisePackageUpdate);

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

        // Omarchy plugin updates launch interactively (without --yes) so users review git diffs.
        const auto pluginSingle = Backend::resolvePluginUpdateLaunch(
            "/usr/share/omarchy/bin/omarchy-launch-floating-terminal-with-presentation",
            "/usr/share/omarchy/bin/omarchy-plugin-update",
            "/usr/bin/xdg-terminal-exec",
            "harshith.system-monitor");
        QCOMPARE(pluginSingle.program, QString("/usr/share/omarchy/bin/omarchy-launch-floating-terminal-with-presentation"));
        QCOMPARE(pluginSingle.arguments, QStringList({"omarchy-plugin-update", "harshith.system-monitor"}));

        const auto pluginAll = Backend::resolvePluginUpdateLaunch(
            "/usr/share/omarchy/bin/omarchy-launch-floating-terminal-with-presentation",
            "/usr/share/omarchy/bin/omarchy-plugin-update",
            "/usr/bin/xdg-terminal-exec");
        QCOMPARE(pluginAll.arguments, QStringList({"omarchy-plugin-update"}));

        // Rejects unsafe plugin IDs containing shell metacharacters or path traversal.
        const auto pluginUnsafe = Backend::resolvePluginUpdateLaunch(
            "/usr/share/omarchy/bin/omarchy-launch-floating-terminal-with-presentation",
            "/usr/share/omarchy/bin/omarchy-plugin-update",
            "/usr/bin/xdg-terminal-exec",
            "bad;rm -rf /");
        QVERIFY(pluginUnsafe.program.isEmpty());
    }

    void securityAuditMatchingAndNotifications() {
        // Arch version comparisons (epochs, pkgrel, numeric vs alpha)
        QVERIFY(Security::compareArchVersions("1.7.0-2", "1.7.0-3") < 0);
        QVERIFY(Security::compareArchVersions("1.7.3-1", "1.7.0-2") > 0);
        QVERIFY(Security::compareArchVersions("1:1.0-1", "9.0-1") > 0);
        QVERIFY(Security::compareArchVersions("2.14.4-1", "2.14.4-1") == 0);
        QVERIFY(Security::compareArchVersions("1.0rc1-1", "1.0-1") < 0);
        QVERIFY(Security::compareArchVersions("1.1.1.o-1", "3.6.5-1") < 0);

        PackageItem pamOld;
        pamOld.id = "pacman:pam";
        pamOld.name = "pam";
        pamOld.displayName = "pam";
        pamOld.source = "pacman";
        pamOld.version = "1.7.0-1";

        PackageItem opensslNew;
        opensslNew.id = "pacman:openssl";
        opensslNew.name = "openssl";
        opensslNew.displayName = "openssl";
        opensslNew.source = "pacman";
        opensslNew.version = "3.6.5-1";

        PackageItem vimOutdated;
        vimOutdated.id = "pacman:vim";
        vimOutdated.name = "vim";
        vimOutdated.displayName = "vim";
        vimOutdated.source = "pacman";
        vimOutdated.version = "9.0.1224-1";

        PackageItem retiredPlugin;
        retiredPlugin.id = "omarchy-plugin:bad.plugin";
        retiredPlugin.name = "bad.plugin";
        retiredPlugin.displayName = "Bad Plugin";
        retiredPlugin.source = "omarchy-plugin";
        retiredPlugin.version = "1.0.0";

        PackageItem flaggedPlugin;
        flaggedPlugin.id = "omarchy-plugin:risky.plugin";
        flaggedPlugin.name = "risky.plugin";
        flaggedPlugin.displayName = "Risky Plugin";
        flaggedPlugin.source = "omarchy-plugin";
        flaggedPlugin.version = "1.0.0";

        PackageItem miseCodex;
        miseCodex.id = "mise:codex";
        miseCodex.name = "codex";
        miseCodex.displayName = "codex";
        miseCodex.source = "mise";
        miseCodex.version = "0.1.0";

        const QVector<PackageItem> pkgs{pamOld, opensslNew, vimOutdated, retiredPlugin, flaggedPlugin, miseCodex};

        // 1. Arch Security Tracker matching:
        // - pam 1.7.0-1 <= affected 1.7.0-2 (unfixed Vulnerable) -> MATCH
        // - openssl 3.6.5-1 > affected 1.1.1.o-1 (stale unfixed) -> NO false positive
        // - vim 9.0.1224-1 < fixed 9.0.1225-1 -> MATCH
        const QByteArray archJson =
            "[\n"
            "  {\"name\":\"AVG-2901\",\"packages\":[\"pam\"],\"status\":\"Vulnerable\",\"severity\":\"High\",\"type\":\"arbitrary filesystem access\",\"affected\":\"1.7.0-2\",\"fixed\":null,\"issues\":[\"CVE-2025-6020\"]},\n"
            "  {\"name\":\"AVG-2765\",\"packages\":[\"openssl\"],\"status\":\"Vulnerable\",\"severity\":\"Medium\",\"type\":\"command injection\",\"affected\":\"1.1.1.o-1\",\"fixed\":null,\"issues\":[\"CVE-2022-2068\"]},\n"
            "  {\"name\":\"AVG-2843\",\"packages\":[\"vim\"],\"status\":\"Fixed\",\"severity\":\"Critical\",\"type\":\"heap buffer overflow\",\"affected\":\"9.0.1224-1\",\"fixed\":\"9.0.1225-1\",\"issues\":[\"CVE-2023-0433\"]}\n"
            "]\n";
        auto advisories = Security::matchArchAdvisories(archJson, pkgs);
        QVERIFY(advisories.contains("pacman:pam"));
        QVERIFY(!advisories.contains("pacman:openssl"));
        QVERIFY(advisories.contains("pacman:vim"));
        QCOMPARE(advisories.value("pacman:vim").first().severity, QString("critical"));
        QCOMPARE(advisories.value("pacman:vim").first().fixedVersion, QString("9.0.1225-1"));

        // 2. Omarchy Plugin Marketplace registry matching (retired + needs-fixes baseline)
        const QByteArray regJson =
            "{\n"
            "  \"retiredPluginIds\": [\"bad.plugin\"],\n"
            "  \"sources\": [\n"
            "    {\n"
            "      \"repo\": \"https://github.com/example/risky\",\n"
            "      \"catalog\": {\"id\": \"risky.plugin\"},\n"
            "      \"automatedSecurityBaseline\": {\n"
            "        \"pluginIds\": [\"risky.plugin\"],\n"
            "        \"outcome\": \"needs-fixes\",\n"
            "        \"findings\": [\"curl-pipe-shell\"]\n"
            "      }\n"
            "    }\n"
            "  ]\n"
            "}\n";
        Security::mergeAdvisories(advisories, Security::matchMarketplacePlugins(regJson, pkgs, {}));
        QVERIFY(advisories.contains("omarchy-plugin:bad.plugin"));
        QCOMPARE(advisories.value("omarchy-plugin:bad.plugin").first().severity, QString("low"));
        QVERIFY(advisories.contains("omarchy-plugin:risky.plugin"));
        QCOMPARE(advisories.value("omarchy-plugin:risky.plugin").first().severity, QString("critical"));

        // 3. OSV.dev target resolution, batch parsing, and detail enrichment
        const auto osvTarget = Security::resolveMiseOsvTarget(miseCodex, "aqua:openai/codex npm:@openai/codex");
        QCOMPARE(osvTarget.ecosystem, QString("npm"));
        QCOMPARE(osvTarget.packageName, QString("@openai/codex"));
        const QByteArray osvResp =
            "{\"results\":[{\"vulns\":[{\"id\":\"GHSA-xrxf-jgv3-qmrm\",\"modified\":\"2026-04-16T23:11:28Z\"}]}]}";
        auto osvAdvisories = Security::parseOsvBatchResponse(osvResp, {osvTarget});
        QVERIFY(osvAdvisories.contains("mise:codex"));
        Security::enrichOsvAdvisory(
            osvAdvisories["mise:codex"][0],
            "{\"id\":\"GHSA-xrxf-jgv3-qmrm\",\"aliases\":[\"CVE-2026-1234\"],\"summary\":\"Command injection in CLI\",\"database_specific\":{\"severity\":\"HIGH\"},\"affected\":[{\"ranges\":[{\"events\":[{\"introduced\":\"0\"},{\"fixed\":\"0.171.0\"}]}]}]}");
        QCOMPARE(osvAdvisories.value("mise:codex").first().cves, QStringList({"CVE-2026-1234"}));
        QCOMPARE(osvAdvisories.value("mise:codex").first().severity, QString("high"));
        QCOMPARE(osvAdvisories.value("mise:codex").first().summary, QString("Command injection in CLI"));
        QCOMPARE(osvAdvisories.value("mise:codex").first().fixedVersion, QString("0.171.0"));
        Security::mergeAdvisories(advisories, osvAdvisories);

        // 4. PackageModel security filtering & severity sorting
        PackageModel model;
        model.setPackages(pkgs);
        model.applySecurityAdvisories(advisories);
        QCOMPARE(model.securityAlertCount(), 5);
        QCOMPARE(model.highestSecuritySeverity(), QString("critical"));
        model.setFilterSource("security");
        QCOMPARE(model.visibleCount(), 5);
        QCOMPARE(model.get(0).value("securitySeverity").toString(), QString("critical"));

        // 5. Notification deduplication:
        // - notifies once for medium/high/critical packages (4 of the 5; "low" bad.plugin is silent)
        // - 0 on repeat
        QTemporaryDir stateTmp;
        QVERIFY(stateTmp.isValid());
        const QString stateFile = stateTmp.filePath("notified.json");
        QCOMPARE(Security::notifyNewAdvisories(pkgs, advisories, stateFile, {}, false), 4);
        QCOMPARE(Security::notifyNewAdvisories(pkgs, advisories, stateFile, {}, false), 0);

        // 6. URL sanitization, mise registry parsing, and keyed cache
        QCOMPARE(Security::sanitizeHttpUrl("https://security.archlinux.org/AVG-2901"),
                 QString("https://security.archlinux.org/AVG-2901"));
        QVERIFY(Security::sanitizeHttpUrl("file:///etc/passwd").isEmpty());
        QVERIFY(Security::sanitizeHttpUrl("javascript:alert(1)").isEmpty());

        const auto regMap = Security::parseMiseRegistry(
            "codex                         aqua:openai/codex npm:@openai/codex\n"
            "go                            core:go\n");
        QCOMPARE(regMap.value("codex"), QString("aqua:openai/codex npm:@openai/codex"));
        QCOMPARE(regMap.value("go"), QString("core:go"));

        const QString keyedFile = stateTmp.filePath("osv-mise.json");
        QVERIFY(Security::writeKeyedCache(keyedFile, "hash-a", "{\"results\":[]}"));
        QByteArray cachedOut;
        QVERIFY(Security::readFreshKeyedCache(keyedFile, "hash-a", 3600, &cachedOut));
        QCOMPARE(cachedOut, QByteArray("{\"results\":[]}"));
        QVERIFY(!Security::readFreshKeyedCache(keyedFile, "hash-b", 3600, &cachedOut));
    }
};

QTEST_MAIN(OmapkgTests)
#include "omapkg_tests.moc"
