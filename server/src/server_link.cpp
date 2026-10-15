// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
#include "server_link.h"

#include <algorithm>
#include <QtEndian>

namespace ServerLink {

namespace {

constexpr int HeaderSize = 9; // ffff ffff, "SHLK", the kind
constexpr int DataHeaderSize = HeaderSize + 6;
constexpr quint64 DropsLogged = 50;

QByteArray Start(Kind kind) {
    QByteArray out = QByteArray::fromHex("ffffffff") + "SHLK";
    out.append(static_cast<char>(kind));
    return out;
}

template <typename T>
void Put(QByteArray& out, T value) {
    for (size_t index = 0; index < sizeof(T); ++index) {
        out.append(static_cast<char>(static_cast<quint64>(value) >> (8 * index)));
    }
}

void PutEndpoint(QByteArray& out, const Endpoint& endpoint) {
    const quint32 ipv4 = qToBigEndian(endpoint.ipv4);
    const quint16 port = qToBigEndian(endpoint.port);
    out.append(reinterpret_cast<const char*>(&ipv4), 4);
    out.append(reinterpret_cast<const char*>(&port), 2);
}

void PutText(QByteArray& out, const QByteArray& text) {
    const int size = std::min<int>(static_cast<int>(text.size()), 0xFF);
    out.append(static_cast<char>(size));
    out.append(text.left(size));
}

// Reads a body from its start; every read fails once the bytes run out.
class Reader {
public:
    Reader(const QByteArray& bytes, int at) : bytes{bytes}, at{at} {}

    template <typename T>
    bool Get(T& value) {
        if (bytes.size() - at < static_cast<int>(sizeof(T))) {
            return false;
        }
        quint64 read = 0;
        for (size_t index = 0; index < sizeof(T); ++index) {
            read |= quint64{static_cast<quint8>(bytes[at + static_cast<int>(index)])}
                    << (8 * index);
        }
        value = static_cast<T>(read);
        at += static_cast<int>(sizeof(T));
        return true;
    }

    bool GetEndpoint(Endpoint& endpoint) {
        if (bytes.size() - at < 6) {
            return false;
        }
        endpoint.ipv4 = qFromBigEndian<quint32>(bytes.constData() + at);
        endpoint.port = qFromBigEndian<quint16>(bytes.constData() + at + 4);
        at += 6;
        return true;
    }

    bool GetText(QByteArray& text) {
        quint8 size = 0;
        if (!Get(size) || bytes.size() - at < size) {
            return false;
        }
        text = bytes.mid(at, size);
        at += size;
        return true;
    }

    QByteArray Rest() const {
        return bytes.mid(at);
    }

private:
    const QByteArray& bytes;
    int at;
};

std::optional<Reader> Body(const QByteArray& datagram, Kind kind) {
    if (datagram.size() < HeaderSize ||
        static_cast<quint8>(datagram[8]) != static_cast<quint8>(kind) ||
        !datagram.startsWith(QByteArray::fromHex("ffffffff") + "SHLK")) {
        return std::nullopt;
    }
    return Reader{datagram, HeaderSize};
}

// The first bytes of a frame, past its vports, in hex.
QString Head(const QByteArray& frame) {
    return QString::fromLatin1(frame.mid(4, 16).toHex());
}

quint16 Vport(const QByteArray& frame, int at) {
    return frame.size() >= at + 2 ? qFromBigEndian<quint16>(frame.constData() + at) : 0;
}

} // namespace

QString Endpoint::Text() const {
    return QStringLiteral("%1.%2.%3.%4:%5")
        .arg(ipv4 >> 24)
        .arg((ipv4 >> 16) & 0xFF)
        .arg((ipv4 >> 8) & 0xFF)
        .arg(ipv4 & 0xFF)
        .arg(port);
}

std::optional<Kind> KindOf(const QByteArray& datagram) {
    if (datagram.size() < HeaderSize) {
        return std::nullopt;
    }
    const quint8 kind = static_cast<quint8>(datagram[8]);
    if (kind < static_cast<quint8>(Kind::Hello) || kind > static_cast<quint8>(Kind::Coop) ||
        !Body(datagram, static_cast<Kind>(kind))) {
        return std::nullopt;
    }
    return static_cast<Kind>(kind);
}

QByteArray Encode(const Hello& hello) {
    QByteArray out = Start(Kind::Hello);
    out.append(static_cast<char>(hello.version));
    PutText(out, hello.online_id);
    PutText(out, hello.build);
    return out;
}

QByteArray Encode(const Welcome& welcome) {
    QByteArray out = Start(Kind::Welcome);
    out.append(static_cast<char>(welcome.accepted ? 1 : 0));
    PutText(out, welcome.text);
    return out;
}

QByteArray Encode(const Data& data) {
    QByteArray out = Start(Kind::Data);
    out.reserve(DataHeaderSize + data.frame.size());
    PutEndpoint(out, data.peer);
    out.append(data.frame);
    return out;
}

QByteArray Encode(Kind kind, const Ping& ping) {
    QByteArray out = Start(kind);
    Put(out, ping.seq);
    Put(out, ping.sent_ms);
    return out;
}

QByteArray Encode(const Member& member) {
    QByteArray out = Start(Kind::Member);
    out.append(static_cast<char>(member.state));
    Put(out, member.gap_ms);
    PutEndpoint(out, member.endpoint);
    PutText(out, member.online_id);
    return out;
}

QByteArray EncodeCoop(const QByteArray& body) {
    return Start(Kind::Coop) + body;
}

std::optional<Hello> DecodeHello(const QByteArray& datagram) {
    auto body = Body(datagram, Kind::Hello);
    Hello hello;
    if (!body || !body->Get(hello.version) || !body->GetText(hello.online_id) ||
        !body->GetText(hello.build)) {
        return std::nullopt;
    }
    return hello;
}

std::optional<Welcome> DecodeWelcome(const QByteArray& datagram) {
    auto body = Body(datagram, Kind::Welcome);
    quint8 accepted = 0;
    Welcome welcome;
    if (!body || !body->Get(accepted) || !body->GetText(welcome.text)) {
        return std::nullopt;
    }
    welcome.accepted = accepted != 0;
    return welcome;
}

std::optional<Data> DecodeData(const QByteArray& datagram) {
    auto body = Body(datagram, Kind::Data);
    Data data;
    if (!body || !body->GetEndpoint(data.peer)) {
        return std::nullopt;
    }
    data.frame = body->Rest();
    return data;
}

std::optional<Ping> DecodePing(Kind kind, const QByteArray& datagram) {
    auto body = Body(datagram, kind);
    Ping ping;
    if (!body || !body->Get(ping.seq) || !body->Get(ping.sent_ms)) {
        return std::nullopt;
    }
    return ping;
}

std::optional<Member> DecodeMember(const QByteArray& datagram) {
    auto body = Body(datagram, Kind::Member);
    quint8 state = 0;
    Member member;
    if (!body || !body->Get(state) || state > static_cast<quint8>(MemberState::Gone) ||
        !body->Get(member.gap_ms) || !body->GetEndpoint(member.endpoint) ||
        !body->GetText(member.online_id)) {
        return std::nullopt;
    }
    member.state = static_cast<MemberState>(state);
    return member;
}

std::optional<QByteArray> DecodeCoop(const QByteArray& datagram) {
    auto body = Body(datagram, Kind::Coop);
    if (!body) {
        return std::nullopt;
    }
    return body->Rest();
}

Hub::Hub(qint64 quiet_ms, qint64 gone_ms, qint64 stats_ms, int pair_lines)
    : quiet_ms{quiet_ms}, gone_ms{gone_ms}, stats_ms{stats_ms}, pair_lines{pair_lines} {}

QByteArray Hub::Build() const {
    return members.isEmpty() ? QByteArray{} : members.cbegin()->build;
}

QString Hub::Name(const Link& link) const {
    return QStringLiteral("%1 (%2)").arg(QString::fromUtf8(link.online_id), link.endpoint.Text());
}

bool Hub::SameTeam(const Endpoint& a, const Endpoint& b) const {
    if (!party_of) {
        return true; // nobody said the teams (the hub alone)
    }
    const QString party = party_of(a);
    return !party.isEmpty() && party == party_of(b);
}

Hub::Result Hub::Receive(const Endpoint& from, const QByteArray& datagram, qint64 now_ms) {
    Result result;
    const auto kind = KindOf(datagram);
    if (!kind) {
        return result;
    }
    if (*kind == Kind::Hello) {
        OnHello(from, datagram, now_ms, result);
        return result;
    }
    const auto found = members.find(from.Key());
    if (found == members.end()) {
        if (dropped++ < DropsLogged) {
            result.log.append(QStringLiteral("server link: kind %1 from %2, which "
                                             "said no hello: dropped")
                                  .arg(static_cast<int>(*kind))
                                  .arg(from.Text()));
        }
        return result;
    }
    Link& link = *found;
    Heard(link, now_ms, result);
    switch (*kind) {
    case Kind::Data:
        OnData(link, datagram, result);
        break;
    case Kind::Ping:
        if (const auto ping = DecodePing(Kind::Ping, datagram)) {
            ++link.pings;
            result.sends.append({from, Encode(Kind::Pong, *ping)});
        }
        break;
    case Kind::Coop:
        if (auto body = DecodeCoop(datagram)) {
            result.coop.append({from, link.online_id, std::move(*body)});
        }
        break;
    default:
        break; // the games' own kinds only
    }
    return result;
}

void Hub::OnHello(const Endpoint& from, const QByteArray& datagram, qint64 now_ms, Result& result) {
    const auto hello = DecodeHello(datagram);
    if (!hello) {
        return;
    }
    QByteArray refusal;
    if (hello->version != LinkVersion) {
        refusal = "link version " + QByteArray::number(hello->version) + ", this server speaks " +
                  QByteArray::number(LinkVersion);
    } else {
        // Every other linked game's build (a game saying hello again is not one).
        for (const Link& link : std::as_const(members)) {
            if (!(link.endpoint == from) && link.build != hello->build) {
                refusal = "another build: everyone needs the same release (this game runs " +
                          hello->build + ", the games here run " + link.build + ")";
                break;
            }
        }
    }
    if (!refusal.isEmpty()) {
        result.sends.append({from, Encode(Welcome{false, refusal})});
        result.log.append(
            QStringLiteral("server link: refused %1 (%2): %3")
                .arg(QString::fromUtf8(hello->online_id), from.Text(), QString::fromUtf8(refusal)));
        return;
    }
    if (!stats_started) {
        stats_started = true;
        stats_at_ms = now_ms;
    }
    const bool again = members.contains(from.Key());
    Link& link = members[from.Key()];
    link.endpoint = from;
    link.online_id = hello->online_id;
    link.build = hello->build;
    link.last_heard_ms = now_ms;
    link.quiet = false;
    result.sends.append({from, Encode(Welcome{true, {}})});
    result.log.append(
        QStringLiteral("server link: %1 %2, build %3, %4 linked")
            .arg(again ? "hello again from" : "linked", Name(link), QString::fromUtf8(link.build))
            .arg(members.size()));
}

void Hub::OnData(Link& sender, const QByteArray& datagram, Result& result) {
    const auto data = DecodeData(datagram);
    if (!data) {
        return;
    }
    ++sender.frames_in;
    sender.bytes_in += static_cast<quint64>(data->frame.size());
    const auto target = members.find(data->peer.Key());
    if (target == members.end()) {
        if (dropped++ < DropsLogged) {
            result.log.append(QStringLiteral("server link: %1 sent %2 bytes to %3: no game linked "
                                             "there, dropped")
                                  .arg(Name(sender))
                                  .arg(data->frame.size())
                                  .arg(data->peer.Text()));
        }
        return;
    }
    if (!SameTeam(sender.endpoint, target->endpoint)) {
        if (dropped++ < DropsLogged) {
            result.log.append(QStringLiteral("server link: %1 sent %2 bytes to %3, a game of "
                                             "another team or none: dropped")
                                  .arg(Name(sender))
                                  .arg(data->frame.size())
                                  .arg(Name(*target)));
        }
        return;
    }
    ++target->frames_out;
    target->bytes_out += static_cast<quint64>(data->frame.size());
    result.sends.append({target->endpoint, Encode(Data{sender.endpoint, data->frame})});
    // The first frames between each pair: a stalled join shows which went.
    const quint64 count = ++pair_frames[qMakePair(sender.endpoint.Key(), target->endpoint.Key())];
    if (count <= static_cast<quint64>(pair_lines)) {
        result.log.append(QStringLiteral("server link: %1 -> %2 #%3: %4 bytes, vport %5 -> %6, %7")
                              .arg(Name(sender), Name(*target))
                              .arg(count)
                              .arg(data->frame.size() - 4)
                              .arg(Vport(data->frame, 0))
                              .arg(Vport(data->frame, 2))
                              .arg(Head(data->frame)));
    }
}

void Hub::Heard(Link& link, qint64 now_ms, Result& result) {
    if (link.quiet) {
        const qint64 gap = now_ms - link.last_heard_ms;
        link.quiet = false;
        result.log.append(
            QStringLiteral("server link: %1 heard again after %2 ms").arg(Name(link)).arg(gap));
        TellOthers(link, MemberState::Back, gap, result);
    }
    link.last_heard_ms = now_ms;
}

void Hub::TellOthers(const Link& about, MemberState state, qint64 gap_ms, Result& result) {
    const QByteArray told =
        Encode(Member{state, static_cast<quint32>(std::clamp<qint64>(gap_ms, 0, 0xFFFFFFFF)),
                      about.endpoint, about.online_id});
    for (const Link& other : std::as_const(members)) {
        if (!(other.endpoint == about.endpoint) && SameTeam(about.endpoint, other.endpoint)) {
            result.sends.append({other.endpoint, told});
        }
    }
}

Hub::Result Hub::Tick(qint64 now_ms) {
    Result result;
    QList<quint64> gone;
    for (const Link& link : std::as_const(members)) {
        const qint64 gap = now_ms - link.last_heard_ms;
        if (gap > gone_ms) {
            gone.append(link.endpoint.Key());
        }
    }
    for (const quint64 key : gone) {
        const Link link = members.take(key);
        result.log.append(QStringLiteral("server link: %1 gone, nothing for %2 ms, %3 linked")
                              .arg(Name(link))
                              .arg(now_ms - link.last_heard_ms)
                              .arg(members.size()));
        TellOthers(link, MemberState::Gone, now_ms - link.last_heard_ms, result);
        result.gone.append(link.endpoint);
        for (auto it = pair_frames.begin(); it != pair_frames.end();) {
            it = it.key().first == key || it.key().second == key ? pair_frames.erase(it)
                                                                 : std::next(it);
        }
    }
    for (Link& link : members) {
        const qint64 gap = now_ms - link.last_heard_ms;
        if (!link.quiet && gap > quiet_ms) {
            link.quiet = true;
            result.log.append(
                QStringLiteral("server link: nothing from %1 for %2 ms").arg(Name(link)).arg(gap));
            TellOthers(link, MemberState::Quiet, gap, result);
        }
    }
    if (stats_started && now_ms - stats_at_ms >= stats_ms) {
        stats_at_ms = now_ms;
        for (Link& link : members) {
            result.log.append(QStringLiteral("server link: %1 in %2 frames/%3 bytes, out %4 "
                                             "frames/%5 bytes, %6 pings%7")
                                  .arg(Name(link))
                                  .arg(link.frames_in)
                                  .arg(link.bytes_in)
                                  .arg(link.frames_out)
                                  .arg(link.bytes_out)
                                  .arg(link.pings)
                                  .arg(link.quiet ? ", quiet" : ""));
            link.frames_in = link.bytes_in = link.frames_out = link.bytes_out = 0;
            link.pings = 0;
        }
    }
    return result;
}

} // namespace ServerLink
