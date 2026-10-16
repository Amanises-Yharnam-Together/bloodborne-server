// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <map>
#include <memory>
#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTcpSocket>
#include <QTimer>
#include <QUdpSocket>
#include "bloodborne_world.h"
#include "client_session.h"
#include "coop/rpc_tunnel.h"
#include "database.h"
#include "server_link.h"

// shadNet's UDP port: the games' link (server_link.h), which relays their frames and carries
// their messages with the parties' worlds (bloodborne_world.h). Phase 11: its hello signs the
// player in (the accounts in `db_path`), and each game's calls for shadNet's TCP port come through
// it (coop/rpc_tunnel.h) and go on to that port on 127.0.0.1, so a host opens this port only.
class LinkServer : public QObject {
    Q_OBJECT
public:
    explicit LinkServer(SharedState* shared, const QString& db_path, quint16 tcp_port,
                        QObject* parent = nullptr);
    bool Start(const QHostAddress& addr, uint16_t port);

private slots:
    void OnReadyRead();
    void OnLinkTick();
    void OnWorldTick();

private:
    // The games' link (co-op phase 2, server_link.h): relayed frames, hellos, pings.
    void HandleLink(const QByteArray& datagram, const QHostAddress& sender, uint16_t senderPort);
    void Apply(const ServerLink::Hub::Result& result);
    void Apply(const BloodborneWorld::Service::Result& result);
    bool SignIn(const QByteArray& online_id, const QByteArray& secret);

    // A game's end of its Rpc tunnel here: the connection to shadNet's TCP port it stands for.
    struct RpcEnd {
        std::unique_ptr<BloodborneCoop::RpcTunnel> tunnel;
        ServerLink::Endpoint to;
        QTcpSocket* socket = nullptr;
        QByteArray pending; // what came before the connection to the TCP port was up
    };
    void OnRpc(const ServerLink::Endpoint& from, const QByteArray& online_id,
               const QByteArray& body);
    void OpenRpc(const QByteArray& online_id, RpcEnd& end);
    void CloseRpc(RpcEnd& end);
    void FlushRpc(RpcEnd& end);

    QUdpSocket* m_socket;
    SharedState* m_shared;
    ServerLink::Hub m_link;
    QTimer* m_linkTimer;
    // The parties' worlds (co-op phase 3), on the link's Coop datagrams; its timer runs its
    // resends and heartbeats.
    BloodborneWorld::Service m_world;
    QTimer* m_worldTimer;
    QElapsedTimer m_clock;
    Database m_db;
    quint16 m_tcpPort;
    quint32 m_rpcInstance;
    QHash<QByteArray, QByteArray> m_signedIn; // player id -> its secret's hash, once signed in
    std::map<QByteArray, std::unique_ptr<RpcEnd>> m_rpc; // by player id
};
