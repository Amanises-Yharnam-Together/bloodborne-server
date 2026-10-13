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

class StunServer : public QObject {
    Q_OBJECT
public:
    explicit StunServer(SharedState* shared, QObject* parent = nullptr);
    bool Start(const QHostAddress& addr, uint16_t port);

private slots:
    void OnReadyRead();
    void OnLinkTick();
    void OnWorldTick();

private:
    void HandleStunPing(const QByteArray& data, const QHostAddress& sender, uint16_t senderPort);
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
