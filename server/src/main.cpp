// SPDX-FileCopyrightText: Copyright 2019-2026 rpcsn Project
// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QLoggingCategory>
#ifndef SHADNET_NO_HTTP
#include "bloodborne_backend_server.h"
#endif
#include "config.h"
#include "server.h"
#include "watchdog.h"
#ifndef SHADNET_NO_HTTP
#include "webapi_server.h"
#endif

const QString versionString = QStringLiteral("0.0.13");

int main(int argc, char* argv[]) {
    QLoggingCategory::setFilterRules(QStringLiteral("*.debug=false\n*.info=true\n*.warning=true"));
    qSetMessagePattern(QStringLiteral("%{time yyyy-MM-dd HH:mm:ss.zzz}  "
                                      "%{if-debug}DEBUG%{endif}"
                                      "%{if-info} INFO%{endif}"
                                      "%{if-warning} WARN%{endif}"
                                      "%{if-critical} CRIT%{endif}"
                                      "%{if-fatal}FATAL%{endif}"
                                      "  %{if-category}[%{category}] %{endif}%{message}"));

    QCoreApplication app(argc, argv);
    app.setApplicationName("shadnet");

    // Set working directory to executable location (SHADNET_HOME: another folder, for tests)
    const QString home = qEnvironmentVariable("SHADNET_HOME");
    QDir::setCurrent(home.isEmpty() ? QCoreApplication::applicationDirPath() : home);

    // Phase 11: `--watchdog` keeps shadNet running as its child (watchdog.h); the child beats.
    QStringList arguments = app.arguments().mid(1);
    if (arguments.removeAll(QStringLiteral("--watchdog")) > 0) {
        return RunWatchdog(QCoreApplication::applicationFilePath(),
                           arguments << QStringLiteral("--beat"));
    }
    if (arguments.contains(QStringLiteral("--beat"))) {
        StartWatchdogBeat(&app);
    }

    qInfo() << "ShadNet Qt server version" << versionString;

    ConfigManager config;
    config.Load();
    config.LoadBannedDomains();

    ShadNetServer server;
    if (!server.Start(&config)) {
        qCritical() << "Failed to start server";
        return 1;
    }

#ifndef SHADNET_NO_HTTP
    // Start the HTTP/JSON WebAPI listener alongside the binary protocol.
    WebApiServer webapi;
    if (!webapi.Start(&config, "db/shadnet.db", &server.Shared())) {
        qWarning() << "WebApiServer failed to start; continuing without WebAPI";
    }

    // The game's own online services (on unless BloodborneBackend=false). When it cannot start, the
    // kits' Python backend can take its port instead.
    BloodborneBackendServer backend;
    if (config.GetBackend().enabled && !backend.Start(&config, &server.Shared().httpGate)) {
        qWarning() << "Backend failed to start; continuing without it";
    }
#else
    qWarning() << "Built without the HTTP services (Qt older than 6.8)";
#endif

    return app.exec();
}
