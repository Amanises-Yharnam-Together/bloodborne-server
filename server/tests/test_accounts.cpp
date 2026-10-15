// SPDX-FileCopyrightText: Copyright 2026 shadNet Project
// SPDX-License-Identifier: GPL-2.0-or-later
// Phase 11 (Kaniel 2026-10-15): no account to register. A player's id (the
// launcher makes it once) signs in, and the server makes its account the first
// time it sees it; after that the id's secret has to match, so nobody else
// signs in as that player.
#include <iostream>

#include <QCoreApplication>
#include <QTemporaryDir>

#include "database.h"

namespace {

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

int AFirstSignInMakesTheAccount(const QString &folder) {
  Database db("first");
  CHECK(db.Open(folder + "/first.db"));
  CHECK(db.Migrate());
  CHECK(!db.CheckUser("p0123456789abcd", "secret0123456789", {}, false));
  const auto made =
      db.SignIn("p0123456789abcd", "secret0123456789", {}, false, true);
  CHECK(made.has_value());
  CHECK(made->username == "p0123456789abcd");
  const auto again =
      db.SignIn("p0123456789abcd", "secret0123456789", {}, false, true);
  CHECK(again.has_value());
  CHECK(again->userId == made->userId);
  // Another secret is another player claiming this id: refused, not a second
  // account.
  CHECK(!db.SignIn("p0123456789abcd", "not-the-secret", {}, false, true));
  CHECK(db.TotalUsers() == 1);
  return 0;
}

int WithoutMakingAnUnknownIdIsRefused(const QString &folder) {
  Database db("off");
  CHECK(db.Open(folder + "/off.db"));
  CHECK(db.Migrate());
  CHECK(!db.SignIn("p0123456789abcd", "secret0123456789", {}, false, false));
  CHECK(db.TotalUsers() == 0);
  // An id the game cannot have is never made.
  CHECK(!db.SignIn("a b", "secret", {}, false, true));
  CHECK(!db.SignIn("p0123456789abcd", "", {}, false, true));
  CHECK(db.TotalUsers() == 0);
  return 0;
}

int AnOldAccountSignsInAsBefore(const QString &folder) {
  Database db("old");
  CHECK(db.Open(folder + "/old.db"));
  CHECK(db.Migrate());
  CHECK(!db.CreateAccount("kaniel", "pw", "avatar.png", "kaniel.pw@p2p.db"));
  CHECK(db.SignIn("kaniel", "pw", {}, false, true).has_value());
  CHECK(!db.SignIn("kaniel", "other", {}, false, true));
  CHECK(db.TotalUsers() == 1);
  return 0;
}

} // namespace

int main(int argc, char **argv) {
  QCoreApplication app(argc, argv);
  QTemporaryDir folder;
  if (!folder.isValid()) {
    std::cerr << "no temporary folder\n";
    return 1;
  }
  for (const auto test :
       {AFirstSignInMakesTheAccount, WithoutMakingAnUnknownIdIsRefused,
        AnOldAccountSignsInAsBefore}) {
    if (const int failed = test(folder.path())) {
      return failed;
    }
  }
  std::cout << "accounts: all passed\n";
  return 0;
}
