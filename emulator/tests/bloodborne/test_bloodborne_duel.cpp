// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

// Team duels (Kaniel 2026-10-25), the game's side: a player's part in the server's duel and what
// changes for it, whom it may strike, which bodies are foes', and what the end gives back.

#include <gtest/gtest.h>

#include "core/bloodborne_duel.h"

using namespace Core::Bloodborne;

namespace {

bbcoop::DuelState StateOf(u32 duel, bbcoop::DuelState::Stage stage,
                          std::vector<std::tuple<std::string, u32, u32, bool>> seats) {
    bbcoop::DuelState state;
    state.set_duel(duel);
    state.set_stage(stage);
    state.set_proposer(std::get<0>(seats.front()));
    for (const auto& [member, side, answer, out] : seats) {
        bbcoop::Duelist& duelist = *state.add_duelists();
        duelist.set_member(member);
        duelist.set_side(side);
        duelist.set_answer(answer);
        duelist.set_out(out);
    }
    return state;
}

} // namespace

// Asked, then in, then fighting: each step said once; whom it may strike only while it fights.
TEST(BloodborneDuel, FollowsTheServersDuel) {
    DuelFollower follower;
    EXPECT_TRUE(follower.MayPropose());
    DuelNews news = follower.Take(
        StateOf(1, bbcoop::DuelState::ASKING, {{"ada", 0, 1, false}, {"bob", 0, 0, false}}), "bob");
    EXPECT_TRUE(news.asked);
    EXPECT_EQ(follower.Part(), DuelPart::Asked);
    EXPECT_EQ(follower.AskedDuel(), std::optional<u32>{1});
    EXPECT_FALSE(follower.MayPropose()) << "one duel at a time";
    // The same state again (a resend): asked once.
    EXPECT_FALSE(follower
                     .Take(StateOf(1, bbcoop::DuelState::ASKING,
                                   {{"ada", 0, 1, false}, {"bob", 0, 0, false}}),
                           "bob")
                     .asked);

    news = follower.Take(
        StateOf(1, bbcoop::DuelState::COUNTDOWN, {{"ada", 1, 1, false}, {"bob", 2, 1, false}}),
        "bob");
    EXPECT_TRUE(news.joined) << "what it has is kept as it joins";
    EXPECT_FALSE(news.fight);
    EXPECT_EQ(follower.InDuel(), std::optional<u32>{1});
    EXPECT_TRUE(follower.Foes().empty()) << "no strikes before the fight";

    news = follower.Take(
        StateOf(1, bbcoop::DuelState::FIGHTING, {{"ada", 1, 1, false}, {"bob", 2, 1, false}}),
        "bob");
    EXPECT_TRUE(news.fight);
    EXPECT_FALSE(news.joined);
    EXPECT_EQ(follower.Foes(), std::vector<std::string>{"ada"});
}

// A defeat puts the player out (no foes); the end says who won and that the duel is over for it.
TEST(BloodborneDuel, OutAndOver) {
    DuelFollower loser, winner;
    const auto fighting =
        StateOf(1, bbcoop::DuelState::FIGHTING, {{"ada", 1, 1, false}, {"bob", 2, 1, false}});
    loser.Take(fighting, "bob");
    winner.Take(fighting, "ada");

    DuelNews news = loser.Take(
        StateOf(1, bbcoop::DuelState::FIGHTING, {{"ada", 1, 1, false}, {"bob", 2, 1, true}}),
        "bob");
    EXPECT_TRUE(news.out);
    EXPECT_EQ(loser.Part(), DuelPart::Out);
    EXPECT_TRUE(loser.Foes().empty()) << "an out player strikes nobody";
    EXPECT_EQ(loser.InDuel(), std::optional<u32>{1}) << "still in it until it is over";

    auto over = StateOf(1, bbcoop::DuelState::OVER, {{"ada", 1, 1, false}, {"bob", 2, 1, true}});
    over.set_end(bbcoop::DuelState::WON);
    over.set_winner("ada");
    news = loser.Take(over, "bob");
    EXPECT_TRUE(news.over && news.lost && !news.won);
    EXPECT_EQ(loser.Part(), DuelPart::None);
    news = winner.Take(over, "ada");
    EXPECT_TRUE(news.over && news.won && !news.lost);
    EXPECT_TRUE(winner.MayPropose());
    // Over once.
    EXPECT_FALSE(winner.Take(over, "ada").over);
}

// A cancel (or a duelist gone) ends it with nobody winning; a player who said no, or was never
// asked, has nothing to give back.
TEST(BloodborneDuel, CancelledAndBystanders) {
    DuelFollower duelist, decliner, bystander;
    const auto fighting =
        StateOf(2, bbcoop::DuelState::FIGHTING,
                {{"ada", 1, 1, false}, {"bob", 2, 1, false}, {"cy", 0, 2, false}});
    duelist.Take(fighting, "ada");
    decliner.Take(fighting, "cy");
    bystander.Take(fighting, "dee");
    EXPECT_EQ(decliner.Part(), DuelPart::None);
    EXPECT_EQ(bystander.Part(), DuelPart::None);
    EXPECT_FALSE(decliner.InDuel());

    auto over = fighting;
    over.set_stage(bbcoop::DuelState::OVER);
    over.set_end(bbcoop::DuelState::CANCELLED);
    over.set_ended_by("bob");
    const DuelNews news = duelist.Take(over, "ada");
    EXPECT_TRUE(news.over);
    EXPECT_FALSE(news.won || news.lost);
    EXPECT_FALSE(decliner.Take(over, "cy").over);
    EXPECT_FALSE(bystander.Take(over, "dee").over);
}

// The server's refusal reaches the player as words; an older duel's state changes nothing.
TEST(BloodborneDuel, RefusedAndStale) {
    DuelFollower follower;
    auto refused = StateOf(3, bbcoop::DuelState::FIGHTING, {{"ada", 1, 1, false}});
    refused.set_refused("A duel is already under way.");
    EXPECT_EQ(follower.Take(refused, "bob").refused, "A duel is already under way.");
    EXPECT_TRUE(follower.State().refused().empty()) << "the refusal is not the duel's";
    follower.Take(
        StateOf(3, bbcoop::DuelState::ASKING, {{"ada", 0, 1, false}, {"bob", 0, 0, false}}), "bob");
    EXPECT_FALSE(follower
                     .Take(StateOf(2, bbcoop::DuelState::ASKING,
                                   {{"cy", 0, 1, false}, {"bob", 0, 0, false}}),
                           "bob")
                     .asked);
    EXPECT_EQ(follower.State().duel(), 3u);
}

// Free for all with three: everyone still standing on another side is a foe.
TEST(BloodborneDuel, FreeForAllFoes) {
    DuelFollower follower;
    follower.Take(StateOf(4, bbcoop::DuelState::FIGHTING,
                          {{"ada", 1, 1, false}, {"bob", 2, 1, false}, {"cy", 3, 1, true}}),
                  "ada");
    EXPECT_EQ(follower.Foes(), std::vector<std::string>{"bob"});
}

// The end gives back the HP from the start and the vials and bullets used; picked-up ones stay.
TEST(BloodborneDuel, GivesBackWhatTheDuelTook) {
    EXPECT_EQ(DuelGiveBackOf({.hp = 500, .vials = 20, .bullets = 15},
                             {.hp = 1, .vials = 17, .bullets = 9}, 600),
              (DuelGiveBack{.hp = 500, .vials = 3, .bullets = 6}));
    EXPECT_EQ(DuelGiveBackOf({.hp = 500, .vials = 20, .bullets = 15},
                             {.hp = 500, .vials = 22, .bullets = 15}, 600),
              (DuelGiveBack{}))
        << "nothing used: nothing to give";
    EXPECT_EQ(DuelGiveBackOf({.hp = 700, .vials = 0, .bullets = 0},
                             {.hp = 100, .vials = 0, .bullets = 0}, 600)
                  .hp,
              std::optional<s32>{600})
        << "never past the max";
    EXPECT_EQ(DuelGiveBackOf({.hp = 0, .vials = 0, .bullets = 0},
                             {.hp = 100, .vials = 0, .bullets = 0}, 600)
                  .hp,
              std::optional<s32>{1})
        << "never dead";
}

// Which bodies are foes': all of them when only foes are here; else each body is its nearest
// member's (the nearest pairs first), within reach.
TEST(BloodborneDuel, FoeBodies) {
    const std::vector<DuelBody> bodies{{0x10, {0, 0, 0}}, {0x11, {10, 0, 0}}};
    const std::vector<std::string> bob{"bob"};
    EXPECT_EQ(DuelFoeBodies(bodies, std::vector<DuelMemberAt>{{"bob", {50, 0, 0}}}, bob),
              (std::vector<u32>{0x10, 0x11}))
        << "only the foe is here: every body is its (a stale post changes nothing)";
    EXPECT_TRUE(DuelFoeBodies(bodies, std::vector<DuelMemberAt>{{"bob", {0, 0, 0}}}, {}).empty());
    const std::vector<DuelMemberAt> two{{"cy", {1, 0, 0}}, {"bob", {9, 0, 0}}};
    EXPECT_EQ(DuelFoeBodies(bodies, two, bob), (std::vector<u32>{0x11}));
    // Both posted nearest the same body: the nearer member takes it, the other its next.
    const std::vector<DuelMemberAt> crowded{{"bob", {0.5f, 0, 0}}, {"cy", {1, 0, 0}}};
    EXPECT_EQ(DuelFoeBodies(bodies, crowded, bob), (std::vector<u32>{0x10}));
    // Out of reach of every post: no foe's.
    const std::vector<DuelMemberAt> far{{"cy", {1, 0, 0}}, {"bob", {40, 0, 0}}};
    EXPECT_TRUE(DuelFoeBodies(bodies, far, bob).empty());
}

// The end takes off the poisons the duel put on, nothing that was on before and nothing else.
TEST(BloodborneDuel, AilmentsTheDuelPutOn) {
    const std::vector<s32> before{1200, 7500, 9003};
    const std::vector<s32> now{1200, 1210, 7500, 9003, 4000, 1210};
    EXPECT_EQ(DuelAilmentsToEnd(before, now), std::vector<s32>{1210});
    EXPECT_TRUE(DuelAilmentsToEnd(now, now).empty());
}
