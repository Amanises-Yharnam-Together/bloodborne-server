// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include <QDateTime>
#include <QDebug>
#include <QtEndian>
#include "stream_extractor.h"
#include "stun_server.h"

// Must match the client's SIGNALING_VPORT_NBO / VPORT_HEADER_SIZE
static constexpr uint16_t SIGNALING_VPORT_NBO = 0xFFFF;
static constexpr int VPORT_HEADER_SIZE = 4;

static QByteArray FrameSignaling(const QByteArray& payload) {
    QByteArray framed;
    framed.reserve(VPORT_HEADER_SIZE + payload.size());
    framed.append(reinterpret_cast<const char*>(&SIGNALING_VPORT_NBO), 2);
    framed.append(reinterpret_cast<const char*>(&SIGNALING_VPORT_NBO), 2);
    framed.append(payload);
    return framed;
}

static QByteArray StripVportHeader(const QByteArray& data) {
    if (data.size() < VPORT_HEADER_SIZE)
        return {};
    uint16_t src_vp, dst_vp;
    memcpy(&src_vp, data.constData(), 2);
    memcpy(&dst_vp, data.constData() + 2, 2);
    if (src_vp != SIGNALING_VPORT_NBO || dst_vp != SIGNALING_VPORT_NBO)
        return {}; // not a signaling packet
    return data.mid(VPORT_HEADER_SIZE);
}

StunServer::StunServer(SharedState* shared, QObject* parent)
    : QObject(parent), m_socket(new QUdpSocket(this)), m_shared(shared),
      m_linkTimer(new QTimer(this)),
      m_world(shared->config ? shared->config->GetBloodborneWorldPath() : QString{}),
      m_worldTimer(new QTimer(this)) {
    connect(m_socket, &QUdpSocket::readyRead, this, &StunServer::OnReadyRead);
    connect(m_linkTimer, &QTimer::timeout, this, &StunServer::OnLinkTick);
    connect(m_worldTimer, &QTimer::timeout, this, &StunServer::OnWorldTick);
    // Phase 7: the link relays only between games of one team (the party of their hello).
    m_link.SetPartyOf([this](const ServerLink::Endpoint& link) { return m_world.PartyOf(link); });
    m_clock.start();
    m_linkTimer->start(500);
    // The worlds' clock is the wall clock: their journals keep it across restarts.
    Apply(m_world.Load(QDateTime::currentMSecsSinceEpoch()));
    m_worldTimer->start(50);
}

bool StunServer::Start(const QHostAddress& addr, uint16_t port) {
    if (!m_socket->bind(addr, port)) {
        qCritical() << "STUN UDP bind failed on" << addr.toString() << ":" << port
                    << m_socket->errorString();
        return false;
    }
    qInfo().nospace().noquote() << "STUN UDP listener on: " << addr.toString() << ":" << port;
    return true;
}

void StunServer::OnReadyRead() {
    while (m_socket->hasPendingDatagrams()) {
        QByteArray raw;
        raw.resize(static_cast<int>(m_socket->pendingDatagramSize()));
        QHostAddress sender;
        uint16_t senderPort = 0;
        m_socket->readDatagram(raw.data(), raw.size(), &sender, &senderPort);

        if (raw.isEmpty())
            continue;

        if (ServerLink::KindOf(raw)) {
            HandleLink(raw, sender, senderPort);
            continue;
        }

        // Strip signaling vport header
        QByteArray data = StripVportHeader(raw);
        if (data.isEmpty())
            continue;

        uint8_t cmd = static_cast<uint8_t>(data[0]);
        switch (cmd) {
        case 0x01:
            HandleStunPing(data, sender, senderPort);
            break;
        default:
            break;
        }
    }
}

void StunServer::HandleStunPing(const QByteArray& data, const QHostAddress& sender,
                                uint16_t senderPort) {
    // cmd(1) + online_id(16) + local_ip(4) = 21 bytes minimum
    if (data.size() < 21)
        return;

    // Extract null-padded online_id from bytes 1-16
    QByteArray rawId = data.mid(1, 16);
    int nullPos = rawId.indexOf('\0');
    QString npid = QString::fromUtf8(rawId.left(nullPos >= 0 ? nullPos : 16)).trimmed();
    if (npid.isEmpty())
        return;

    QString extIpStr = sender.toString();
    // Strip IPv6-mapped prefix if present (e.g. "::ffff:192.168.1.1" -> "192.168.1.1")
    if (extIpStr.startsWith("::ffff:"))
        extIpStr = extIpStr.mid(7);

    // Phase 7: a game on the server link is known by its virtual address (server_link.h), which
    // the rooms and signaling then hand to the others; it never learns another's real one.
    quint16 extPort = senderPort;
    bool linkedV4 = false;
    const quint32 senderV4 = sender.toIPv4Address(&linkedV4);
    if (linkedV4) {
        if (const auto virtualEndpoint = m_link.VirtualOf({senderV4, senderPort})) {
            extIpStr = QHostAddress(virtualEndpoint->ipv4).toString();
            extPort = virtualEndpoint->port;
        }
    }

    qInfo() << "STUN ping: npid=" << npid << "ext=" << extIpStr << ":" << extPort;

    // Record external endpoint
    {
        QWriteLocker lk(&m_shared->matching.udpLock);
        m_shared->matching.udpExt[npid] = {extIpStr, extPort};
    }

    // Reply: ext_ip(4 bytes network order) + ext_port(2 bytes network order)
    QHostAddress extAddr(extIpStr);
    uint32_t ipNet = qToBigEndian(static_cast<uint32_t>(extAddr.toIPv4Address()));
    uint16_t portNet = qToBigEndian(extPort);

    QByteArray response;
    response.append(reinterpret_cast<const char*>(&ipNet), 4);
    response.append(reinterpret_cast<const char*>(&portNet), 2);
    m_socket->writeDatagram(FrameSignaling(response), sender, senderPort);
}

void StunServer::HandleLink(const QByteArray& datagram, const QHostAddress& sender,
                            uint16_t senderPort) {
    bool ipv4 = false;
    const quint32 address = sender.toIPv4Address(&ipv4);
    if (!ipv4)
        return;
    Apply(m_link.Receive({address, senderPort}, datagram, m_clock.elapsed()));
}

void StunServer::OnLinkTick() {
    Apply(m_link.Tick(m_clock.elapsed()));
}

void StunServer::OnWorldTick() {
    Apply(m_world.Tick(QDateTime::currentMSecsSinceEpoch()));
}

void StunServer::Apply(const ServerLink::Hub::Result& result) {
    for (const auto& send : result.sends)
        m_socket->writeDatagram(send.datagram, QHostAddress(send.to.ipv4), send.to.port);
    for (const auto& line : result.log)
        qInfo().noquote() << line;
    for (const auto& gone : result.gone)
        m_world.Forget(gone);
    for (const auto& coop : result.coop)
        Apply(m_world.Receive(coop.from, coop.online_id, coop.body,
                              QDateTime::currentMSecsSinceEpoch()));
}

void StunServer::Apply(const BloodborneWorld::Service::Result& result) {
    for (const auto& send : result.sends)
        m_socket->writeDatagram(ServerLink::EncodeCoop(send.body), QHostAddress(send.to.ipv4),
                                send.to.port);
    for (const auto& line : result.log)
        qInfo().noquote() << line;
}
