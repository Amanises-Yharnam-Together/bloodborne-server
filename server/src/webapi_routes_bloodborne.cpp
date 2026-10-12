// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "webapi_routes_bloodborne.h"

#include <cstdlib>
#include <memory>
#include <optional>

#include <QDateTime>
#include <QDebug>
#include <QHttpHeaders>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QList>
#include <QMutex>
#include <QMutexLocker>
#include <QString>

#include "bloodborne_party_rest.h"
#include "bloodborne_summon_broker.h"

namespace WebApiRoutes {
namespace {

constexpr auto HostPlacementHeader = "X-ShadPS4-Bloodborne-Host-Placement";
constexpr auto PartyHeader = "X-ShadPS4-Bloodborne-Party";

QByteArray Party(const QHttpServerRequest& request) {
    return Bloodborne::NormalizePartyKey(request.value(PartyHeader));
}

QHttpServerResponse SummonResponse(const QString& messageId, const QByteArray& hostPlacement = {}) {
    QJsonObject body;
    body.insert(QStringLiteral("ResKind"), 0);
    body.insert(QStringLiteral("MessageId"), messageId);
    QHttpServerResponse response{"application/json",
                                 QJsonDocument(body).toJson(QJsonDocument::Compact),
                                 QHttpServerResponse::StatusCode::Ok};
    if (!hostPlacement.isEmpty()) {
        QHttpHeaders headers = response.headers();
        if (headers.append(HostPlacementHeader, hostPlacement)) {
            response.setHeaders(std::move(headers));
        }
    }
    return response;
}

QHttpServerResponse SummonListResponse(const QList<QByteArray>& signs) {
    QByteArray body = "{\"SummonDataList\":[";
    for (qsizetype index = 0; index < signs.size(); ++index) {
        if (index != 0) {
            body.append(',');
        }
        body.append(signs[index]);
    }
    body.append("],\"ResKind\":0,\"MessageId\":\"SummonDataGetListResponse\"}");
    return QHttpServerResponse{"application/json", body, QHttpServerResponse::StatusCode::Ok};
}

QHttpServerResponse RawJsonResponse(const QByteArray& body, const QByteArray& hostPlacement = {}) {
    QHttpServerResponse response{"application/json", body, QHttpServerResponse::StatusCode::Ok};
    if (!hostPlacement.isEmpty()) {
        QHttpHeaders headers = response.headers();
        if (headers.append(HostPlacementHeader, hostPlacement)) {
            response.setHeaders(std::move(headers));
        }
    }
    return response;
}

QHttpServerResponse InvalidRequest(const QString& reason) {
    QJsonObject body;
    body.insert(QStringLiteral("ResKind"), 1);
    body.insert(QStringLiteral("MessageId"), QString());
    body.insert(QStringLiteral("Error"), reason);
    return QHttpServerResponse{"application/json",
                               QJsonDocument(body).toJson(QJsonDocument::Compact),
                               QHttpServerResponse::StatusCode::BadRequest};
}

std::optional<QJsonObject> ParseRequest(const QHttpServerRequest& request) {
    QJsonParseError error{};
    const QJsonDocument document = QJsonDocument::fromJson(request.body(), &error);
    if (error.error != QJsonParseError::NoError || !document.isObject()) {
        return std::nullopt;
    }
    return document.object();
}

qint64 Integer(const QJsonObject& object, const QString& key, qint64 fallback = 0) {
    const QJsonValue value = object.value(key);
    return value.isDouble() ? value.toVariant().toLongLong() : fallback;
}

bool EnvEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && QString::fromUtf8(value).trimmed() != "0";
}

void TraceSummonRequest(const char* route, const QHttpServerRequest& request) {
    if (!EnvEnabled("SHADNET_BLOODBORNE_RE_TRACE")) {
        return;
    }
    qInfo().noquote() << "Bloodborne summon RE" << route << request.body();
}

} // namespace

void RegisterBloodborneRoutes(QHttpServer& http, bool seamlessCoop) {
    seamlessCoop = seamlessCoop || EnvEnabled("SHADNET_BLOODBORNE_SEAMLESS_COOP");
    Bloodborne::SummonBroker::Options options;
    options.seamlessCoop = seamlessCoop;
    auto broker = std::make_shared<Bloodborne::SummonBroker>(options);

    qInfo() << "Bloodborne summon routes registered; seamless co-op"
            << (broker->IsSeamlessCoopEnabled() ? "enabled" : "disabled") << "anywhere summons"
            << (broker->IsSeamlessAnywhereSummonsEnabled() ? "enabled" : "disabled");

    http.route("/summon_messenger/create", QHttpServerRequest::Method::Post,
               [broker](const QHttpServerRequest& request) -> QHttpServerResponse {
                   TraceSummonRequest("create", request);
                   const auto body = ParseRequest(request);
                   if (!body || !Bloodborne::HasRequiredAdvertisementFields(*body)) {
                       return InvalidRequest(QStringLiteral("Invalid summon advertisement"));
                   }
                   const auto result = broker->Advertise(
                       *body, request.body(), QDateTime::currentMSecsSinceEpoch(), Party(request));
                   if (!result.pendingClaim.isEmpty()) {
                       const QByteArray response =
                           Bloodborne::BuildClaimDeliveryResponse(result.pendingClaim);
                       if (response.isEmpty()) {
                           return InvalidRequest(QStringLiteral("Invalid pending summon claim"));
                       }
                       qInfo() << "Bloodborne summon: delivered claim to user"
                               << Integer(*body, QStringLiteral("UserId")) << "session"
                               << body->value(QStringLiteral("SessionId")).toString()
                               << "host-placement-bytes" << result.pendingHostPlacement.size();
                       return RawJsonResponse(response, result.pendingHostPlacement);
                   }
                   if (!result.pendingHostPlacement.isEmpty()) {
                       qInfo() << "Bloodborne summon: preparing cross-map user"
                               << Integer(*body, QStringLiteral("UserId")) << "session"
                               << body->value(QStringLiteral("SessionId")).toString()
                               << "destination placement bytes"
                               << result.pendingHostPlacement.size();
                       return SummonResponse(QStringLiteral("SummonDataCreateResponse"),
                                             result.pendingHostPlacement);
                   }
                   qInfo() << "Bloodborne summon: advertised user"
                           << Integer(*body, QStringLiteral("UserId")) << "session"
                           << body->value(QStringLiteral("SessionId")).toString();
                   return SummonResponse(QStringLiteral("SummonDataCreateResponse"));
               });

    http.route("/summon_messenger/get", QHttpServerRequest::Method::Post,
               [broker](const QHttpServerRequest& request) -> QHttpServerResponse {
                   TraceSummonRequest("get", request);
                   const auto body = ParseRequest(request);
                   if (!body) {
                       return InvalidRequest(QStringLiteral("Invalid summon search"));
                   }
                   const QList<QByteArray> signs =
                       broker->Search(*body, QDateTime::currentMSecsSinceEpoch(),
                                      request.value(HostPlacementHeader), Party(request));
                   qInfo() << "Bloodborne summon: search for user"
                           << Integer(*body, QStringLiteral("UserId")) << "returned"
                           << signs.size();
                   return SummonListResponse(signs);
               });

    http.route("/summon_messenger/delete", QHttpServerRequest::Method::Post,
               [broker](const QHttpServerRequest& request) -> QHttpServerResponse {
                   TraceSummonRequest("delete", request);
                   const auto body = ParseRequest(request);
                   if (!body) {
                       return InvalidRequest(QStringLiteral("Invalid summon removal"));
                   }
                   const auto result = broker->Consume(*body, QDateTime::currentMSecsSinceEpoch());
                   qInfo() << "Bloodborne summon: consumed" << result.consumed << "retained"
                           << result.retained << "advertisement(s) host-placement-bytes"
                           << result.pendingHostPlacement.size();
                   QHttpServerResponse response =
                       SummonResponse(QStringLiteral("SummonDataRemoveResponse"));
                   if (result.pendingHostPlacement.isEmpty()) {
                       return response;
                   }
                   QHttpHeaders headers = response.headers();
                   if (headers.append(HostPlacementHeader, result.pendingHostPlacement)) {
                       response.setHeaders(std::move(headers));
                   }
                   return response;
               });

    http.route("/summon_messenger/request", QHttpServerRequest::Method::Post,
               [broker](const QHttpServerRequest& request) -> QHttpServerResponse {
                   TraceSummonRequest("request", request);
                   const auto body = ParseRequest(request);
                   if (!body) {
                       return InvalidRequest(QStringLiteral("Invalid summon request"));
                   }
                   const auto result =
                       broker->Claim(*body, request.body(), QDateTime::currentMSecsSinceEpoch(),
                                     request.value(HostPlacementHeader), Party(request));
                   switch (result.status) {
                   case Bloodborne::SummonBroker::ClaimStatus::Claimed:
                       qInfo() << "Bloodborne summon: claimed session" << result.targetSessionId
                               << "user" << result.targetUserId;
                       break;
                   case Bloodborne::SummonBroker::ClaimStatus::AlreadyClaimed:
                       qInfo() << "Bloodborne summon: repeated claim for session"
                               << result.targetSessionId;
                       break;
                   case Bloodborne::SummonBroker::ClaimStatus::NotFound:
                       qWarning() << "Bloodborne summon: claim target not found";
                       break;
                   case Bloodborne::SummonBroker::ClaimStatus::Conflict:
                       qWarning() << "Bloodborne summon: conflicting claim for session"
                                  << result.targetSessionId;
                       break;
                   }
                   return SummonResponse(QStringLiteral("SummonDataSummonResponse"));
               });

    // Lamps as campfires: party members post where they rest and read back who else does.
    // Called by the emulator itself (not the game) about once a second while a member rests.
    auto restBoard = std::make_shared<Bloodborne::PartyRestBoard>();
    auto lastBoards = std::make_shared<QHash<QByteArray, QByteArray>>();
    auto lastBoardsMutex = std::make_shared<QMutex>();
    http.route(
        "/bloodborne/party/rest", QHttpServerRequest::Method::Post,
        [restBoard, lastBoards,
         lastBoardsMutex](const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto update = Bloodborne::ParseRestUpdate(request.body());
            if (!update) {
                return InvalidRequest(QStringLiteral("Invalid party rest update"));
            }
            const QByteArray party = Party(request);
            const QByteArray board = Bloodborne::FormatRestBoard(
                restBoard->Update(party, update->member, update->lamp,
                                  QDateTime::currentMSecsSinceEpoch(), update->at));
            {
                QMutexLocker lock(lastBoardsMutex.get());
                QByteArray& last = (*lastBoards)[party];
                if (last != board) {
                    last = board;
                    qInfo().noquote() << "Bloodborne party rest board:"
                                      << (board.isEmpty() ? QByteArray("nobody resting")
                                                          : board.trimmed().replace('\n', ", "));
                }
            }
            return QHttpServerResponse{"text/plain", board, QHttpServerResponse::StatusCode::Ok};
        });

    // Seamless travel: party members post the map they are on and read back everyone's.
    // Called by the emulator about once a second while in a party session.
    auto mapBoard = std::make_shared<Bloodborne::PartyMapBoard>();
    http.route(
        "/bloodborne/party/map", QHttpServerRequest::Method::Post,
        [mapBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto update = Bloodborne::ParseMapUpdate(request.body());
            if (!update) {
                return InvalidRequest(QStringLiteral("Invalid party map update"));
            }
            const QByteArray board = Bloodborne::FormatMapBoard(mapBoard->Update(
                Party(request), update->member, update->map, QDateTime::currentMSecsSinceEpoch()));
            return QHttpServerResponse{"text/plain", board, QHttpServerResponse::StatusCode::Ok};
        });

    // Shared trophies: party members post what they unlocked in the party and read back
    // everything the party unlocked. Called by the emulator every few seconds in a session.
    auto trophyBoard = std::make_shared<Bloodborne::PartyTrophyBoard>();
    http.route("/bloodborne/party/trophy", QHttpServerRequest::Method::Post,
               [trophyBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
                   const auto update = Bloodborne::ParseTrophyUpdate(request.body());
                   if (!update) {
                       return InvalidRequest(QStringLiteral("Invalid party trophy update"));
                   }
                   if (!update->trophies.isEmpty()) {
                       qInfo().noquote() << "Bloodborne party trophies from" << update->member
                                         << ":" << update->trophies;
                   }
                   const QByteArray board = Bloodborne::FormatTrophyBoard(
                       trophyBoard->Update(Party(request), update->member, update->trophies));
                   return QHttpServerResponse{"text/plain", board,
                                              QHttpServerResponse::StatusCode::Ok};
               });

    // Shared world changes: party members post the event flags they changed in the session
    // and read back the others' changes. Called by the emulator every few seconds in a session.
    auto flagBoard = std::make_shared<Bloodborne::PartyFlagBoard>();
    http.route(
        "/bloodborne/party/flags", QHttpServerRequest::Method::Post,
        [flagBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto update = Bloodborne::ParseFlagUpdate(request.body());
            if (!update) {
                return InvalidRequest(QStringLiteral("Invalid party flag update"));
            }
            if (!update->changes.isEmpty()) {
                qInfo().noquote() << "Bloodborne party flags from" << update->member << ":"
                                  << update->changes.size() << "changes";
            }
            const QByteArray board = Bloodborne::FormatFlagBoard(
                flagBoard->Update(Party(request), update->member, update->since, update->changes));
            return QHttpServerResponse{"text/plain", board, QHttpServerResponse::StatusCode::Ok};
        });

    // Enemy deaths kept in step across loads: a member posts "<handle>:1" for each enemy it
    // killed during its visit to a map and "<handle>:0" when that visit ends (its copies
    // respawn), in the flag board's format; a member loading a map later removes its copies of
    // the others' kills there. Called by the emulator every few seconds.
    auto killBoard = std::make_shared<Bloodborne::PartyFlagBoard>();
    http.route(
        "/bloodborne/party/kills", QHttpServerRequest::Method::Post,
        [killBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto update = Bloodborne::ParseFlagUpdate(request.body());
            if (!update) {
                return InvalidRequest(QStringLiteral("Invalid party kill update"));
            }
            if (!update->changes.isEmpty()) {
                qInfo().noquote() << "Bloodborne party kills from" << update->member << ":"
                                  << update->changes.size() << "changes";
            }
            // A game's first post: its kills from an earlier run of the game go (a restart
            // under the same member name never knew them to take them back).
            if (update->since == Bloodborne::PartyFlagBoard::FreshStart) {
                killBoard->WithdrawMember(Party(request), update->member);
            }
            const QByteArray board = Bloodborne::FormatKillBoard(
                killBoard->Update(Party(request), update->member, update->since, update->changes));
            return QHttpServerResponse{"text/plain", board, QHttpServerResponse::StatusCode::Ok};
        });

    // World state per map: the host posts its map's flag bits every few seconds and a guest
    // loading that map takes them (killed-for-good enemies, doors, shortcuts).
    auto mapStateBoard = std::make_shared<Bloodborne::PartyMapStateBoard>();
    http.route(
        "/bloodborne/party/mapstate", QHttpServerRequest::Method::Post,
        [mapStateBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto update = Bloodborne::ParseMapStateUpdate(request.body());
            if (!update) {
                return InvalidRequest(QStringLiteral("Invalid party map state update"));
            }
            const QByteArray board = Bloodborne::FormatMapStateBoard(
                mapStateBoard->Update(Party(request), update->member, update->map, update->bits));
            return QHttpServerResponse{"text/plain", board, QHttpServerResponse::StatusCode::Ok};
        });

    // Enemy HP kept in step: every member posts the enemies it hurt on its map (the map state
    // board's "<member> <map> <hex>" format, emulator EncodeEnemyHp) about every second and reads
    // the others' latest, so each game shows the lowest HP left.
    // A member that stopped posting (its game quit) drops out after a few seconds, so a later
    // session's enemies do not start hurt by its old report.
    auto enemyHpBoard = std::make_shared<Bloodborne::PartyMapStateBoard>(5'000);
    http.route("/bloodborne/party/hp", QHttpServerRequest::Method::Post,
               [enemyHpBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
                   const auto update = Bloodborne::ParseMapStateUpdate(request.body());
                   if (!update) {
                       return InvalidRequest(QStringLiteral("Invalid party enemy HP update"));
                   }
                   const QByteArray board = Bloodborne::FormatMapStateBoard(
                       enemyHpBoard->Update(Party(request), update->member, update->map,
                                            update->bits, QDateTime::currentMSecsSinceEpoch()));
                   return QHttpServerResponse{"text/plain", board,
                                              QHttpServerResponse::StatusCode::Ok};
               });

    // Characters hidden or shown as on the host: the host posts every character of its map with
    // its event disable byte (the map state board's format, emulator EncodeShownCharacters) about
    // every second, guests post map 0 and read it, and hide or show their copies the same (story
    // NPCs the host's events hide, as Iosefka at her clinic door). A host that stopped posting
    // drops out after a few seconds.
    auto shownBoard = std::make_shared<Bloodborne::PartyMapStateBoard>(5'000);
    http.route("/bloodborne/party/shown", QHttpServerRequest::Method::Post,
               [shownBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
                   const auto update = Bloodborne::ParseMapStateUpdate(request.body());
                   if (!update) {
                       return InvalidRequest(QStringLiteral("Invalid party shown update"));
                   }
                   const QByteArray board = Bloodborne::FormatMapStateBoard(
                       shownBoard->Update(Party(request), update->member, update->map, update->bits,
                                          QDateTime::currentMSecsSinceEpoch()));
                   return QHttpServerResponse{"text/plain", board,
                                              QHttpServerResponse::StatusCode::Ok};
               });

    // Shared kill echoes: a member posts the echoes it got from kills made with no other member
    // on its map and reads back the others'. Called by the emulator every few seconds.
    auto echoBoard = std::make_shared<Bloodborne::PartyEchoBoard>();
    http.route(
        "/bloodborne/party/echoes", QHttpServerRequest::Method::Post,
        [echoBoard](const QHttpServerRequest& request) -> QHttpServerResponse {
            const auto update = Bloodborne::ParseEchoUpdate(request.body());
            if (!update) {
                return InvalidRequest(QStringLiteral("Invalid party echo update"));
            }
            if (!update->echoes.isEmpty()) {
                qInfo().noquote() << "Bloodborne party echoes from" << update->member << ":"
                                  << update->echoes.size() << "kills";
            }
            const QByteArray board = Bloodborne::FormatEchoBoard(echoBoard->Update(
                Party(request), update->member, update->since, update->echoes, update->maps));
            return QHttpServerResponse{"text/plain", board, QHttpServerResponse::StatusCode::Ok};
        });

    // Party death: members post their state, HP, revive progress and position (every second,
    // a few times a second while someone is down) and read back everyone's, with the wipes the
    // board declared: a map where nobody stands any more takes everyone down there.
    auto vitalsBoard = std::make_shared<Bloodborne::PartyVitalsBoard>();
    auto lastVitals = std::make_shared<QHash<QByteArray, QByteArray>>();
    auto lastVitalsMutex = std::make_shared<QMutex>();
    http.route("/bloodborne/party/vitals", QHttpServerRequest::Method::Post,
               [vitalsBoard, lastVitals,
                lastVitalsMutex](const QHttpServerRequest& request) -> QHttpServerResponse {
                   const auto update = Bloodborne::ParseVitalsUpdate(request.body());
                   if (!update) {
                       return InvalidRequest(QStringLiteral("Invalid party vitals update"));
                   }
                   const QByteArray party = Party(request);
                   const auto answer =
                       vitalsBoard->Update(party, *update, QDateTime::currentMSecsSinceEpoch());
                   // Logged when someone's state or the wipes change, not for every HP change.
                   QByteArray summary;
                   for (const auto& member : answer.members) {
                       summary += member.member + ' ' + member.vital + ' ' +
                                  QByteArray::number(member.map, 16) + ", ";
                   }
                   for (const auto& wipe : answer.wipes) {
                       summary += "wipe " + QByteArray::number(wipe.id) + " of " +
                                  QByteArray::number(wipe.map, 16) + " (" + wipe.members.join(' ') +
                                  "), ";
                   }
                   {
                       QMutexLocker lock(lastVitalsMutex.get());
                       QByteArray& last = (*lastVitals)[party];
                       if (last != summary) {
                           last = summary;
                           qInfo().noquote() << "Bloodborne party vitals:"
                                             << (summary.isEmpty() ? summary : summary.chopped(2));
                       }
                   }
                   return QHttpServerResponse{"text/plain", Bloodborne::FormatVitalsBoard(answer),
                                              QHttpServerResponse::StatusCode::Ok};
               });
}

} // namespace WebApiRoutes
