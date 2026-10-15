// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#pragma once
#include <QElapsedTimer>
#include <QHostAddress>
#include <QObject>
#include <QTimer>
#include <QUdpSocket>
#include "bloodborne_world.h"
#include "client_session.h"
#include "server_link.h"

// shadNet's UDP port: the games' link (server_link.h), which relays their frames and carries
// their messages with the parties' worlds (bloodborne_world.h).
class LinkServer : public QObject {
    Q_OBJECT
public:
    explicit LinkServer(SharedState* shared, QObject* parent = nullptr);
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

    QUdpSocket* m_socket;
    SharedState* m_shared;
    ServerLink::Hub m_link;
    QTimer* m_linkTimer;
    // The parties' worlds (co-op phase 3), on the link's Coop datagrams; its timer runs its
    // resends and heartbeats.
    BloodborneWorld::Service m_world;
    QTimer* m_worldTimer;
    QElapsedTimer m_clock;
};
