// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Party-scoped matching: signs advertised with a party key are only visible to,
// and only claimable by, searches carrying the same key. Signs without a key
// behave as before.
#include <cstdlib>
#include <iostream>

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include "bloodborne_summon_broker.h"

namespace {

QJsonObject Parse(const QByteArray &raw) {
  QJsonParseError error{};
  const QJsonDocument document = QJsonDocument::fromJson(raw, &error);
  if (error.error != QJsonParseError::NoError || !document.isObject()) {
    std::cerr << "JSON parse failed: " << error.errorString().toStdString()
              << '\n';
    std::exit(1);
  }
  return document.object();
}

bool Check(bool condition, const char *expression, int line) {
  if (!condition) {
    std::cerr << "check failed at line " << line << ": " << expression << '\n';
  }
  return condition;
}

#define CHECK(expression)                                                      \
  do {                                                                         \
    if (!Check((expression), #expression, __LINE__))                           \
      return 1;                                                                \
  } while (false)

QByteArray Advertisement(const char *session, int userId) {
  return QByteArray(R"({"MessageId":"SummonDataCreateRequest","SessionId":")") +
         session + R"(","UserId":)" + QByteArray::number(userId) +
         R"(,"CharaId":1,"AreaId":385875968,"AreaRegionId":230100,"ChannelId":0,"MatchingLevel":46,"SummonData":"d","SummonDataVersion":3,"SummonMethod":0,"SummonType":0,"SummonWord":null,"PosX":143,"PosY":-116,"PosZ":-87})";
}

QByteArray Search(int userId) {
  return R"({"MessageId":"SummonDataGetListRequest","SessionId":"host","UserId":)" +
         QByteArray::number(userId) +
         R"(,"AreaId":385875968,"AreaRegionId":230100,"ChannelId":0,"MatchingLevel":46,"SummonDataVersion":3,"SummonMethod":0,"SummonTypeList":[{"SummonType":0}],"SummonWord":null,"DistanceThreshold":100,"GetMaxCount":20,"PosX":143,"PosY":-116,"PosZ":-87})";
}

QByteArray Claim(const char *session, int hostUserId, int targetUserId) {
  return QByteArray(R"({"MessageId":"SummonDataSummonRequest","SessionId":")") +
         session + R"(","UserId":)" + QByteArray::number(hostUserId) +
         R"(,"TargetUserId":)" + QByteArray::number(targetUserId) +
         R"(,"TargetCharaId":1,"HostData":"h"})";
}

} // namespace

int main() {
  using Broker = Bloodborne::SummonBroker;
  const QByteArray partyA =
      "hunters:"
      "5e884898da28047151d0e56f8dc6292773603d0d6aabbdd62a11ef721d1542d8";
  const QByteArray partyB =
      "others:6b86b273ff34fce19d6b804eff5a3f5747ada4eaa22f1d49c01e52ddb7875b4b";

  const QByteArray signA = Advertisement("guest-a", 100);
  const QByteArray signB = Advertisement("guest-b", 200);
  const QByteArray signPublic = Advertisement("guest-public", 300);

  // Party signs are only visible to the same party.
  {
    Broker broker(60'000);
    broker.Advertise(Parse(signA), signA, 10, partyA);
    broker.Advertise(Parse(signB), signB, 10, partyB);
    broker.Advertise(Parse(signPublic), signPublic, 10);

    const QList<QByteArray> forA =
        broker.Search(Parse(Search(1)), 20, {}, partyA);
    CHECK(forA.size() == 1);
    CHECK(forA.front() == signA);

    const QList<QByteArray> forB =
        broker.Search(Parse(Search(2)), 20, {}, partyB);
    CHECK(forB.size() == 1);
    CHECK(forB.front() == signB);

    // A search without a party never sees party signs: they are private.
    const QList<QByteArray> forPublic = broker.Search(Parse(Search(3)), 20);
    CHECK(forPublic.size() == 1);
    CHECK(forPublic.front() == signPublic);
  }

  // Claims only succeed within the same party.
  {
    Broker broker(60'000);
    broker.Advertise(Parse(signA), signA, 10, partyA);

    const QByteArray outsiderClaim = Claim("guest-a", 2, 100);
    CHECK(broker.Claim(Parse(outsiderClaim), outsiderClaim, 20, {}, partyB)
              .status == Broker::ClaimStatus::NotFound);
    CHECK(broker.Claim(Parse(outsiderClaim), outsiderClaim, 21).status ==
          Broker::ClaimStatus::NotFound);
    CHECK(broker.StateFor(QStringLiteral("guest-a"), 100, 22) ==
          Broker::State::Advertised);

    const QByteArray memberClaim = Claim("guest-a", 1, 100);
    const auto claimed =
        broker.Claim(Parse(memberClaim), memberClaim, 30, {}, partyA);
    CHECK(claimed.status == Broker::ClaimStatus::Claimed);
    CHECK(claimed.targetUserId == 100);
  }

  // Party signs still show up for seamless cross-map searches of the same party
  // only.
  {
    Broker::Options options;
    options.seamlessCoop = true;
    Broker broker(options);
    broker.Advertise(Parse(signA), signA, 10, partyA);
    CHECK(broker.Search(Parse(Search(1)), 20, {}, partyA).size() == 1);
    CHECK(broker.Search(Parse(Search(2)), 20, {}, partyB).isEmpty());
  }

  // Header values are untrusted: trimmed, capped, and anything with control
  // characters or without a "name:secret" shape is treated as no party at all.
  {
    using Bloodborne::NormalizePartyKey;
    CHECK(NormalizePartyKey("  hunters:abc  ") == "hunters:abc");
    CHECK(NormalizePartyKey("").isEmpty());
    CHECK(NormalizePartyKey("nocolon").isEmpty());
    CHECK(NormalizePartyKey(":secret").isEmpty());
    CHECK(NormalizePartyKey("name:").isEmpty());
    CHECK(NormalizePartyKey("bad\r\nname:x").isEmpty());
    CHECK(NormalizePartyKey(QByteArray(200, 'a') + ":x").isEmpty());
  }

  std::cout << "Bloodborne summon party test passed\n";
  return 0;
}
