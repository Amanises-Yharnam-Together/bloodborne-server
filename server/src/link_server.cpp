// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <QDateTime>
#include <QDebug>
#include <QtEndian>
#include "link_server.h"

LinkServer::LinkServer(SharedState* shared, QObject* parent)
    : QObject(parent), m_socket(new QUdpSocket(this)), m_shared(shared),
      m_linkTimer(new QTimer(this)),
      m_world(shared->config ? shared->config->GetBloodborneWorldPath() : QString{}),
      m_worldTimer(new QTimer(this)) {
    connect(m_socket, &QUdpSocket::readyRead, this, &LinkServer::OnReadyRead);
    connect(m_linkTimer, &QTimer::timeout, this, &LinkServer::OnLinkTick);
    connect(m_worldTimer, &QTimer::timeout, this, &LinkServer::OnWorldTick);
    // Phase 7: the link relays only between games of one team (the party of their hello).
    m_link.SetPartyOf([this](const ServerLink::Endpoint& link) { return m_world.PartyOf(link); });
    // Phase 9: and forwards a game packet only to a teammate that needs it.
    m_link.SetPacketRoutes(
        [this](const ServerLink::Endpoint& from, const ServerLink::Endpoint& to, quint32 channel) {
            return m_world.RoutesPacket(from, to, channel);
        });
    m_clock.start();
    m_linkTimer->start(500);
    // The worlds' clock is the wall clock: their journals keep it across restarts.
    Apply(m_world.Load(QDateTime::currentMSecsSinceEpoch()));
    m_worldTimer->start(50);
}

bool LinkServer::Start(const QHostAddress& addr, uint16_t port) {
    if (!m_socket->bind(addr, port)) {
        qCritical() << "Link UDP bind failed on" << addr.toString() << ":" << port
                    << m_socket->errorString();
        return false;
    }
    qInfo().nospace().noquote() << "Link UDP listener on: " << addr.toString() << ":" << port;
    return true;
}

void LinkServer::OnReadyRead() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray raw;
        raw.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        uint16_t senderPort = 0;
        m_socket->readDatagram(raw.data(), raw.size(), &sender, &senderPort);

        if (raw.isEmpty())
            continue;

        // The games' link only (phase 9: no STUN, nothing else on this port).
        if (ServerLink::KindOf(raw)) {
            HandleLink(raw, sender, senderPort);
        }
    }
}

void LinkServer::HandleLink(const QByteArray& datagram, const QHostAddress& sender,
                            uint16_t senderPort) {
    bool ipv4 = false;
    const quint32 address = sender.toIPv4Address(&ipv4);
    if (!ipv4)
        return;
    Apply(m_link.Receive({address, senderPort}, datagram, m_clock.elapsed()));
}

void LinkServer::OnLinkTick() {
    Apply(m_link.Tick(m_clock.elapsed()));
}

void LinkServer::OnWorldTick() {
    Apply(m_world.Tick(QDateTime::currentMSecsSinceEpoch()));
}

void LinkServer::Apply(const ServerLink::Hub::Result& result) {
    for (const auto& send : result.sends)
        m_socket->writeDatagram(send.datagram, QHostAddress(send.to.ipv4), send.to.port);
    for (const auto& line : result.log)
        qInfo().noquote() << line;
    for (const auto& gone : result.gone)
        m_world.Forget(gone);
    // Phase 7: a linked game's address for the rooms and signaling is its virtual one, known from
    // its hello.
    for (const auto& linked : result.linked) {
        QWriteLocker lk(&m_shared->matching.udpLock);
        m_shared->matching.udpExt[QString::fromUtf8(linked.online_id)] = {
            QHostAddress(linked.virtual_endpoint.ipv4).toString(), linked.virtual_endpoint.port};
    }
    for (const auto& coop : result.coop)
        Apply(m_world.Receive(coop.from, coop.online_id, coop.body,
                              QDateTime::currentMSecsSinceEpoch()));
}

void LinkServer::Apply(const BloodborneWorld::Service::Result& result) {
    for (const auto& send : result.sends)
        m_socket->writeDatagram(ServerLink::EncodeCoop(send.body), QHostAddress(send.to.ipv4),
                                send.to.port);
    for (const auto& line : result.log)
        qInfo().noquote() << line;
}
