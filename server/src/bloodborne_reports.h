// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Phase 11: crash dumps and logs the players' launchers send the server on their own after a crash,
// kept per player: reports/<player id>/<when>-<file>. Only dumps and text, under 64 MB each, and a
// player's newest 50.
#pragma once

#include <optional>
#include <QByteArray>
#include <QDateTime>
#include <QString>

class ReportStore {
public:
    static constexpr qint64 MaxBytes = 64ll * 1024 * 1024;
    static constexpr int KeptPerPlayer = 50;

    explicit ReportStore(QString folder) : folder{std::move(folder)} {}

    // The file's path once kept, nothing when the player, its name, kind or size is refused.
    std::optional<QString> Save(const QString& player, const QString& file_name,
                                const QByteArray& bytes,
                                const QDateTime& now = QDateTime::currentDateTimeUtc());

private:
    QString folder;
};

class Database;
class QHttpServer;
// POST /bb/report/<file>, the body the file, X-Player and X-Secret its player id: kept in
// `folder` (ReportStore).
void RegisterReportRoutes(QHttpServer& http, Database& db, const QString& folder);
