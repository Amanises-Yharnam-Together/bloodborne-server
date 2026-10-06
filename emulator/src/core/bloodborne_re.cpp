// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include "core/bloodborne_party.h"
#include "core/bloodborne_re.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <charconv>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <map>
#include <mutex>
#include <optional>
#include <random>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "common/logging/log.h"
#include "common/memory_patcher.h"
#include "common/path_util.h"
#include "common/singleton.h"
#include "core/cpu_patches.h"
#include "core/debugger.h"
#include "core/emulator_settings.h"
#include "core/file_format/psf.h"
#include "core/memory.h"

#include <httplib.h>

namespace Core::Bloodborne {
namespace {

enum class TraceKind : u8 {
    SosCondition,
    SosValidityDispatch,
    SosValidityCode,
    SosEvent,
    MatchingEvent,
    MatchingSignal,
    MatchingLeaveDecision,
    SummonRequest,
    SummonCandidate,
    SummonSelection,
    SummonBuild,
    GetSosRequest,
    GetSosArea,
    GetSosInsert,
    SosStatus,
    SosMessenger,
    ActionFlags,
    UseItem,
    UseItemExecution,
    GoodsActionSubmit,
    GoodsParamLookup,
    BellAvailability,
    ResponderBellAvailability,
    NetworkAreaRegion,
    MultiplayerInsert,
    CrossMapGuestHandoff,
    SosPlacement,
    GlobalState,
    ReceivedPlacementConditional,
    ReceivedPlacementSet,
    PlacementSelect,
    WarpCommand,
    CharacterWarp,
    WarpResolved,
    WarpLocalApply,
    StageWarp,
    RespawnPointWarp,
    StageWarpDescriptor,
    StageWarpAcknowledge,
    StageWarpRespawnResolve,
    StageWarpTransformResolve,
    StageWarpPlacement,
    SummonPoint,
    MapReload,
    HealingFountainRegistration,
    HealingFountainAvailability,
    ChairRespawnNotification,
    WorldStateValidation,
};

struct TraceSite {
    std::string_view name;
    u64 offset;
    TraceKind kind;
    std::array<u8, 16> prologue;
    u8 prologue_size;
};

struct NativeCallSignature {
    std::string_view name;
    u64 offset;
    std::array<u8, 16> prologue;
    u8 prologue_size;
};

constexpr std::array<u8, 16> StandardR15Prologue{0x55, 0x48, 0x89, 0xE5, 0x41, 0x57};
constexpr std::array<u8, 16> StandardR14Prologue{0x55, 0x48, 0x89, 0xE5, 0x41, 0x56};
constexpr std::array<u8, 16> StandardRbxPrologue{0x55, 0x48, 0x89, 0xE5, 0x53};
constexpr std::array<u8, 16> ReturnAndPadding{0xC3, 0x66, 0x66, 0x66, 0x66, 0x66,
                                              0x66, 0x2E, 0x0F, 0x1F, 0x84};
constexpr u64 GlobalStatePointerOffset = 0x05556678;
constexpr u64 CandidateLocalStatePointerOffset = 0x0553E878;
constexpr u64 SummonManagerRootPointerOffset = 0x0553B120; // FrpgNetMan
// GameDataMan: +0x28 "has a bloodstain", +0x30 the bloodstain record UpDateBloodMark writes.
constexpr u64 GameDataManPointerOffset = 0x0553B130;
constexpr u64 EventManPointerOffset = 0x0553B108; // SprjEventMan
// SetChrType(_, entity, type) event binding: sets +0x78 and refreshes the character (vtable 0x1A0).
constexpr u64 SetChrTypeOffset = 0x01338DF0;
constexpr u64 CurrentMapListPointerOffset = 0x0553B148;
constexpr u64 MatchingStatePointerOffset = 0x05540290;
constexpr u64 SummonSessionRulesPointerOffset = 0x0553D6D0;
constexpr u64 RoleMetadataTableOffset = 0x0553D750;
constexpr u64 SummonBuildRoleTableOffset = 0x0556E560;
constexpr u64 RoleMetadataStride = 0x80;
constexpr s32 RoleMetadataCount = 34;
constexpr u64 BeckoningAreaComparisonOffset = 0x0157F8C8;
constexpr std::array<u8, 6> BeckoningAreaComparison{0x0F, 0x87, 0x90, 0x00, 0x00, 0x00};
constexpr std::array<u8, 6> BeckoningSignedAreaComparison{0x0F, 0x8F, 0x90, 0x00, 0x00, 0x00};
constexpr u64 BeckoningAreaFlagResultOffset = 0x0157F960;
constexpr std::array<u8, 2> BeckoningAreaFlagResult{0x34, 0x01};
constexpr std::array<u8, 2> BeckoningAllowedAreaFlagResult{0x31, 0xC0};
constexpr u64 ResponderBellAreaResultOffset = 0x0157F6D1;
constexpr std::array<u8, 2> ResponderBellAreaResult{0x88, 0xC3};
constexpr std::array<u8, 2> ResponderBellAllowedAreaResult{0xB1, 0x01};
constexpr u64 ResponderBellCommonResultOffset = 0x0157F6D3;
constexpr std::array<u8, 8> ResponderBellCommonResult{0x4C, 0x89, 0xF7, 0xE8,
                                                      0xF5, 0x9F, 0x34, 0x00};
constexpr std::array<u8, 8> ResponderBellAllowedCommonResult{0xE9, 0xE9, 0x09, 0x00,
                                                             0x00, 0x90, 0x90, 0x90};
constexpr u64 ActiveBellAreaComparisonOffset = 0x015068BB;
constexpr std::array<u8, 2> ActiveBellAreaComparison{0x77, 0x4A};
constexpr std::array<u8, 2> ActiveBellSignedAreaComparison{0x7F, 0x4A};
constexpr u64 ActiveBellAreaFlagResultOffset = 0x015068FC;
constexpr std::array<u8, 5> ActiveBellAreaFlagResult{0x88, 0xC3, 0x80, 0xF3, 0x01};
constexpr std::array<u8, 5> ActiveBellAllowedAreaFlagResult{0xB3, 0x01, 0x90, 0x90, 0x90};
constexpr u64 ResponderSearchAreaRangeResultOffset = 0x0191A8C3;
constexpr std::array<u8, 6> ResponderSearchAreaRangeResult{0x18, 0xC9, 0x20, 0xC1, 0xEB, 0x02};
constexpr std::array<u8, 6> ResponderSearchSignedAreaRangeResult{0x7D, 0x04, 0x88,
                                                                 0xC1, 0xEB, 0x02};
constexpr u64 SosStatusAreaRestrictionOffset = 0x018700D3;
constexpr std::array<u8, 16> SosStatusAreaRestriction{
    0x84, 0xC0, 0x41, 0xBD, 0xFF, 0xFF, 0xFF, 0xFF, 0x44, 0x0F, 0x44, 0xEB, 0x41, 0xC1, 0xED, 0x1F,
};
constexpr std::array<u8, 16> SosStatusAllowedArea{
    0x45, 0x31, 0xED, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90, 0x90,
};
constexpr u64 SummonCandidateAreaRestrictionOffset = 0x014B714A;
constexpr std::array<u8, 6> SummonCandidateAreaRestriction{0x0F, 0x85, 0xC0, 0x03, 0x00, 0x00};
constexpr std::array<u8, 6> SummonCandidateAllowedCrossMap{0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
constexpr u64 SummonBuildAreaRestrictionOffset = 0x01874B79;
constexpr std::array<u8, 2> SummonBuildAreaRestriction{0x74, 0x0D};
constexpr std::array<u8, 2> SummonBuildAllowedCrossMap{0xEB, 0x0D};
constexpr u64 SummonBuildWorldStateRestrictionOffset = 0x018749E8;
constexpr std::array<u8, 6> SummonBuildWorldStateRestriction{
    0x0F, 0x85, 0x48, 0x01, 0x00, 0x00,
};
constexpr std::array<u8, 6> SummonBuildAllowedWorldState{0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
constexpr u64 SummonBuildNegativeEventRestrictionOffset = 0x018749F0;
constexpr std::array<u8, 6> SummonBuildNegativeEventRestriction{
    0x0F, 0x88, 0x40, 0x01, 0x00, 0x00,
};
constexpr std::array<u8, 6> SummonBuildAllowedNegativeEvent{0x90, 0x90, 0x90, 0x90, 0x90, 0x90};
constexpr u64 StageTransitionStopMatchingCallOffset = 0x019471B1;
// Same world teardown (0x019470F0), where it loads the session rules to decide whether to stop
// matching (mov rbx,[rax]; cmp [rbx+0x84],3). No RIP-relative code, so it can be hooked.
constexpr u64 WorldTeardownMatchingOffset = 0x0194716E;
// PartyGhostDeath_1's `cmp byte ptr [r14+0xC], 0` (ClearBoss), right before the menu branch that
// GhostDeathKeepSessionPatches turns into a plain jump: where a party guest revives instead.
constexpr u64 GhostDeathReviveOffset = 0x01382354;
// Event script bindings ReviveDeadPartyGuest calls.
constexpr u64 IsWhiteGhostOffset = 0x01331D80;
constexpr u64 RequestFullRecoverOffset = 0x01337550;
constexpr u64 UpDateBloodMarkOffset = 0x013308B0;
constexpr u64 RevivePlayerOffset = 0x01332B20;
// 01.09's local-player revive: refills HP and clears the death's controller locks ([+0x58]
// +0x114 bit 0 and [+0x38]+0x48 bits 0-1), then makes the player a live one (type 0).
constexpr u64 ReviveLocalCharacterOffset = 0x013179B0;
// Resolves a respawn point id to a position and rotation in the loaded blocks.
constexpr u64 ResolveRespawnPointOffset = 0x0154AFC0;
constexpr u64 LoadedBlocksPointerOffset = 0x0553B148;
// ChrResetAnimation(_, entity): sends the character's behaviour back to idle.
constexpr u64 ChrResetAnimationOffset = 0x013370F0;
// Native SoloPlayDeath_2: the local player's death respawn (bloodstain, warp to the last lamp).
constexpr u64 SoloPlayDeathRespawnOffset = 0x01381DE0;

// Sign poster timer stores (see FirstSignPostDelaySeconds / SignRepostSeconds).
struct SignTimerSite {
    u64 offset;
    u8 modrm;
    float original_seconds;
    float patched_seconds;
};
constexpr auto SignTimerSites = std::to_array<SignTimerSite>({
    {0x018A9264, 0x47, 5.0f, FirstSignPostDelaySeconds},
    {0x018A9324, 0x47, 5.0f, FirstSignPostDelaySeconds},
    {0x018A9757, 0x43, 30.0f, SignRepostSeconds},
    {0x018A988F, 0x43, 30.0f, SignRepostSeconds},
});
constexpr std::array<u8, 5> StageTransitionStopMatchingCall{0xE8, 0xEA, 0x95, 0x58, 0x00};
constexpr std::array<u8, 5> StageTransitionKeepMatchingCall{0xE8, 0xEA, 0x8E, 0x58, 0x00};
constexpr u64 SummonBuildEntryOffset = 0x01874710;
constexpr u64 SosStatusUpdateOffset = 0x01872360;
constexpr u64 StageWarpDescriptorFinalizeOffset = 0x01944F23;
constexpr u64 MultiPlayStageUidPolicyOffset = 0x0193C5DE;
constexpr u64 MultiPlayStopRequestOffset = 0x01ED07A0;
constexpr u64 MatchingStopTaskEnqueueOffset = 0x00C98070;
constexpr u64 CrossMapGuestHandoffOffset = 0x01E4FF3A;
constexpr u64 HealingFountainAvailabilityOffset = 0x012F836E;
constexpr u64 CharacterWarpSetOrCopyFloorOffset = 0x018BBA70;
constexpr auto BackgroundFloorCopyReturnOffsets = std::to_array<u64>({0x018BEE67, 0x018BEF34});
constexpr u64 OnlineStatePointerOffset = 0x056C7048;
constexpr u64 SetForcedSummonMapOffset = 0x0156CF10;
constexpr u64 SetForcedSummonPositionOffset = 0x0156CF20;
constexpr u64 SetForcedSummonOrientationOffset = 0x0156CF40;
constexpr u64 SetForcedSummonWarpOffset = 0x0156CF60;
constexpr u64 SelectSummonedPlacementOffset = 0x01332BC0;
constexpr u64 SummonedMapReloadOffset = 0x01336B90;
constexpr u64 StageTransitionOffset = 0x013CDE30;
constexpr u64 SetSummonReloadStateOffset = 0x0178D9A0;
constexpr u64 AddSpEffectOffset = 0x018C6DB0;
constexpr u64 CharacterAddSpEffectSlot = 0x3F0;
constexpr u32 ResponderResumeStableObservations = 20;
constexpr u32 ResponderResumeMaxAttempts = 2;
constexpr auto ResponderResumeRetryDelay = std::chrono::seconds{15};

constexpr auto CrossMapNativeCalls = std::to_array<NativeCallSignature>({
    {"SetForcedSummonMap",
     SetForcedSummonMapOffset,
     {0x48, 0x8B, 0x05, 0x61, 0x97, 0xFE, 0x03, 0x8B, 0x0F, 0x89, 0x88, 0xF0, 0x14, 0x00, 0x00,
      0xC3},
     16},
    {"SetForcedSummonPosition",
     SetForcedSummonPositionOffset,
     {0x48, 0x8B, 0x05, 0x51, 0x97, 0xFE, 0x03, 0xC5, 0xF8, 0x28, 0x07, 0xC5, 0xF8, 0x29, 0x80,
      0x00},
     16},
    {"SetForcedSummonOrientation",
     SetForcedSummonOrientationOffset,
     {0x48, 0x8B, 0x05, 0x31, 0x97, 0xFE, 0x03, 0xC5, 0xF8, 0x28, 0x07, 0xC5, 0xF8, 0x29, 0x80,
      0x10},
     16},
    {"SetForcedSummonWarp",
     SetForcedSummonWarpOffset,
     {0x48, 0x8B, 0x05, 0x11, 0x97, 0xFE, 0x03, 0xC6, 0x80, 0x20, 0x15, 0x00, 0x00, 0x01, 0xC3},
     15},
    {"SelectSummonedPlacement",
     SelectSummonedPlacementOffset,
     {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x53, 0x48, 0x83, 0xEC, 0x18},
     13},
    {"SummonedMapReload",
     SummonedMapReloadOffset,
     {0x55, 0x48, 0x89, 0xE5, 0x48, 0x8D, 0x05, 0x2D, 0x45, 0x20, 0x04, 0x48, 0x83, 0x38, 0x00},
     15},
    {"StageTransition",
     StageTransitionOffset,
     {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x50},
     14},
    {"SetSummonReloadState",
     SetSummonReloadStateOffset,
     {0xC7, 0x87, 0x84, 0x00, 0x00, 0x00, 0x03, 0x00, 0x00, 0x00, 0xC3},
     11},
    {"MultiPlayStopRequest",
     MultiPlayStopRequestOffset,
     {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83,
      0xEC},
     16},
    {"AddSpEffect",
     AddSpEffectOffset,
     {0x55, 0x48, 0x89, 0xE5, 0x41, 0x57, 0x41, 0x56, 0x41, 0x55, 0x41, 0x54, 0x53, 0x48, 0x83,
      0xEC},
     16},
});

constexpr auto Sites = std::to_array<TraceSite>({
    {"IsValidSos.Update", 0x01308DC0, TraceKind::SosCondition, StandardR15Prologue, 6},
    {"IsValidSos.NativeValidityDispatch",
     0x01308DF9,
     TraceKind::SosValidityDispatch,
     {0x48, 0x8B, 0x03, 0x48, 0x89, 0xDF},
     6},
    {"IsValidSos.ValidityCode",
     0x01308E20,
     TraceKind::SosValidityCode,
     {0x49, 0x63, 0xCF, 0x48, 0x69, 0xC9, 0x67, 0x66, 0x66, 0x66},
     10},
    {"IsValidSos.Condition", 0x013091B0, TraceKind::SosCondition, StandardR14Prologue, 6},
    {"Condition.SelfSos", 0x0130B030, TraceKind::SosCondition, StandardR15Prologue, 6},
    {"Condition.SOS", 0x0130C590, TraceKind::SosCondition, StandardR15Prologue, 6},
    {"Condition.SosMenu", 0x0130CDE0, TraceKind::SosCondition, StandardR15Prologue, 6},

    {"OnEvent.CallSOS.Dispatch",
     0x01389A3B,
     TraceKind::SosEvent,
     {0xBE, 0xDA, 0x0F, 0x00, 0x00, 0xBA, 0x01, 0x00, 0x00, 0x00},
     10},
    {"OnEvent.CallBlackSOS.Dispatch",
     0x01389A6B,
     TraceKind::SosEvent,
     {0xBE, 0xDA, 0x0F, 0x00, 0x00, 0xBA, 0x02, 0x00, 0x00, 0x00},
     10},
    {"OnEvent.CallDragonewtSOS.Dispatch",
     0x01389A9B,
     TraceKind::SosEvent,
     {0xBE, 0xDA, 0x0F, 0x00, 0x00, 0xBA, 0x03, 0x00, 0x00, 0x00},
     10},
    {"Call.WhiteSos.Dispatch",
     0x01389AE2,
     TraceKind::SosEvent,
     {0x31, 0xF6, 0xBA, 0xEA, 0x22, 0x02, 0x00},
     7},
    {"Call.BlackSos.Dispatch",
     0x01389B22,
     TraceKind::SosEvent,
     {0x31, 0xF6, 0xBA, 0xEB, 0x22, 0x02, 0x00},
     7},

    {"OnMatchingCheck", 0x013808B0, TraceKind::MatchingEvent, StandardR15Prologue, 6},
    {"OnMatchingError", 0x013809F0, TraceKind::MatchingEvent, StandardR15Prologue, 6},
    {"OnSummonResult.Empty", 0x01384500, TraceKind::MatchingSignal, ReturnAndPadding, 16},
    {"OnSummonResult.Move", 0x01384510, TraceKind::MatchingSignal, ReturnAndPadding, 16},
    {"OnSummonResult.TimeOut", 0x01384520, TraceKind::MatchingSignal, ReturnAndPadding, 16},
    {"OnSummonResult.OtherError", 0x01384530, TraceKind::MatchingEvent, StandardRbxPrologue, 5},
    {"ForceSummonFail", 0x01384990, TraceKind::MatchingSignal, ReturnAndPadding, 16},
    {"ForceSummonSuccess", 0x013849A0, TraceKind::MatchingEvent, StandardR15Prologue, 6},

    {"CSMultiPlayMan.StageUidChanged.Policy",
     MultiPlayStageUidPolicyOffset,
     TraceKind::MatchingLeaveDecision,
     {0x8B, 0x87, 0x24, 0x01, 0x00, 0x00},
     6},
    {"CSMultiPlayMan.StopRequest", MultiPlayStopRequestOffset, TraceKind::MatchingLeaveDecision,
     StandardR15Prologue, 6},
    {"Matching2.StopTask.Enqueue", MatchingStopTaskEnqueueOffset, TraceKind::MatchingLeaveDecision,
     StandardR15Prologue, 6},
    {"Matching2.LeaveRoom.Begin",
     0x00CC5390,
     TraceKind::MatchingLeaveDecision,
     {0x55, 0x48, 0x89, 0xE5, 0x53, 0x50},
     6},

    {"CSRequestSummon.Init", 0x014B47C0, TraceKind::SummonRequest, StandardR15Prologue, 6},
    {"CSRequestSummon.Update", 0x014B4890, TraceKind::SummonRequest, StandardRbxPrologue, 5},
    {"CSRequestSummon.CheckWait", 0x014B48C0, TraceKind::SummonRequest, StandardR15Prologue, 6},
    {"CSRequestSummon.CheckComplete",
     0x014B4AE0,
     TraceKind::SummonRequest,
     {0xC7, 0x47, 0x54, 0xFF, 0xFF, 0xFF, 0xFF},
     7},

    {"SummonCandidate.Entry", 0x014BA980, TraceKind::SummonCandidate, StandardR15Prologue, 6},
    {"SummonCandidate.Accepted",
     0x014BAA5C,
     TraceKind::SummonCandidate,
     {0x48, 0x8B, 0x07, 0xBE, 0xF8, 0x00, 0x00, 0x00},
     8},
    {"SummonCandidate.Submit",
     0x014BABCB,
     TraceKind::SummonCandidate,
     {0x48, 0x8B, 0xB3, 0xE8, 0x01, 0x00, 0x00},
     7},
    {"SummonCandidate.FilterCycle",
     0x014B6F09,
     TraceKind::SummonCandidate,
     {0x41, 0x83, 0xFF, 0xFF, 0x4C, 0x89, 0xA4, 0x24, 0x88, 0x00, 0x00, 0x00},
     12},
    {"SummonCandidate.ManagerScan",
     0x014B6F5D,
     TraceKind::SummonCandidate,
     {0x49, 0x8D, 0x7C, 0x24, 0x20},
     5},
    {"SummonCandidate.LocalIdentityPassed",
     0x014B6F9D,
     TraceKind::SummonCandidate,
     {0x4C, 0x8B, 0x28, 0x4D, 0x85, 0xED},
     6},
    {"SummonCandidate.ManagerIdentityStatePassed",
     0x014B6FE1,
     TraceKind::SummonCandidate,
     {0x48, 0x89, 0xCB, 0x4D, 0x89, 0xC6},
     6},
    {"SummonCandidate.ManagerIdentityPassed",
     0x014B7070,
     TraceKind::SummonCandidate,
     {0x41, 0x83, 0xBD, 0xC0, 0x0A, 0x00, 0x00, 0x00},
     8},
    {"SummonCandidate.SessionGatePassed",
     0x014B70FF,
     TraceKind::SummonCandidate,
     {0x41, 0xF6, 0x85, 0x14, 0x0B, 0x00, 0x00, 0x02},
     8},
    {"SummonCandidate.AreaGatePassed",
     0x014B7150,
     TraceKind::SummonCandidate,
     {0x48, 0x89, 0xDF, 0x48, 0x8B, 0x84, 0x24, 0xA8, 0x00, 0x00, 0x00},
     11},
    {"SummonCandidate.NoPendingDuplicate",
     0x014B71B2,
     TraceKind::SummonCandidate,
     {0x41, 0x0F, 0xB6, 0x84, 0x24, 0xED, 0x00, 0x00, 0x00},
     9},
    {"SummonCandidate.FreshnessPassed",
     0x014B71F4,
     TraceKind::SummonCandidate,
     {0x48, 0x8B, 0x38, 0x48, 0x8B, 0x07},
     6},
    {"SummonCandidate.Inserted",
     0x014B8E8D,
     TraceKind::SummonCandidate,
     {0x48, 0x8B, 0xB2, 0xE8, 0x01, 0x00, 0x00},
     7},
    {"SummonCandidate.PendingInserted",
     0x014B750B,
     TraceKind::SummonCandidate,
     {0x0F, 0x1F, 0x44, 0x00, 0x00},
     5},
    {"SummonCandidate.ObserverResult",
     0x014C0238,
     TraceKind::SummonCandidate,
     {0x45, 0x31, 0xFF, 0x84, 0xC0, 0x4D, 0x0F, 0x45, 0xFC},
     9},

    {"SummonSelection.Scan",
     0x01873269,
     TraceKind::SummonSelection,
     {0x48, 0x8B, 0x80, 0xA8, 0x01, 0x00, 0x00},
     7},
    {"SummonSelection.Matched",
     0x018732DD,
     TraceKind::SummonSelection,
     {0xC6, 0x46, 0x10, 0x01, 0x4C, 0x8B, 0x76, 0x08},
     8},
    {"SummonBuild.Entry", SummonBuildEntryOffset, TraceKind::SummonBuild, StandardR15Prologue, 6},
    {"SummonBuild.RoleDelayPassed",
     0x0187477A,
     TraceKind::SummonBuild,
     {0x49, 0x83, 0x7D, 0x00, 0x00},
     5},
    {"SummonBuild.SessionStatePassed",
     0x0187481D,
     TraceKind::SummonBuild,
     {0x48, 0x8B, 0x18, 0x48, 0x85, 0xDB},
     6},
    {"SummonBuild.ControlAndWorldStatePassed",
     0x01874B8F,
     TraceKind::SummonBuild,
     {0x48, 0x8B, 0x90, 0xF8, 0x16, 0x00, 0x00},
     7},
    {"SummonBuild.GlobalCapacityPassed",
     0x01874BC7,
     TraceKind::SummonBuild,
     {0x4D, 0x89, 0xC7, 0x49, 0xC1, 0xE3, 0x07},
     7},
    {"SummonBuild.RoleCapacityPassed",
     0x01874C94,
     TraceKind::SummonBuild,
     {0x43, 0x8B, 0x04, 0x1E, 0x83, 0xF8, 0x02},
     7},
    {"SummonBuild.RoleRules",
     0x01874E5A,
     TraceKind::SummonBuild,
     {0x49, 0x89, 0xCD, 0x4F, 0x8D, 0x7C, 0x1E, 0x14},
     8},
    {"SummonBuild.Return",
     0x018750F7,
     TraceKind::SummonBuild,
     {0x49, 0x8B, 0x06, 0x48, 0x3B, 0x45, 0xD0},
     7},
    {"SummonSelection.QueueInsert", 0x01875320, TraceKind::SummonSelection, StandardR15Prologue, 6},
    {"SummonSelection.RequestQueued",
     0x01873605,
     TraceKind::SummonSelection,
     {0x48, 0x8B, 0x83, 0xC0, 0x01, 0x00, 0x00},
     7},
    {"SummonSelection.RequestStart", 0x014BAEC0, TraceKind::SummonSelection, StandardR15Prologue,
     6},
    {"SummonSelection.HandleAllocated",
     0x014BAFE3,
     TraceKind::SummonSelection,
     {0x49, 0x89, 0x84, 0x24, 0x38, 0x01, 0x00, 0x00},
     8},

    {"CSRequestGetSos.Init", 0x01E5FAF0, TraceKind::GetSosRequest, StandardR15Prologue, 6},
    {"CSRequestGetSos.UpdateSeamless", 0x01E5FBF0, TraceKind::GetSosRequest, StandardR14Prologue,
     6},
    {"CSRequestGetSos.BuildRequest", 0x01E5FCE0, TraceKind::GetSosRequest, StandardR15Prologue, 6},
    {"CSRequestGetSos.SelectedArea",
     0x01E60270,
     TraceKind::GetSosArea,
     {0x8B, 0x02, 0x89, 0x85, 0xE8, 0xFE, 0xFF, 0xFF},
     8},
    {"CSRequestGetSos.InsertType", 0x01E5F970, TraceKind::GetSosInsert, StandardR15Prologue, 6},
    {"SosStatus.Update", SosStatusUpdateOffset, TraceKind::SosStatus, StandardR15Prologue, 6},
    {"SosStatus.NativeResult",
     0x0187239E,
     TraceKind::SosStatus,
     {0x83, 0xBD, 0x64, 0xFF, 0xFF, 0xFF, 0x00},
     7},

    {"Messenger.SosSign.Init", 0x01CC9460, TraceKind::SosMessenger, StandardRbxPrologue, 5},
    {"Messenger.SosSign.Wait", 0x01CC94B0, TraceKind::SosMessenger, StandardRbxPrologue, 5},
    {"Messenger.SosSign.Finish", 0x01CC94E0, TraceKind::SosMessenger, StandardR15Prologue, 6},

    {"ChrAction.Flags.Update",
     0x01A0F910,
     TraceKind::ActionFlags,
     {0x48, 0x8B, 0x47, 0x10, 0x48, 0x8B, 0x4F, 0x18},
     8},
    {"ChrAction.UseItem.Pressed",
     0x018F751C,
     TraceKind::UseItem,
     {0x49, 0x8B, 0x45, 0x00, 0x4C, 0x89, 0xEF},
     7},
    {"ChrAction.UseItem.Inventory",
     0x018F7529,
     TraceKind::UseItem,
     {0x48, 0x8D, 0xB8, 0xD0, 0x01, 0x00, 0x00},
     7},
    {"ChrAction.UseItem.ResolvedGoods",
     0x018F7535,
     TraceKind::UseItem,
     {0x41, 0x8B, 0x8D, 0x7C, 0x04, 0x00, 0x00},
     7},
    {"ChrAction.UseItem.NativeApply", 0x018F9720, TraceKind::UseItemExecution, StandardR15Prologue,
     6},
    {"ChrAction.UseItem.GoodsExecutor", 0x018C9160, TraceKind::UseItemExecution,
     StandardR15Prologue, 6},
    {"ChrAction.UseItem.GoodsExecutorResult",
     0x018C9364,
     TraceKind::UseItemExecution,
     {0x48, 0x8B, 0x03, 0x48, 0x3B, 0x45, 0xD0},
     7},
    {"ChrAction.UseItem.ActionSubmit", 0x01FF0FA0, TraceKind::GoodsActionSubmit,
     StandardR15Prologue, 6},
    {"GameParam.Goods.Lookup", 0x01F1E3D0, TraceKind::GoodsParamLookup, StandardR15Prologue, 6},
    {"Beckoning.Availability.Area",
     0x0157F8BA,
     TraceKind::BellAvailability,
     {0x84, 0xC0, 0x48, 0x0F, 0x45, 0xD9},
     6},
    {"Beckoning.Availability.NativeResult",
     0x0157F962,
     TraceKind::BellAvailability,
     {0x89, 0x85, 0x10, 0xFE, 0xFF, 0xFF},
     6},
    {"ResponderBell.Availability.Entry",
     0x0157F686,
     TraceKind::ResponderBellAvailability,
     {0x41, 0x81, 0xFC, 0xCD, 0x00, 0x00, 0x00},
     7},
    {"ResponderBell.Availability.FinalResult",
     0x015800D1,
     TraceKind::ResponderBellAvailability,
     {0x88, 0xC8, 0x48, 0x81, 0xC4, 0xE8, 0x01, 0x00, 0x00},
     9},
    {"Network.Area.RegionResult",
     0x013DEAC8,
     TraceKind::NetworkAreaRegion,
     {0x45, 0x89, 0x7E, 0x44, 0xB0, 0x01},
     6},

    {"CSMultiPlayerIns.Init", 0x01E4FCF0, TraceKind::MultiplayerInsert, StandardRbxPrologue, 5},
    {"CSMultiPlayerIns.StartNotifyWait", 0x01E4FD50, TraceKind::MultiplayerInsert,
     StandardR15Prologue, 6},
    {"CSMultiPlayerIns.StartWait", 0x01E4FF70, TraceKind::MultiplayerInsert, StandardR14Prologue,
     6},
    {"CSMultiPlayerIns.FirstSyncWait", 0x01E50090, TraceKind::MultiplayerInsert,
     StandardR15Prologue, 6},
    {"CSMultiPlayerIns.Create", 0x01E505E0, TraceKind::MultiplayerInsert, StandardR15Prologue, 6},
    {"CSMultiPlayerIns.CreateWait", 0x01E50830, TraceKind::MultiplayerInsert, StandardRbxPrologue,
     5},
    {"CSMultiPlayerIns.Update",
     0x01E50860,
     TraceKind::MultiplayerInsert,
     {0x83, 0xBF, 0xE8, 0x00, 0x00, 0x00, 0x00},
     7},
    {"CSMultiPlayerIns.WaitExit", 0x01E50880, TraceKind::MultiplayerInsert, StandardR15Prologue, 6},
    {"CSMultiPlayerIns.Delete", 0x01E50A70, TraceKind::MultiplayerInsert, StandardR15Prologue, 6},
    {"CSMultiPlayerIns.DeleteWait",
     0x01E50D70,
     TraceKind::MultiplayerInsert,
     {0x8B, 0x87, 0xE8, 0x00, 0x00, 0x00, 0x00},
     6},
    {"CSMultiPlayerIns.PlacementCopy",
     0x01E4FF1E,
     TraceKind::GlobalState,
     {0x89, 0x88, 0xC4, 0x14, 0x00, 0x00},
     6},
    {"CSMultiPlayerIns.CrossMapGuestHandoff",
     CrossMapGuestHandoffOffset,
     TraceKind::CrossMapGuestHandoff,
     {0x48, 0x8D, 0x83, 0xED, 0x00, 0x00, 0x00},
     7},

    {"SetSosSignWarp",
     0x0132E267,
     TraceKind::GlobalState,
     {0xC6, 0x80, 0x20, 0x15, 0x00, 0x00, 0x01},
     7},
    {"SetSummonedPos",
     0x01332BB7,
     TraceKind::GlobalState,
     {0xC6, 0x40, 0x20, 0x01, 0xB0, 0x01, 0x00},
     6},
    {"SetSosSignPos.NativeCopy",
     0x01333C43,
     TraceKind::SosPlacement,
     {0x89, 0x88, 0xC0, 0x14, 0x00, 0x00},
     6},
    {"SummonedPlacement.SelectMap",
     0x01332C1B,
     TraceKind::PlacementSelect,
     {0x80, 0xB9, 0x20, 0x15, 0x00, 0x00, 0x00},
     7},
    {"SummonedPlacement.SelectedMap",
     0x01332CD0,
     TraceKind::PlacementSelect,
     {0x89, 0x41, 0x0C, 0x48, 0x8B, 0x03},
     6},
    {"SummonedMapReload.Native", 0x0131E5C0, TraceKind::MapReload, StandardR15Prologue, 6},
    {"SummonPoint.Resolve", 0x01582E70, TraceKind::SummonPoint, StandardR15Prologue, 6},
    {"HealingFountain.Register.Native", 0x0133B030, TraceKind::HealingFountainRegistration,
     StandardR15Prologue, 6},
    {"WorldTeardown.MatchingDecision",
     WorldTeardownMatchingOffset,
     TraceKind::MatchingLeaveDecision,
     {0x48, 0x8B, 0x18, 0x83, 0xBB, 0x84, 0x00, 0x00, 0x00, 0x03},
     10},
    {"SoloPlayDeath2.Respawn", SoloPlayDeathRespawnOffset, TraceKind::MatchingSignal,
     StandardR15Prologue, 6},
    {"PartyGhostDeath1.Revive",
     GhostDeathReviveOffset,
     TraceKind::MatchingSignal,
     {0x41, 0x80, 0x7E, 0x0C, 0x00},
     5},
    {"HealingFountain.Availability.Host",
     HealingFountainAvailabilityOffset,
     TraceKind::HealingFountainAvailability,
     {0x41, 0x80, 0x7D, 0x48, 0x00},
     5},
    {"ChairMessenger.RespawnPointNotify.Init", 0x01E5C6C0, TraceKind::ChairRespawnNotification,
     StandardR15Prologue, 6},
    {"ChairMessenger.RespawnPointNotify.Update", 0x01E5C890, TraceKind::ChairRespawnNotification,
     StandardR15Prologue, 6},

    {"ReceivedSummonPlacement.ConditionalSet",
     0x0156E6E7,
     TraceKind::ReceivedPlacementConditional,
     {0x41, 0x80, 0xB8, 0x48, 0x16, 0x00, 0x00, 0x00},
     8},
    {"ReceivedSummonPlacement.Set",
     0x0156E7A7,
     TraceKind::ReceivedPlacementSet,
     {0x8B, 0x12, 0x8B, 0x09, 0xC5, 0xF8, 0x28, 0x07},
     8},

    {"CharacterWarp.Area", 0x013CB3D0, TraceKind::CharacterWarp, StandardR15Prologue, 6},
    {"CharacterWarp.Object", 0x013CB870, TraceKind::CharacterWarp, StandardR15Prologue, 6},
    {"CharacterWarp.Character", 0x013CCBD0, TraceKind::CharacterWarp, StandardR15Prologue, 6},
    {"CharacterWarp.SetOrCopyFloor", 0x018BBA70, TraceKind::CharacterWarp, StandardR15Prologue, 6},

    {"Event.WarpNextStage",
     0x0132E010,
     TraceKind::StageWarp,
     {0x55, 0x48, 0x89, 0xE5, 0xC1, 0xE6},
     6},
    {"Event.WarpNextStage_Bonfire",
     0x0132E050,
     TraceKind::StageWarp,
     {0x55, 0x48, 0x89, 0xE5, 0x48, 0x63, 0xC6},
     7},
    {"Warp.RespawnPointParam", 0x013CDF30, TraceKind::RespawnPointWarp, StandardR15Prologue, 6},
    {"Warp.StageTransition", 0x013CDE30, TraceKind::StageWarp, StandardR15Prologue, 6},
    {"Warp.StageDescriptor",
     0x01944D76,
     TraceKind::StageWarpDescriptor,
     {0x8B, 0x88, 0x3C, 0x15, 0x00, 0x00},
     6},
    {"Warp.StageDescriptor.Finalize",
     StageWarpDescriptorFinalizeOffset,
     TraceKind::StageWarpDescriptor,
     {0xB8, 0x00, 0x00, 0x00, 0xFF},
     5},
    {"Warp.StageTransition.AcknowledgeCheck",
     0x0194100A,
     TraceKind::StageWarpAcknowledge,
     {0x80, 0xBD, 0x68, 0xFE, 0xFF, 0xFF, 0x00},
     7},
    {"Warp.RespawnPoint.Resolve",
     0x01938F65,
     TraceKind::StageWarpRespawnResolve,
     {0x41, 0x8B, 0x85, 0xA4, 0x00, 0x00, 0x00},
     7},
    {"Warp.RespawnTransform.Resolve",
     0x0154AFC0,
     TraceKind::StageWarpTransformResolve,
     {0x55, 0x53, 0x83, 0xF9, 0xFF, 0x0F, 0x84, 0x9D, 0x00, 0x00, 0x00},
     11},
    {"Warp.RespawnPlacement.Apply", 0x01939C80, TraceKind::StageWarpPlacement, StandardR14Prologue,
     6},

    {"PlayerWarp.Dispatch.RemoteWarpPlayer.Result", 0x01FB2BE0, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.RemoteResetPlayer.Result", 0x01FB2C60, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.GetPlayerInfo.Result", 0x01FB2CE0, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.RemoteWarpWorldPos.Result", 0x01FB2D60, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.RemoteWarpMapStudioPos.Result", 0x01FB2DE0, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.RemoteWarpPlayer.Void", 0x01FB2E60, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.RemoteResetPlayer.Void", 0x01FB2EE0, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.GetPlayerInfo.Void", 0x01FB2F60, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.RemoteWarpWorldPos.Void", 0x01FB2FE0, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Dispatch.RemoteWarpMapStudioPos.Void", 0x01FB3060, TraceKind::WarpCommand,
     StandardR15Prologue, 6},

    {"PlayerWarp.Executor.RemoteWarpPlayer", 0x0154D180, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Executor.RemoteResetPlayer", 0x0154D5E0, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Executor.GetPlayerInfo", 0x0154D7A0, TraceKind::WarpCommand, StandardR15Prologue,
     6},
    {"PlayerWarp.Executor.RemoteWarpWorldPos", 0x0154DC50, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.Executor.RemoteWarpMapStudioPos", 0x0154E0B0, TraceKind::WarpCommand,
     StandardR15Prologue, 6},
    {"PlayerWarp.NativeDispatch", 0x0154EA30, TraceKind::WarpResolved, StandardR15Prologue, 6},
    {"PlayerWarp.LocalApply", 0x0154B110, TraceKind::WarpLocalApply, StandardR15Prologue, 6},

    {"WorldStateValidation.018C5360.Result",
     0x018C5365,
     TraceKind::WorldStateValidation,
     {0xBE, 0xFF, 0xFF, 0xFF, 0xFF},
     5},
    {"WorldStateValidation.018CF975.Result",
     0x018CF97A,
     TraceKind::WorldStateValidation,
     {0x41, 0xBE, 0xFF, 0xFF, 0xFF, 0xFF},
     6},
    {"WorldStateValidation.0191A968.Return",
     0x0191A980,
     TraceKind::WorldStateValidation,
     {0x48, 0x83, 0xC4, 0x18, 0x5B, 0x41, 0x5E, 0x41, 0x5F, 0x5D, 0xC3},
     11},
    {"WorldStateValidation.0191AA4C.Return",
     0x0191AA71,
     TraceKind::WorldStateValidation,
     {0x48, 0x83, 0xC4, 0x18, 0x5B, 0x41, 0x5E, 0x41, 0x5F, 0x5D, 0xC3},
     11},
    {"WorldStateValidation.01AC6543.Return",
     0x01AC6557,
     TraceKind::WorldStateValidation,
     {0x48, 0x83, 0xC4, 0x18, 0x5B, 0x41, 0x5E, 0x41, 0x5F, 0x5D, 0xC3},
     11},
    {"WorldStateValidation.01AC65F4.Return",
     0x01AC661E,
     TraceKind::WorldStateValidation,
     {0x48, 0x83, 0xC4, 0x18, 0x5B, 0x41, 0x5E, 0x41, 0x5F, 0x5D, 0xC3},
     11},
    {"WorldStateValidation.01FEB60C.Result",
     0x01FEB611,
     TraceKind::WorldStateValidation,
     {0x44, 0x89, 0xE1, 0x41, 0x88, 0xC4},
     6},
    {"WorldStateValidation.01FEBCFC.Result",
     0x01FEBD01,
     TraceKind::WorldStateValidation,
     {0x44, 0x89, 0xE1, 0x41, 0x88, 0xC4},
     6},
    {"WorldStateValidation.01FEC37C.Result",
     0x01FEC381,
     TraceKind::WorldStateValidation,
     {0x44, 0x89, 0xE1, 0x41, 0x88, 0xC4},
     6},
});

std::array<std::atomic<u64>, Sites.size()> site_hits{};
std::atomic<u64> event_sequence{};
std::atomic<s32> observed_sos_area{-1};
std::mutex capture_mutex;
std::ofstream capture_file;
std::filesystem::path capture_path;
uintptr_t image_base{};
bool installed{};
bool negative_area_patch_installed{};
bool area_flag_patch_installed{};
bool responder_bell_area_patch_installed{};
bool responder_bell_common_patch_installed{};
bool active_bell_negative_area_patch_installed{};
bool active_bell_area_flag_patch_installed{};
bool responder_search_negative_area_patch_installed{};
bool sos_status_area_patch_installed{};
bool summon_candidate_area_patch_installed{};
bool summon_build_area_patch_installed{};
bool summon_build_world_state_patch_installed{};
bool summon_build_negative_event_patch_installed{};
bool stage_transition_keep_matching_patch_installed{};
bool world_teardown_hook_installed{};
bool ghost_death_revive_hook_installed{};
bool seamless_travel_hooks_installed{};
bool stop_request_hook_installed{};
bool host_death_respawn_hook_installed{};
bool summon_build_host_placement_hook_installed{};
bool cross_map_guest_handoff_hook_installed{};
bool summon_reload_state_hook_installed{};
bool deferred_summon_reload_hook_installed{};
bool healing_fountain_host_availability_hook_installed{};
thread_local u64 responder_availability_frame{};
thread_local s32 responder_availability_goods{-1};

struct SummonPlacementDescriptor {
    u32 packed_region{};
    float x{};
    float y{};
    float z{};
    float heading{};
    s32 area{};
};
static_assert(sizeof(SummonPlacementDescriptor) == 0x18);

struct HostPlacementRewriteRecord {
    std::string_view result{"not_attempted"};
    u64 prepared{};
    SummonPlacementDescriptor candidate{};
    SummonPlacementDescriptor host{};
};

thread_local HostPlacementRewriteRecord host_placement_rewrite{};
std::mutex seamless_placement_mutex;
std::optional<SummonPlacementDescriptor> seamless_host_placement;
std::optional<SummonPlacementDescriptor> seamless_received_host_placement;
bool seamless_received_host_placement_consumed{};
std::atomic<u32> pending_summon_reload_map{};
std::atomic<u32> pending_summon_reload_state_map{};

struct GuestPlacementHandoffRecord {
    std::string_view result{"not_attempted"};
    u64 object{};
    u64 state{};
    u32 current_map{};
    u32 received_map{};
    u32 selected_map{};
    bool transported_host{};
    bool placement_refreshed{};
    bool select_result{};
    bool summon_reload_armed{};
    s32 transition_result{};
};

thread_local GuestPlacementHandoffRecord guest_placement_handoff{};

struct SummonReloadStateRecord {
    std::string_view result{"not_pending"};
    u32 target_map{};
    u32 descriptor_map{};
    u64 reload_state{};
    s32 state_before{};
    s32 state_after{};
    bool summon_reload_started{};
    bool state_setter_used{};
    bool placement_refreshed{};
};

thread_local SummonReloadStateRecord summon_reload_state{};

struct DeferredSummonReloadRecord {
    std::string_view result{"not_pending"};
    u32 target_map{};
    u32 current_map{};
    bool summon_reload_started{};
};

thread_local DeferredSummonReloadRecord deferred_summon_reload{};

struct PreMatchGuestWarpRecord {
    std::string_view result{"not_pending"};
    u64 state{};
    u32 current_map{};
    u32 target_map{};
    u32 selected_map{};
    s32 multi_play_state{-1};
    bool placement_written{};
    bool select_result{};
    bool responder_resume_armed{};
    s32 transition_result{};
};

thread_local PreMatchGuestWarpRecord pre_match_guest_warp{};

struct PendingResponderResume {
    u32 target_map{};
    s32 target_area{};
    u32 ready_observations{};
    u32 attempts{};
    bool dispatched{};
    std::chrono::steady_clock::time_point dispatched_at{};
};

thread_local PendingResponderResume pending_responder_resume{};

struct ResponderResumeRecord {
    std::string_view result{"not_pending"};
    u32 target_map{};
    u32 current_map{};
    s32 target_area{};
    s32 current_area{};
    u64 state{};
    u64 multi_play{};
    s32 multi_play_state{-1};
    u64 matching_controller{};
    u64 player{};
    u32 ready_observations{};
    u32 attempts{};
    bool effect_active{};
    bool native_result{};
};

thread_local ResponderResumeRecord responder_resume{};

struct HealingFountainAvailabilityRecord {
    std::string_view result{"not_attempted"};
    u64 object{};
    u64 player{};
    bool blocked_before{};
    bool host_effect{};
    bool blocked_after{};
};

thread_local HealingFountainAvailabilityRecord healing_fountain_availability{};

bool EnvFlagEnabled(const char* name) {
    const char* value = std::getenv(name);
    return value != nullptr && value[0] != '\0' && std::string_view{value} != "0";
}

std::string BytesToHex(std::span<const u8> bytes) {
    std::ostringstream out;
    out << std::hex << std::setfill('0');
    for (const u8 byte : bytes) {
        out << std::setw(2) << static_cast<u32>(byte);
    }
    return out.str();
}

bool HasMemoryAccess(u64 address, size_t size, MemoryProt required) {
    if (size == 0 || address > std::numeric_limits<u64>::max() - size) {
        return false;
    }

    auto* memory = Memory::Instance();
    if (memory == nullptr || !memory->IsValidMapping(address, size)) {
        return false;
    }

    void* mapping_start{};
    void* mapping_end{};
    u32 protection{};
    const u32 required_bits = static_cast<u32>(required);
    return memory->QueryProtection(address, &mapping_start, &mapping_end, &protection) == 0 &&
           (protection & required_bits) == required_bits &&
           address >= reinterpret_cast<u64>(mapping_start) &&
           address + size <= reinterpret_cast<u64>(mapping_end);
}

template <typename T>
T ReadValue(u64 address, size_t offset) {
    T value{};
    if (address > std::numeric_limits<u64>::max() - offset) {
        return value;
    }
    const u64 source = address + offset;
    if (source > std::numeric_limits<u64>::max() - sizeof(value)) {
        return value;
    }

    if (!HasMemoryAccess(source, sizeof(value), MemoryProt::CpuRead)) {
        return value;
    }

    std::memcpy(&value, reinterpret_cast<const void*>(source), sizeof(value));
    return value;
}

template <typename T>
bool WriteValue(u64 address, size_t offset, const T& value) {
    if (address > std::numeric_limits<u64>::max() - offset) {
        return false;
    }
    const u64 destination = address + offset;
    if (!HasMemoryAccess(destination, sizeof(value), MemoryProt::CpuWrite)) {
        return false;
    }

    std::memcpy(reinterpret_cast<void*>(destination), &value, sizeof(value));
    return true;
}

bool WriteTransportedSummonPlacement(u64 state, const SummonPlacementDescriptor& placement) {
    const std::array<float, 4> position{placement.x, placement.y, placement.z, 1.0F};
    const std::array<float, 4> orientation{0.0F, placement.heading, 0.0F, 0.0F};
    return WriteValue(state, 0x14C4, placement.packed_region) &&
           WriteValue(state, 0x14D0, position) && WriteValue(state, 0x14E0, orientation);
}

bool IsUsablePackedMap(u32 packed_region) {
    return packed_region != 0 && packed_region != std::numeric_limits<u32>::max();
}

bool IsSameSummonPlacement(const SummonPlacementDescriptor& left,
                           const SummonPlacementDescriptor& right) {
    return left.packed_region == right.packed_region &&
           std::bit_cast<u32>(left.x) == std::bit_cast<u32>(right.x) &&
           std::bit_cast<u32>(left.y) == std::bit_cast<u32>(right.y) &&
           std::bit_cast<u32>(left.z) == std::bit_cast<u32>(right.z) &&
           std::bit_cast<u32>(left.heading) == std::bit_cast<u32>(right.heading) &&
           left.area == right.area;
}

u64 GetSummonManagerRoot();
u64 GetGlobalState();
u64 GetMatchingState();

u64 GetLocalPlayer() {
    const u64 local_state =
        image_base != 0 ? ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0) : 0;
    return local_state >= 0x10000 ? ReadValue<u64>(local_state, 0x60) : 0;
}

bool HasPlayerEffect(u64 player, s32 effect_id) {
    const u64 manager = player >= 0x10000 ? ReadValue<u64>(player, 0x1C8) : 0;
    if (manager < 0x10000) {
        return false;
    }

    u64 node = ReadValue<u64>(manager, 0x08);
    for (size_t index = 0; node >= 0x10000 && index < 512; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        const u32 flags = ReadValue<u32>(node, 0x1C);
        const u64 param = ReadValue<u64>(node, 0x48);
        if ((flags & 0x800C0003U) == 0 && param >= 0x10000 &&
            ReadValue<s32>(node, 0x40) == effect_id) {
            return true;
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    return false;
}

u32 GetCurrentPackedMap() {
    const u64 map_list =
        image_base != 0 ? ReadValue<u64>(image_base + CurrentMapListPointerOffset, 0) : 0;
    if (map_list < 0x10000 || !HasMemoryAccess(map_list, 0x24, MemoryProt::CpuRead)) {
        return 0;
    }

    const s32 index = ReadValue<s32>(map_list, 0x20);
    const u64 map_array = ReadValue<u64>(map_list, 0x10);
    if (index < 0 || map_array < 0x10000 ||
        !HasMemoryAccess(map_array, 0x28, MemoryProt::CpuRead)) {
        return 0;
    }

    const s32 count = ReadValue<s32>(map_array, 0x18);
    const u64 entries = ReadValue<u64>(map_array, 0x20);
    if (index >= count || entries < 0x10000) {
        return 0;
    }

    constexpr u64 MapEntryStride = 0xA0;
    const u64 entry_offset = static_cast<u64>(index) * MapEntryStride;
    if (entry_offset > std::numeric_limits<u64>::max() - entries) {
        return 0;
    }
    const u64 entry = entries + entry_offset;
    if (entry > std::numeric_limits<u64>::max() - sizeof(u64)) {
        return 0;
    }
    return ReadValue<u32>(entry, sizeof(u64));
}

bool ReadLocalSummonPlacement(SummonPlacementDescriptor& placement, std::string_view& result) {
    const u64 manager_root = GetSummonManagerRoot();
    if (manager_root < 0x10000 ||
        !HasMemoryAccess(manager_root, 0xA80 + sizeof(s32), MemoryProt::CpuRead)) {
        result = "invalid_manager";
        return false;
    }

    placement.packed_region = GetCurrentPackedMap();
    placement.area = ReadValue<s32>(manager_root, 0xA80);
    if (!IsUsablePackedMap(placement.packed_region)) {
        result = "invalid_map";
        return false;
    }

    const u64 local_state = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const u64 local_world = local_state >= 0x10000 ? ReadValue<u64>(local_state, 0x60) : 0;
    const u64 character = local_world >= 0x10000 ? ReadValue<u64>(local_world, 0x58) : 0;
    const u64 character_state = character >= 0x10000 ? ReadValue<u64>(character, 0x08) : 0;
    const u64 character_model =
        character_state >= 0x10000 ? ReadValue<u64>(character_state, 0x3B0) : 0;
    const u64 transform = character_model >= 0x10000 ? ReadValue<u64>(character_model, 0x68) : 0;
    if (transform < 0x10000 || !HasMemoryAccess(transform, 0x1EC, MemoryProt::CpuRead)) {
        result = "invalid_transform";
        return false;
    }

    placement.heading = ReadValue<float>(transform, 0x1D4);
    placement.x = ReadValue<float>(transform, 0x1E0);
    placement.y = ReadValue<float>(transform, 0x1E4);
    placement.z = ReadValue<float>(transform, 0x1E8);
    if (!std::isfinite(placement.x) || !std::isfinite(placement.y) || !std::isfinite(placement.z) ||
        !std::isfinite(placement.heading)) {
        result = "non_finite_transform";
        return false;
    }
    result = "captured";
    return true;
}

void RefreshSeamlessLocalPlacement() {
    SummonPlacementDescriptor placement{};
    std::string_view result;
    if (!ReadLocalSummonPlacement(placement, result)) {
        return;
    }
    std::scoped_lock lock{seamless_placement_mutex};
    seamless_host_placement = placement;
}

void ApplyCrossMapSummonHostPlacement(const GuestRegisterSnapshot& registers) {
    host_placement_rewrite = {};
    host_placement_rewrite.result = "invalid_prepared";
    host_placement_rewrite.prepared = registers.rsi;
    {
        std::scoped_lock lock{seamless_placement_mutex};
        seamless_host_placement.reset();
    }

    const u64 prepared = registers.rsi;
    if (prepared < 0x10000 ||
        !HasMemoryAccess(prepared, 0x04 + sizeof(SummonPlacementDescriptor), MemoryProt::CpuRead)) {
        return;
    }
    std::memcpy(&host_placement_rewrite.candidate, reinterpret_cast<const void*>(prepared + 0x04),
                sizeof(SummonPlacementDescriptor));

    auto& host = host_placement_rewrite.host;
    if (!IsUsablePackedMap(host_placement_rewrite.candidate.packed_region)) {
        host_placement_rewrite.result = "invalid_map";
        return;
    }
    if (!ReadLocalSummonPlacement(host, host_placement_rewrite.result)) {
        return;
    }
    if (host_placement_rewrite.candidate.packed_region == host.packed_region) {
        host_placement_rewrite.result = "same_map";
        return;
    }

    if (!WriteValue(prepared, 0x04, host)) {
        host_placement_rewrite.result = "write_rejected";
        return;
    }
    {
        std::scoped_lock lock{seamless_placement_mutex};
        seamless_host_placement = host;
    }
    host_placement_rewrite.result = "applied";
}

void ApplyCrossMapSummonGuestPlacement(const GuestRegisterSnapshot& registers) {
    guest_placement_handoff = {};
    guest_placement_handoff.result = "invalid_object";
    guest_placement_handoff.object = registers.rbx;

    const u64 object = registers.rbx;
    if (object < 0x10000 || !HasMemoryAccess(object, 0xEC, MemoryProt::CpuRead)) {
        return;
    }
    if (ReadValue<u32>(object, 0xE8) != 0) {
        guest_placement_handoff.result = "not_summoned_client";
        return;
    }

    auto& record = guest_placement_handoff;
    record.result = "invalid_state";
    record.state = GetGlobalState();
    const u64 state = record.state;
    if (state < 0x10000 || !HasMemoryAccess(state, 0x1649, MemoryProt::CpuRead)) {
        return;
    }
    if (ReadValue<u8>(state, 0x1648) == 0) {
        record.result = "received_placement_not_ready";
        return;
    }

    std::optional<SummonPlacementDescriptor> transported_host;
    {
        std::scoped_lock lock{seamless_placement_mutex};
        transported_host = seamless_received_host_placement;
    }
    record.transported_host = transported_host.has_value();

    record.current_map = GetCurrentPackedMap();
    record.received_map = transported_host.has_value() ? transported_host->packed_region
                                                       : ReadValue<u32>(state, 0x14C4);
    const u32 transport_map = transported_host.has_value() ? transported_host->packed_region
                                                           : ReadValue<u32>(state, 0x1644);
    if (!IsUsablePackedMap(record.current_map) || !IsUsablePackedMap(record.received_map)) {
        record.result = "invalid_map";
        return;
    }
    if (record.received_map != transport_map) {
        record.result = "placement_copy_pending";
        return;
    }
    if (record.current_map == record.received_map) {
        record.result = "same_map";
        return;
    }
    if (!HasMemoryAccess(state, 0x1521, MemoryProt::CpuWrite)) {
        record.result = "state_not_writable";
        return;
    }
    if (transported_host.has_value()) {
        if (!WriteTransportedSummonPlacement(state, *transported_host)) {
            record.result = "transport_write_rejected";
            return;
        }
        record.placement_refreshed = true;
    }
    if (ReadValue<u8>(state, 0x08) != 0) {
        record.result = "stage_transition_pending";
        return;
    }
    if (ReadValue<u8>(state, 0x1520) != 0) {
        record.result = "native_forced_warp_pending";
        return;
    }

    for (size_t index = 0; index < 4; ++index) {
        const float position = ReadValue<float>(state, 0x14D0 + index * sizeof(float));
        const float orientation = ReadValue<float>(state, 0x14E0 + index * sizeof(float));
        if (!std::isfinite(position) || !std::isfinite(orientation)) {
            record.result = "non_finite_placement";
            return;
        }
    }

    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (image_base == 0 || std::ranges::any_of(CrossMapNativeCalls, [image_size](const auto& site) {
            return site.offset >= image_size;
        })) {
        record.result = "native_call_out_of_range";
        return;
    }

    using PlacementSetter = void PS4_SYSV_ABI (*)(const void* value);
    using FlagSetter = void PS4_SYSV_ABI (*)();
    using PlacementSelector = bool PS4_SYSV_ABI (*)(u64 context, s32 warp_info_id);
    using StageTransition = s32 PS4_SYSV_ABI (*)();
    const auto set_map = reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonMapOffset);
    const auto set_position =
        reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonPositionOffset);
    const auto set_orientation =
        reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonOrientationOffset);
    const auto set_warp = reinterpret_cast<FlagSetter>(image_base + SetForcedSummonWarpOffset);
    const auto select =
        reinterpret_cast<PlacementSelector>(image_base + SelectSummonedPlacementOffset);
    const auto transition = reinterpret_cast<StageTransition>(image_base + StageTransitionOffset);

    set_position(reinterpret_cast<const void*>(state + 0x14D0));
    set_orientation(reinterpret_cast<const void*>(state + 0x14E0));
    set_map(reinterpret_cast<const void*>(state + 0x14C4));
    set_warp();
    record.select_result = select(0, -1);
    record.selected_map = ReadValue<u32>(state, 0x0C);
    if (!record.select_result || record.selected_map != record.received_map) {
        record.result = "native_select_failed";
        return;
    }

    pending_summon_reload_map.store(record.received_map, std::memory_order_release);
    pending_summon_reload_state_map.store(record.received_map, std::memory_order_release);
    record.summon_reload_armed = true;
    record.transition_result = transition();
    if (record.transition_result == 0) {
        pending_summon_reload_map.store(0, std::memory_order_release);
        pending_summon_reload_state_map.store(0, std::memory_order_release);
        record.summon_reload_armed = false;
        record.result = "stage_transition_failed";
        return;
    }
    record.result = "applied";
}

void ApplySummonReloadStateAtDescriptor(const GuestRegisterSnapshot& registers) {
    summon_reload_state = {};
    auto& record = summon_reload_state;
    record.target_map = pending_summon_reload_state_map.load(std::memory_order_acquire);
    if (!IsUsablePackedMap(record.target_map)) {
        return;
    }

    const u64 descriptor = registers.r9;
    if (descriptor < 0x10000 || !HasMemoryAccess(descriptor, 0x0C, MemoryProt::CpuRead)) {
        record.result = "invalid_descriptor";
        return;
    }
    record.descriptor_map = ReadValue<u32>(descriptor, 0x08);
    if (record.descriptor_map != record.target_map) {
        record.result = "waiting_for_target_descriptor";
        return;
    }

    u32 expected_map = record.target_map;
    if (!pending_summon_reload_state_map.compare_exchange_strong(
            expected_map, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
        record.result = "already_dispatched";
        return;
    }

    record.reload_state = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    if (record.reload_state < 0x10000 ||
        !HasMemoryAccess(record.reload_state, 0x88, MemoryProt::CpuWrite)) {
        pending_summon_reload_state_map.store(record.target_map, std::memory_order_release);
        record.result = "invalid_reload_state";
        return;
    }

    record.state_before = ReadValue<s32>(record.reload_state, 0x84);
    u32 pending_reload_map = record.target_map;
    const bool cleared_deferred_reload = pending_summon_reload_map.compare_exchange_strong(
        pending_reload_map, 0, std::memory_order_acq_rel, std::memory_order_acquire);

    using SummonedMapReload = void PS4_SYSV_ABI (*)();
    const auto reload_summoned_map =
        reinterpret_cast<SummonedMapReload>(image_base + SummonedMapReloadOffset);
    reload_summoned_map();
    record.summon_reload_started = true;
    record.state_after = ReadValue<s32>(record.reload_state, 0x84);
    if (record.state_after != 3) {
        using SummonReloadStateSetter = void PS4_SYSV_ABI (*)(u64 state);
        const auto set_reload_state =
            reinterpret_cast<SummonReloadStateSetter>(image_base + SetSummonReloadStateOffset);
        set_reload_state(record.reload_state);
        record.state_setter_used = true;
        record.state_after = ReadValue<s32>(record.reload_state, 0x84);
    }
    if (record.state_after != 3) {
        pending_summon_reload_state_map.store(record.target_map, std::memory_order_release);
        if (cleared_deferred_reload) {
            pending_summon_reload_map.store(record.target_map, std::memory_order_release);
        }
        record.result = "state_transition_failed";
        return;
    }

    std::optional<SummonPlacementDescriptor> transported_host;
    {
        std::scoped_lock lock{seamless_placement_mutex};
        transported_host = seamless_received_host_placement;
    }
    const u64 placement_state = GetGlobalState();
    if (transported_host.has_value() && transported_host->packed_region == record.target_map &&
        placement_state >= 0x10000 &&
        WriteTransportedSummonPlacement(placement_state, *transported_host)) {
        using PlacementSetter = void PS4_SYSV_ABI (*)(const void* value);
        const auto set_map =
            reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonMapOffset);
        const auto set_position =
            reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonPositionOffset);
        const auto set_orientation =
            reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonOrientationOffset);
        set_position(reinterpret_cast<const void*>(placement_state + 0x14D0));
        set_orientation(reinterpret_cast<const void*>(placement_state + 0x14E0));
        set_map(reinterpret_cast<const void*>(placement_state + 0x14C4));
        record.placement_refreshed = true;
    }
    record.result = "applied";
}

void ApplyDeferredSummonReload() {
    deferred_summon_reload = {};
    auto& record = deferred_summon_reload;
    record.target_map = pending_summon_reload_map.load(std::memory_order_acquire);
    if (!IsUsablePackedMap(record.target_map)) {
        return;
    }

    record.current_map = GetCurrentPackedMap();
    if (record.current_map != record.target_map) {
        record.result = "waiting_for_target_map";
        return;
    }

    const u64 state = GetGlobalState();
    if (state < 0x10000 || !HasMemoryAccess(state, 0x1521, MemoryProt::CpuRead)) {
        record.result = "invalid_state";
        return;
    }
    if (ReadValue<u8>(state, 0x08) != 0 || ReadValue<u8>(state, 0x1520) != 0) {
        record.result = "waiting_for_transition";
        return;
    }

    u32 expected_map = record.target_map;
    if (!pending_summon_reload_map.compare_exchange_strong(
            expected_map, 0, std::memory_order_acq_rel, std::memory_order_acquire)) {
        record.result = "already_dispatched";
        return;
    }
    pending_summon_reload_state_map.store(0, std::memory_order_release);

    using SummonedMapReload = void PS4_SYSV_ABI (*)();
    const auto reload_summoned_map =
        reinterpret_cast<SummonedMapReload>(image_base + SummonedMapReloadOffset);
    reload_summoned_map();
    record.summon_reload_started = true;
    record.result = "applied";
}

void ApplyPreMatchCrossMapGuestWarp() {
    pre_match_guest_warp = {};
    auto& record = pre_match_guest_warp;

    std::optional<SummonPlacementDescriptor> transported_host;
    bool placement_consumed = false;
    {
        std::scoped_lock lock{seamless_placement_mutex};
        transported_host = seamless_received_host_placement;
        placement_consumed = seamless_received_host_placement_consumed;
    }
    if (!transported_host.has_value()) {
        record.result = "no_placement";
        return;
    }
    if (placement_consumed) {
        record.result = "placement_consumed";
        return;
    }

    record.current_map = GetCurrentPackedMap();
    record.target_map = transported_host->packed_region;
    if (!IsUsablePackedMap(record.current_map) || !IsUsablePackedMap(record.target_map)) {
        record.result = "invalid_map";
        return;
    }
    if (record.current_map == record.target_map) {
        record.result = "destination_reached";
        return;
    }

    const u64 multi_play = ReadValue<u64>(image_base + MatchingStatePointerOffset, 0);
    record.multi_play_state = multi_play >= 0x10000 ? ReadValue<s32>(multi_play, 0x124) : -1;
    if (multi_play >= 0x10000 &&
        (record.multi_play_state != 0 || ReadValue<u64>(multi_play, 0x18) != 0)) {
        record.result = "matching_already_active";
        return;
    }

    record.state = GetGlobalState();
    const u64 state = record.state;
    if (state < 0x10000 || !HasMemoryAccess(state, 0x1521, MemoryProt::CpuWrite)) {
        record.result = "invalid_state";
        return;
    }
    if (ReadValue<u8>(state, 0x08) != 0) {
        record.result = "stage_transition_pending";
        return;
    }
    if (ReadValue<u8>(state, 0x1520) != 0) {
        record.result = "native_forced_warp_pending";
        return;
    }
    if (pending_summon_reload_map.load(std::memory_order_acquire) != 0 ||
        pending_summon_reload_state_map.load(std::memory_order_acquire) != 0) {
        record.result = "summon_reload_pending";
        return;
    }
    if (!WriteTransportedSummonPlacement(state, *transported_host)) {
        record.result = "transport_write_rejected";
        return;
    }
    record.placement_written = true;

    using PlacementSetter = void PS4_SYSV_ABI (*)(const void* value);
    using FlagSetter = void PS4_SYSV_ABI (*)();
    using PlacementSelector = bool PS4_SYSV_ABI (*)(u64 context, s32 warp_info_id);
    using StageTransition = s32 PS4_SYSV_ABI (*)();
    const auto set_map = reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonMapOffset);
    const auto set_position =
        reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonPositionOffset);
    const auto set_orientation =
        reinterpret_cast<PlacementSetter>(image_base + SetForcedSummonOrientationOffset);
    const auto set_warp = reinterpret_cast<FlagSetter>(image_base + SetForcedSummonWarpOffset);
    const auto select =
        reinterpret_cast<PlacementSelector>(image_base + SelectSummonedPlacementOffset);
    const auto transition = reinterpret_cast<StageTransition>(image_base + StageTransitionOffset);

    set_position(reinterpret_cast<const void*>(state + 0x14D0));
    set_orientation(reinterpret_cast<const void*>(state + 0x14E0));
    set_map(reinterpret_cast<const void*>(state + 0x14C4));
    set_warp();
    record.select_result = select(0, -1);
    record.selected_map = ReadValue<u32>(state, 0x0C);
    if (!record.select_result || record.selected_map != record.target_map) {
        record.result = "native_select_failed";
        return;
    }

    record.transition_result = transition();
    if (record.transition_result == 0) {
        record.result = "stage_transition_failed";
        return;
    }
    {
        std::scoped_lock lock{seamless_placement_mutex};
        if (seamless_received_host_placement.has_value() &&
            IsSameSummonPlacement(*seamless_received_host_placement, *transported_host)) {
            seamless_received_host_placement_consumed = true;
        }
    }
    pending_responder_resume = {
        .target_map = record.target_map,
        .target_area = transported_host->area,
    };
    responder_resume = {};
    responder_resume.result = "armed";
    responder_resume.target_map = record.target_map;
    responder_resume.target_area = transported_host->area;
    record.responder_resume_armed = true;
    record.result = "applied";
}

// Starts the bell's summon search directly (see StartSummonSearch), only through the player's own
// add-effect virtual, and only if it is the 01.09 function we verified at startup.
bool StartPlayerSummonSearch(u64 player, s32 effect_id) {
    if (image_base == 0 || player < 0x10000 ||
        !HasMemoryAccess(player, sizeof(u64), MemoryProt::CpuRead)) {
        return false;
    }
    const u64 vtable = ReadValue<u64>(player, 0);
    if (vtable < 0x10000 ||
        !HasMemoryAccess(vtable + CharacterAddSpEffectSlot, sizeof(u64), MemoryProt::CpuRead) ||
        ReadValue<u64>(vtable, CharacterAddSpEffectSlot) != image_base + AddSpEffectOffset) {
        LOG_WARNING(Debug,
                    "Bloodborne summon search skipped: player {:#x} has no 01.09 "
                    "add-effect virtual",
                    player);
        return false;
    }
    return StartSummonSearch(player, effect_id,
                             reinterpret_cast<AddSpEffectFn>(image_base + AddSpEffectOffset));
}

void ResumePreMatchCrossMapResponder() {
    auto& pending = pending_responder_resume;
    if (!IsUsablePackedMap(pending.target_map)) {
        return;
    }

    responder_resume = {};
    auto& record = responder_resume;
    record.target_map = pending.target_map;
    record.target_area = pending.target_area;
    record.current_map = GetCurrentPackedMap();
    if (record.current_map != record.target_map) {
        pending.ready_observations = 0;
        record.result = "waiting_for_target_map";
        return;
    }

    SummonPlacementDescriptor local{};
    std::string_view placement_result;
    if (!ReadLocalSummonPlacement(local, placement_result)) {
        pending.ready_observations = 0;
        record.result = placement_result;
        return;
    }
    record.current_area = local.area;
    if (record.current_area != record.target_area) {
        pending.ready_observations = 0;
        record.result = "waiting_for_target_area";
        return;
    }

    record.state = GetGlobalState();
    if (record.state < 0x10000 || !HasMemoryAccess(record.state, 0x1521, MemoryProt::CpuRead)) {
        pending.ready_observations = 0;
        record.result = "invalid_state";
        return;
    }
    if (ReadValue<u8>(record.state, 0x08) != 0 || ReadValue<u8>(record.state, 0x1520) != 0) {
        pending.ready_observations = 0;
        record.result = "waiting_for_stage_transition";
        return;
    }

    record.multi_play = GetMatchingState();
    if (record.multi_play < 0x10000 ||
        !HasMemoryAccess(record.multi_play, 0x128, MemoryProt::CpuRead)) {
        pending.ready_observations = 0;
        record.result = "invalid_matching_state";
        return;
    }
    record.multi_play_state = ReadValue<s32>(record.multi_play, 0x124);
    record.matching_controller = ReadValue<u64>(record.multi_play, 0x18);
    if (record.multi_play_state != 0 || record.matching_controller != 0) {
        pending.ready_observations = 0;
        record.result = "waiting_for_matching_idle";
        return;
    }

    record.player = GetLocalPlayer();
    if (record.player < 0x10000 ||
        !HasMemoryAccess(record.player, sizeof(u64), MemoryProt::CpuRead)) {
        pending.ready_observations = 0;
        record.result = "invalid_player";
        return;
    }
    record.effect_active = HasPlayerEffect(record.player, SmallResonantBellEffect);
    if (record.effect_active) {
        record.ready_observations = pending.ready_observations;
        record.attempts = pending.attempts;
        record.result = "completed";
        LOG_INFO(Debug,
                 "Bloodborne seamless responder resumed at map={:#x} area={} after {} native "
                 "attempt(s)",
                 record.target_map, record.target_area, record.attempts);
        pending = {};
        return;
    }

    if (pending.dispatched) {
        record.ready_observations = pending.ready_observations;
        record.attempts = pending.attempts;
        if (std::chrono::steady_clock::now() - pending.dispatched_at < ResponderResumeRetryDelay) {
            record.result = "waiting_for_effect";
            return;
        }
        if (pending.attempts >= ResponderResumeMaxAttempts) {
            record.result = "effect_timeout";
            return;
        }
        pending.dispatched = false;
        pending.ready_observations = 0;
    }

    ++pending.ready_observations;
    record.ready_observations = pending.ready_observations;
    record.attempts = pending.attempts;
    if (pending.ready_observations < ResponderResumeStableObservations) {
        record.result = "waiting_for_destination_stability";
        return;
    }

    record.native_result = StartPlayerSummonSearch(record.player, SmallResonantBellEffect);
    ++pending.attempts;
    pending.dispatched = true;
    pending.dispatched_at = std::chrono::steady_clock::now();
    record.attempts = pending.attempts;
    record.result = record.native_result ? "native_dispatched" : "native_rejected";
    LOG_INFO(Debug,
             "Bloodborne seamless responder search dispatch map={:#x} area={} attempt={} "
             "result={}",
             record.target_map, record.target_area, record.attempts, record.native_result);
}

// Auto-rejoin for a configured party (SHADPS4_BLOODBORNE_PARTY + SHADPS4_BLOODBORNE_PARTY_ROLE).
// Only ticked from the SOS-status hook on the game thread.
std::optional<PartyController> party_controller;

void InitPartyController() {
    const char* party = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    const char* role = std::getenv("SHADPS4_BLOODBORNE_PARTY_ROLE");
    if (party == nullptr || !ParsePartySetting(party)) {
        return;
    }
    const std::string_view role_name = role != nullptr ? role : "";
    if (role_name != "host" && role_name != "guest") {
        LOG_WARNING(Debug, "Bloodborne party set but SHADPS4_BLOODBORNE_PARTY_ROLE is not "
                           "host or guest; auto-rejoin disabled");
        return;
    }
    party_controller.emplace(role_name == "host" ? PartyRole::Host : PartyRole::Guest);
    LOG_INFO(Debug, "Bloodborne party auto-rejoin enabled as {}", role_name);
}

bool PartyHostOnMyMap();
extern GuestApart guest_apart;

PartyObservation ObserveParty(u64 player, s32 search_effect) {
    PartyObservation observation{};
    observation.player_valid =
        player >= 0x10000 && HasMemoryAccess(player, sizeof(u64), MemoryProt::CpuRead);
    observation.map_usable = IsUsablePackedMap(GetCurrentPackedMap());

    const u64 state = GetGlobalState();
    observation.stage_transition =
        state < 0x10000 || !HasMemoryAccess(state, 0x1521, MemoryProt::CpuRead) ||
        ReadValue<u8>(state, 0x08) != 0 || ReadValue<u8>(state, 0x1520) != 0 ||
        pending_summon_reload_map.load(std::memory_order_acquire) != 0 ||
        pending_summon_reload_state_map.load(std::memory_order_acquire) != 0;

    // The cross-map responder resume owns the bell while it is armed; stay out of its way.
    const u64 multi_play = GetMatchingState();
    observation.matching_idle =
        !IsUsablePackedMap(pending_responder_resume.target_map) && multi_play >= 0x10000 &&
        HasMemoryAccess(multi_play, 0x128, MemoryProt::CpuRead) &&
        ReadValue<s32>(multi_play, 0x124) == 0 && ReadValue<u64>(multi_play, 0x18) == 0;

    observation.search_effect_active =
        observation.player_valid && HasPlayerEffect(player, search_effect);
    observation.waiting_to_meet = guest_apart.Apart() && !PartyHostOnMyMap();
    observation.white_phantom = observation.player_valid &&
                                HasMemoryAccess(player, 0x7C, MemoryProt::CpuRead) &&
                                ReadValue<s32>(player, 0x78) == WhiteGhostCharacterType;
    return observation;
}

// Diagnostics for guest bloodstains in the host's world (Phase 2): logs the local bloodstain
// record, the spawned stain object and the world flags that gate them whenever they change.
void LogBloodstainStateChanges() {
    static std::string last;
    const u64 data = ReadValue<u64>(image_base + GameDataManPointerOffset, 0);
    const u64 record = data >= 0x10000 ? ReadValue<u64>(data, 0x30) : 0;
    const u64 net = GetSummonManagerRoot();
    const u64 stains = net >= 0x10000 ? ReadValue<u64>(net, 0xC68) : 0;
    const u64 stain = stains >= 0x10000 ? ReadValue<u64>(stains, 0xC0) : 0;
    const u64 global = GetGlobalState();
    const u64 player = GetLocalPlayer();
    const auto state =
        fmt::format("has_mark={} record_valid={} record_30={} record_38={} record_block={:#x} "
                    "record_pos=({}, {}, {}) stain={:#x} "
                    "stain_hidden={} flag1592={} flag1770={} chr_type={}",
                    data >= 0x10000 ? ReadValue<u8>(data, 0x28) : 0xFF,
                    record >= 0x10000 ? ReadValue<u8>(record, 0x44) : 0xFF,
                    record >= 0x10000 ? ReadValue<s32>(record, 0x30) : 0,
                    record >= 0x10000 ? ReadValue<s32>(record, 0x38) : 0,
                    record >= 0x10000 ? ReadValue<u32>(record, 0x3C) : 0,
                    record >= 0x10000 ? ReadValue<float>(record, 0x18) : 0.0f,
                    record >= 0x10000 ? ReadValue<float>(record, 0x1C) : 0.0f,
                    record >= 0x10000 ? ReadValue<float>(record, 0x20) : 0.0f, stain,
                    stain >= 0x10000 ? ReadValue<u8>(stain, 0xC0) : 0xFF,
                    global >= 0x10000 ? ReadValue<u8>(global, 0x1592) : 0xFF,
                    global >= 0x10000 ? ReadValue<u8>(global, 0x1770) : 0xFF,
                    player >= 0x10000 ? ReadValue<s32>(player, 0x78) : -1);
    if (state != last) {
        last = state;
        LOG_INFO(Debug, "Bloodborne bloodstain state map={:#x} {}", GetCurrentPackedMap(), state);
    }
}

void ObserveLocalAliveState();

// Diagnostics for a guest reviving in place (Phase 2): each side logs the other players'
// character type and HP (WorldChrMan +0x30 count, +0x38 entries of 0x38 bytes) when they change.
void LogOtherPlayersChanges() {
    static std::string last;
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const s32 count = world >= 0x10000 ? ReadValue<s32>(world, 0x30) : 0;
    const u64 entries = world >= 0x10000 ? ReadValue<u64>(world, 0x38) : 0;
    std::string state;
    for (s32 index = 0; index < std::min(count, 8); ++index) {
        const u64 player = ReadValue<u64>(entries + static_cast<u64>(index) * 0x38, 0);
        if (player < 0x10000) {
            continue;
        }
        const u64 data = ReadValue<u64>(ReadValue<u64>(player, 0x3B0), 0x20);
        state += fmt::format(" [type={} hp={}/{}]", ReadValue<s32>(player, 0x78),
                             ReadValue<s32>(data, 0x128), ReadValue<s32>(data, 0x12C));
    }
    if (state != last) {
        last = state;
        LOG_INFO(Debug, "Bloodborne other players:{}", state.empty() ? " none" : state);
    }
}

// Diagnostics for a guest reviving in place (Phase 2): the local player's active special
// effects, logged when the set changes, to see what a phantom's death leaves on it.
void LogLocalEffectChanges() {
    static std::string last;
    const u64 player = GetLocalPlayer();
    const u64 manager = player >= 0x10000 ? ReadValue<u64>(player, 0x1C8) : 0;
    std::string effects;
    u64 node = manager >= 0x10000 ? ReadValue<u64>(manager, 0x08) : 0;
    for (size_t index = 0; node >= 0x10000 && index < 128; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        if ((ReadValue<u32>(node, 0x1C) & 0x800C0003U) == 0) {
            effects += fmt::format(" {}", ReadValue<s32>(node, 0x40));
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    if (effects != last) {
        last = effects;
        LOG_INFO(Debug, "Bloodborne local effects:{}", effects.empty() ? " none" : effects);
    }
}

std::vector<s32> LocalEffects() {
    std::vector<s32> effects;
    const u64 player = GetLocalPlayer();
    const u64 manager = player >= 0x10000 ? ReadValue<u64>(player, 0x1C8) : 0;
    u64 node = manager >= 0x10000 ? ReadValue<u64>(manager, 0x08) : 0;
    for (size_t index = 0; node >= 0x10000 && index < 128; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        if ((ReadValue<u32>(node, 0x1C) & 0x800C0003U) == 0) {
            effects.push_back(ReadValue<s32>(node, 0x40));
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    return effects;
}

// Lamps as campfires (see IsLampTravelFlag). Two register hooks in the talk scripts' native code
// let a party in a session use its lamps, and turn the travel dialog's flag into a rest; the
// party's rest board on shadNet tells every member when the whole party rests at one lamp.
//
// ESD function 52 ("in multiplayer"): `cmp [count+0x14], 1; setg al; test ecx, ecx; setg cl; or
// cl, al` (more than one player, or a pending matching). Only the warp chairs (lamps, the
// Hunter's Dream headstones, chalice lamps) ask it; their multiplayer state has no rest.
constexpr u64 EsdIsMultiplayerResultOffset = 0x0203C55F;
constexpr std::array<u8, 7> EsdIsMultiplayerResult{0x85, 0xC9, 0x0F, 0x9F, 0xC1, 0x08, 0xC1};
// Talk command 11 (SetEventFlag), with the flag in r15d: `mov ecx, [rax+0xA0]; cmp ecx, -1`
// loads the flag range the talk may set ([rax+0xA0, rax+0xA4], -1 for any). A range that leaves
// the flag out makes the command skip it.
constexpr u64 TalkSetEventFlagRangeOffset = 0x0204232E;
constexpr std::array<u8, 9> TalkSetEventFlagRange{0x8B, 0x88, 0xA0, 0x00, 0x00,
                                                  0x00, 0x83, 0xF9, 0xFF};
// ForcePlayAnimation(_, entity, animation) event binding, and the lamp touch common event 7200
// plays before it warps (Lua_Warp's 7725 does nothing on the player).
constexpr u64 ForcePlayAnimationOffset = 0x0132E290;
constexpr s32 LampTouchAnimation = 101160;
// WorldChrMan +0xA6C: the bonfire respawn left over from Dark Souls. 1 asks for it; WorldChrMan
// then respawns every loaded block's characters (3), SprjEmkSystem restarts the event scripts
// (4) and the characters are released again (5, then 0). The healing fountain update requests it
// 1.33 s after OnEvent_BonfireRespawn, which no Bloodborne talk script sends.
constexpr u64 WorldRespawnStateOffset = 0xA6C;
// The two restock steps of the respawn load (0x01939470): 0x014DBB70 sorts out one goods entry
// (0x40000385) first, 0x014DACE0 then moves every goods with the restock flag from the storage
// box into the inventory, up to its limit. Neither takes arguments.
// The bonfire regene (Util_RequestRegene's work, 0x018FAC80(player, refill)): with refill 0 it
// skips the Dark Souls flask refill and only applies the rest special effects 100-109 (HP and
// the rest). RequestFullRecover alone did not heal the player at a lamp.
constexpr u64 BonfireRegeneOffset = 0x018FAC80;
constexpr u64 RestockItemClampOffset = 0x014DBB70;
constexpr u64 RestockFromStorageOffset = 0x014DACE0;
constexpr std::array<u8, 7> RestockItemClampPrologue{0x55, 0x48, 0x89, 0xE5, 0x48, 0x8D, 0x05};

// SprjEmkSystem's update (0x012F0F90): `mov ecx, [rax+0xA6C]; cmp ecx, 4` with rax = WorldChrMan.
// In state 3 it restarts every event script and resets the temporary event flags, then moves
// to 4. In a session that restart spawns things a reload never does (a quest monster behind the
// window near the Central Yharnam lamp), so a party rest goes straight to 4: the characters still
// respawn (WorldChrMan did that before state 3) and the scripts keep running as they were.
constexpr u64 EventRestartStateOffset = 0x012F0FCB;
constexpr std::array<u8, 9> EventRestartState{0x8B, 0x88, 0x6C, 0x0A, 0x00, 0x00, 0x83, 0xF9, 0x04};
std::atomic<bool> party_rest_respawn_pending{};
// The bonfire respawn's two walks over a block's characters (0x0190E3B0) skip a character whose
// NPC param has bit 0x2000000 at +0x145 (`mov rax, [rax+8]; test rax, rax` loads the param). For
// a living character during a party rest the hook points rax at a stand-in with that bit set.
constexpr std::array<u64, 2> RespawnParamChecks{0x0190E445, 0x0190E572};
constexpr std::array<u8, 7> RespawnParamCheck{0x48, 0x8B, 0x40, 0x08, 0x48, 0x85, 0xC0};
alignas(16) std::array<u8, 0x150> no_respawn_param{};
alignas(16) std::array<u64, 2> no_respawn_param_holder{};
std::atomic<bool> party_rest_respawn_running{};

// GlobalState +0x16F8 -> +0x14: players in the session (the count ESD function 52 checks).
constexpr u64 SessionPlayersOffset = 0x16F8;

std::mutex lamp_rest_mutex;
LampRestState lamp_rest;
LampRestCoordinator lamp_rest_coordinator;
bool lamp_rest_hooks_installed{};
bool lamp_rests_enabled = LampRestsEnabled(std::getenv("SHADPS4_BLOODBORNE_LAMP_REST"));
std::optional<std::chrono::steady_clock::time_point> lamp_touch_animation_at;
alignas(16) std::array<u8, 0x100> no_talk_flag_range{}; // +0xA0 = +0xA4 = 0

// What the game thread tells the rest board client, and what the client last heard back.
std::atomic<bool> rest_board_active{};
std::atomic<s32> rest_board_lamp{NotResting};
std::mutex rest_board_position_mutex;
std::optional<LampPosition> rest_board_position;
std::mutex rest_board_mutex;
std::vector<LampRest> rest_board;
std::chrono::steady_clock::time_point rest_board_time{};
std::jthread rest_board_client;

// Seamless travel: the map this member is on while in a party session (0 otherwise), posted to
// the party's map board by the same client thread, and the board it read back.
std::atomic<u32> party_map_now{};
std::mutex map_board_mutex;
std::vector<MemberMap> map_board;
std::string map_board_member;
std::chrono::steady_clock::time_point map_board_time{};

bool InPartySession() {
    const u64 multi_play = GetMatchingState();
    return party_controller.has_value() && multi_play >= 0x10000 &&
           HasMemoryAccess(multi_play, 0x128, MemoryProt::CpuRead) &&
           IsPartySession(true, ReadValue<s32>(multi_play, 0x124));
}

s32 SessionPlayers() {
    const u64 global = GetGlobalState();
    const u64 session = global >= 0x10000 ? ReadValue<u64>(global, SessionPlayersOffset) : 0;
    return session >= 0x10000 ? ReadValue<s32>(session, 0x14) : 0;
}

// The local player's position, where the healing fountain update reads it ([+0x3B0]+0x68,
// then +0x1E0).
std::optional<LampPosition> LocalPlayerPosition() {
    const u64 player = GetLocalPlayer();
    const u64 modules = player >= 0x10000 ? ReadValue<u64>(player, 0x3B0) : 0;
    const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
    if (physics < 0x10000 || !HasMemoryAccess(physics, 0x1EC, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    return LampPosition{ReadValue<float>(physics, 0x1E0), ReadValue<float>(physics, 0x1E4),
                        ReadValue<float>(physics, 0x1E8)};
}

s32 CharacterHp(u64 character);

// Characters seen alive during this visit to the map (sampled once a second while in a party
// session; see RespawnsAtPartyRest).
std::mutex seen_alive_mutex;
std::unordered_set<u64> seen_alive;
u32 seen_alive_map = 0;

bool WasAliveThisVisit(u64 character) {
    std::scoped_lock lock{seen_alive_mutex};
    return seen_alive.contains(character);
}

void PS4_SYSV_ABI RespawnOnlyTheDead(u64, GuestRegisterSnapshot* registers) {
    if (!party_rest_respawn_running.load() || registers->rax < 0x10000 ||
        RespawnsAtPartyRest(CharacterHp(registers->rbx), WasAliveThisVisit(registers->rbx))) {
        return;
    }
    registers->rax = reinterpret_cast<u64>(no_respawn_param_holder.data());
}

void PS4_SYSV_ABI SkipEventRestartForPartyRest(u64, GuestRegisterSnapshot* registers) {
    const u64 world = registers->rax;
    if (!party_rest_respawn_pending.load() || world < 0x10000 ||
        ReadValue<s32>(world, WorldRespawnStateOffset) != 3) {
        return;
    }
    party_rest_respawn_pending.store(false);
    party_rest_respawn_running.store(false);
    WriteValue(world, WorldRespawnStateOffset, s32{4});
    LOG_INFO(Debug, "Bloodborne party rest: kept the event scripts running (no restart)");
}

void PS4_SYSV_ABI LetPartyUseLamps(u64, GuestRegisterSnapshot* registers) {
    // Diagnostics: whether this player's lamp talk scripts ask at all (once, then every 3000).
    static std::atomic<u64> asked{};
    const u64 count = asked.fetch_add(1) + 1;
    if (count == 1 || count % 3000 == 0) {
        LOG_INFO(Debug, "Bloodborne lamp talk asked 'in multiplayer' {} times (party session={})",
                 count, InPartySession());
    }
    if (lamp_rest_hooks_installed && InPartySession()) {
        registers->rax &= ~u64{0xFF};
        registers->rcx = 0;
    }
}

// A party guest's lamp trip (see GuestMakesItsOwnLampTrip): the touch animation, then the warp,
// both from the party tick.
// Set by a guest's own trip, cleared once it is in a session again (see waiting_to_meet).
GuestApart guest_apart;
s32 guest_trip_point = HuntersDreamWarpPoint;
// A guest's own warp into the host's map ended in a grey void (no position at arrival); a second
// after arrival the guest is moved to the lamp the way a revive is.
bool guest_trip_in_flight{};
std::optional<std::chrono::steady_clock::time_point> guest_trip_fixup_at;
void WarpGuestToLastLamp();
std::optional<std::chrono::steady_clock::time_point> guest_lamp_touch_at;
std::optional<std::chrono::steady_clock::time_point> guest_lamp_trip_at;
constexpr u64 WarpNextStageBonfireOffset = 0x0132E050;

void TickGuestLampTrip() {
    const auto now = std::chrono::steady_clock::now();
    if (guest_trip_fixup_at && now >= *guest_trip_fixup_at) {
        guest_trip_fixup_at.reset();
        const auto at = LocalPlayerPosition();
        LOG_INFO(Debug, "Bloodborne party guest trip: position after arrival known={} ({}, {}, {})",
                 at.has_value(), at ? (*at)[0] : 0.0f, at ? (*at)[1] : 0.0f, at ? (*at)[2] : 0.0f);
        WarpGuestToLastLamp();
    }
    if (guest_lamp_touch_at && now >= *guest_lamp_touch_at) {
        guest_lamp_touch_at.reset();
        using ForcePlayAnimation = bool PS4_SYSV_ABI (*)(u64 unused, s32 entity, s32 animation);
        reinterpret_cast<ForcePlayAnimation>(image_base + ForcePlayAnimationOffset)(
            0, LocalPlayerEntity, LampTouchAnimation);
    }
    if (guest_lamp_trip_at && now >= *guest_lamp_trip_at) {
        guest_lamp_trip_at.reset();
        using WarpNextStageBonfire = void PS4_SYSV_ABI (*)(u64 unused, s32 warp_point);
        // As the events do with SetPlayerRespawnPoint: the lamp a death respawns at. The summon
        // warp byte (+0x1520, SetSosSignWarp) makes the placement selector use the summon
        // placement instead (0x01332BC0); it must be clear for a trip to a lamp.
        if (const u64 global = GetGlobalState(); global >= 0x10000) {
            LOG_INFO(Debug, "Bloodborne party guest trip: summon warp byte was {}",
                     ReadValue<u8>(global, 0x1520));
            WriteValue(global, 0x1528, guest_trip_point);
            WriteValue(global, 0x1520, u8{0});
        }
        reinterpret_cast<WarpNextStageBonfire>(image_base +
                                               WarpNextStageBonfireOffset)(0, guest_trip_point);
        guest_apart.Travelled();
        guest_trip_in_flight = true;
        LOG_INFO(Debug, "Bloodborne party guest warps to {} ({:#x})", guest_trip_point,
                 PackedMapOfWarpPoint(guest_trip_point));
    }
}

void PS4_SYSV_ABI RestInsteadOfLampTravel(u64, GuestRegisterSnapshot* registers) {
    const s32 flag = static_cast<s32>(registers->r15);
    if (flag >= 70000000 && flag < 80000000 && party_controller &&
        party_controller->SearchEffectId() == SmallResonantBellEffect) {
        LOG_INFO(Debug, "Bloodborne party guest talk sets flag {} (party session={})", flag,
                 InPartySession()); // diagnostics
    }
    // Event 12107000 ends for a client: a party guest's headstone trip is ours to make. Not gated
    // on the white-phantom type: in the host's Dream that check stopped the trip (the type is
    // logged to find out why).
    if (lamp_rest_hooks_installed && IsHeadstoneTravelFlag(flag) && InPartySession() &&
        party_controller->SearchEffectId() == SmallResonantBellEffect) {
        const auto now = std::chrono::steady_clock::now();
        guest_trip_point = HeadstoneWarpPoint(flag);
        guest_lamp_touch_at = now + std::chrono::milliseconds{500};
        guest_lamp_trip_at = now + std::chrono::seconds{3};
        const u64 player = GetLocalPlayer();
        LOG_INFO(Debug, "Bloodborne party guest travels by headstone {} to {} (type {})", flag,
                 guest_trip_point, player >= 0x10000 ? ReadValue<s32>(player, 0x78) : -1);
        return;
    }
    if (!lamp_rest_hooks_installed || !IsLampTravelFlag(flag)) {
        return;
    }
    if (!InPartySession()) {
        LOG_INFO(Debug, "Bloodborne lamp travel {} outside a party session", flag);
        return;
    }
    if (!lamp_rests_enabled) {
        LOG_INFO(Debug, "Bloodborne party member travels from lamp {} (party rests off)", flag);
        if (GuestMakesItsOwnLampTrip(true,
                                     party_controller->SearchEffectId() == SmallResonantBellEffect
                                         ? PartyRole::Guest
                                         : PartyRole::Host)) {
            const auto now = std::chrono::steady_clock::now();
            guest_lamp_touch_at = now + std::chrono::milliseconds{500};
            guest_lamp_trip_at = now + std::chrono::seconds{3};
            guest_trip_point = HuntersDreamWarpPoint;
        }
        return;
    }
    const auto position = LocalPlayerPosition();
    LampUse use;
    {
        std::scoped_lock lock{lamp_rest_mutex};
        use = lamp_rest.Use(flag, position.value_or(LampPosition{}));
    }
    if (use == LampUse::Travel) {
        // The game's own trip (common event 7200), for this player only.
        LOG_INFO(Debug, "Bloodborne party member travels from lamp {}", flag);
        return;
    }
    registers->rax = reinterpret_cast<u64>(no_talk_flag_range.data());
    // Played from TickLampRest once the talk has closed; started here, the closing talk cancels
    // it.
    lamp_touch_animation_at = std::chrono::steady_clock::now() + std::chrono::milliseconds{500};
    LOG_INFO(Debug, "Bloodborne party member rests at lamp {} (position known={})", flag,
             position.has_value());
}

// ESD function 14 (`mov rax, [r15+0x18]; test rax, rax`, then `mov eax, [rax+0x78]`): the talking
// player's character type, which the lamp scripts (and others) require to be 0 for their prompt.
// For a party guest the hook points r15 at a stand-in talk whose player has type 0; r15 is the
// dispatcher's callee-saved register, restored on its way out.
constexpr u64 EsdTalkerTypeOffset = 0x0203B7FB;
constexpr std::array<u8, 7> EsdTalkerType{0x49, 0x8B, 0x47, 0x18, 0x48, 0x85, 0xC0};
alignas(16) std::array<u8, 0x100> host_like_talker{};
alignas(16) std::array<u64, 8> host_like_talk{};

// A talk object marks itself "phantom talker" (+0x25, what ESD function 19 answers) when it starts
// and the local player's type is neither live (0) nor grey (8): `mov eax, [rax+0x78]; or eax, 8`
// with rax = the player. For a party guest the hook points rax at a stand-in of type 0.
constexpr u64 TalkPhantomCheckOffset = 0x0203AC04;
constexpr std::array<u8, 6> TalkPhantomCheck{0x8B, 0x40, 0x78, 0x83, 0xC8, 0x08};

void PS4_SYSV_ABI LetPartyGuestsStartTalksLikeTheHost(u64, GuestRegisterSnapshot* registers) {
    const u64 player = registers->rax;
    const s32 type = player >= 0x10000 && HasMemoryAccess(player, 0x7C, MemoryProt::CpuRead)
                         ? ReadValue<s32>(player, 0x78)
                         : -1;
    if (lamp_rest_hooks_installed && TalksAsHost(InPartySession(), type)) {
        registers->rax = reinterpret_cast<u64>(host_like_talker.data());
    }
}

void PS4_SYSV_ABI LetPartyGuestsTalkLikeTheHost(u64, GuestRegisterSnapshot* registers) {
    const u64 player = GetLocalPlayer();
    const s32 type = player >= 0x10000 && HasMemoryAccess(player, 0x7C, MemoryProt::CpuRead)
                         ? ReadValue<s32>(player, 0x78)
                         : -1;
    if (lamp_rest_hooks_installed && TalksAsHost(InPartySession(), type)) {
        registers->r15 = reinterpret_cast<u64>(host_like_talk.data());
    }
}

// Diagnostics for a guest's missing lamp prompt: which talk functions the scripts ask and what
// they answer (function id from the dispatcher's switch at 0x0203AD95, result at its common
// exit 0x0203EECF), logged every 5 s while a party guest is in a session.
constexpr u64 EsdFunctionIdOffset = 0x0203AD95;
constexpr std::array<u8, 5> EsdFunctionId{0x89, 0xC1, 0x83, 0xF8, 0x3E};
constexpr u64 EsdFunctionResultOffset = 0x0203EECF;
constexpr std::array<u8, 9> EsdFunctionResult{0x41, 0xC7, 0x44, 0x24, 0x08, 0x02, 0x00, 0x00, 0x00};
// Talk commands (dispatcher 0x0203FFD0, command id in eax at 0x02040002) and the dialog answers
// (ESD functions 21/22 leave through 0x0203EBCB with the result in ecx) go to the same log,
// commands as "c<id>".
constexpr u64 TalkCommandIdOffset = 0x02040002;
constexpr std::array<u8, 5> TalkCommandId{0x89, 0xC1, 0x83, 0xF8, 0x57};
constexpr u64 EsdDialogResultOffset = 0x0203EBCB;
thread_local s32 esd_function_asked = -1;
std::mutex esd_answers_mutex;
std::map<std::pair<s32, s32>, u64> esd_answers;
std::atomic<bool> esd_answers_wanted{};

void PS4_SYSV_ABI NoteEsdFunction(u64, GuestRegisterSnapshot* registers) {
    esd_function_asked = static_cast<s32>(registers->rax);
}

void PS4_SYSV_ABI NoteEsdAnswer(u64, GuestRegisterSnapshot* registers) {
    if (!esd_answers_wanted.load(std::memory_order_relaxed)) {
        return;
    }
    std::scoped_lock lock{esd_answers_mutex};
    ++esd_answers[{esd_function_asked, static_cast<s32>(registers->rax)}];
}

void PS4_SYSV_ABI NoteTalkCommand(u64, GuestRegisterSnapshot* registers) {
    if (!esd_answers_wanted.load(std::memory_order_relaxed)) {
        return;
    }
    std::scoped_lock lock{esd_answers_mutex};
    ++esd_answers[{1000 + static_cast<s32>(registers->rax), 0}];
}

void PS4_SYSV_ABI NoteEsdDialogAnswer(u64, GuestRegisterSnapshot* registers) {
    if (!esd_answers_wanted.load(std::memory_order_relaxed)) {
        return;
    }
    std::scoped_lock lock{esd_answers_mutex};
    ++esd_answers[{esd_function_asked, static_cast<s32>(registers->rcx)}];
}

void LogEsdAnswers(bool party_guest) {
    static std::chrono::steady_clock::time_point last{};
    esd_answers_wanted.store(party_guest, std::memory_order_relaxed);
    const auto now = std::chrono::steady_clock::now();
    if (!party_guest || now - last < std::chrono::seconds{5}) {
        return;
    }
    last = now;
    std::string text;
    {
        std::scoped_lock lock{esd_answers_mutex};
        for (const auto& [key, count] : esd_answers) {
            text += key.first >= 1000 ? fmt::format("c{}x{} ", key.first - 1000, count)
                                      : fmt::format("f{}={}x{} ", key.first, key.second, count);
        }
        esd_answers.clear();
    }
    if (!text.empty()) {
        LOG_INFO(Debug, "Bloodborne guest talk answers: {}", text);
    }
}

// Both or neither: lamps that work in a session but still travel would take one player away.
void InstallLampsAsCampfires(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    const auto starts_with = [base](u64 offset, std::span<const u8> bytes) {
        return std::ranges::equal(
            std::span<const u8>{reinterpret_cast<const u8*>(base + offset), bytes.size()}, bytes);
    };
    if (!starts_with(RestockItemClampOffset, RestockItemClampPrologue) ||
        !starts_with(RestockFromStorageOffset,
                     std::span<const u8>{StandardR15Prologue.data(), 6}) ||
        !starts_with(BonfireRegeneOffset, std::span<const u8>{StandardR15Prologue.data(), 6})) {
        LOG_ERROR(Debug, "Bloodborne rest functions not found; lamps stay locked in a session");
        return;
    }
    if (EsdIsMultiplayerResultOffset + EsdIsMultiplayerResult.size() > image_size ||
        TalkSetEventFlagRangeOffset + TalkSetEventFlagRange.size() > image_size ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + TalkSetEventFlagRangeOffset),
                                      TalkSetEventFlagRange, 0, RestInsteadOfLampTravel)) {
        LOG_ERROR(Debug, "Bloodborne lamp rest hook unavailable; lamps stay locked in a session");
        return;
    }
    if (!InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + EsdIsMultiplayerResultOffset),
                                      EsdIsMultiplayerResult, 0, LetPartyUseLamps)) {
        LOG_ERROR(Debug, "Bloodborne lamp multiplayer hook unavailable; lamps stay locked in a "
                         "session");
        return;
    }
    if (!InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + EventRestartStateOffset),
                                      EventRestartState, 0, SkipEventRestartForPartyRest)) {
        LOG_ERROR(Debug, "Bloodborne party rest would restart the event scripts; lamps stay "
                         "locked in a session");
        return;
    }
    const u32 no_respawn = 0x2000000;
    std::memcpy(no_respawn_param.data() + 0x145, &no_respawn, sizeof(no_respawn));
    no_respawn_param_holder[1] = reinterpret_cast<u64>(no_respawn_param.data());
    for (const u64 offset : RespawnParamChecks) {
        if (!InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + offset), RespawnParamCheck,
                                          0, RespawnOnlyTheDead)) {
            LOG_ERROR(Debug, "Bloodborne party rest would respawn the living; lamps stay locked "
                             "in a session");
            return;
        }
    }
    if (!InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + EsdFunctionIdOffset),
                                      EsdFunctionId, 0, NoteEsdFunction) ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + EsdFunctionResultOffset),
                                      EsdFunctionResult, 0, NoteEsdAnswer) ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + EsdDialogResultOffset),
                                      EsdFunctionResult, 0, NoteEsdDialogAnswer) ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + TalkCommandIdOffset),
                                      TalkCommandId, 0, NoteTalkCommand)) {
        LOG_ERROR(Debug, "Bloodborne talk diagnostics unavailable");
    }
    if (!InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + TalkPhantomCheckOffset),
                                      TalkPhantomCheck, 0, LetPartyGuestsStartTalksLikeTheHost)) {
        LOG_ERROR(Debug, "Bloodborne talk phantom hook unavailable; party guests get no lamps");
    }
    host_like_talk[3] = reinterpret_cast<u64>(host_like_talker.data()); // +0x18
    if (EsdTalkerTypeOffset + EsdTalkerType.size() > image_size ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + EsdTalkerTypeOffset),
                                      EsdTalkerType, 0, LetPartyGuestsTalkLikeTheHost)) {
        LOG_ERROR(Debug, "Bloodborne talk hook unavailable; party guests get no lamp prompt");
    }
    lamp_rest_hooks_installed = true;
    LOG_INFO(Debug, "Bloodborne lamps as campfires: a party rests at its lamps together");
}

// Posts this member's rest about once a second while it rests (once more when it stands up) and
// keeps the board the server answers with. Runs on its own thread; the game never waits for it.
void RunRestBoardClient(std::stop_token stop, std::string base_url, std::string party) {
    std::array<u8, 8> random_bytes{};
    std::random_device random;
    for (auto& byte : random_bytes) {
        byte = static_cast<u8>(random());
    }
    const std::string member = BytesToHex(random_bytes);
    httplib::Client client(base_url);
    client.set_connection_timeout(std::chrono::seconds{1});
    client.set_read_timeout(std::chrono::seconds{2});
    const httplib::Headers headers{{"X-ShadPS4-Bloodborne-Party", party}};
    LOG_INFO(Debug, "Bloodborne party rest board client for {} as member {}", base_url, member);

    std::mutex wait_mutex;
    std::condition_variable_any wait;
    s32 posted = NotResting;
    bool failing = false;
    while (!stop.stop_requested()) {
        {
            std::unique_lock lock{wait_mutex};
            wait.wait_for(lock, stop, std::chrono::seconds{1}, [] { return false; });
        }
        if (const u32 map = party_map_now.load(); map != 0) {
            const auto map_response = client.Post("/bloodborne/party/map", headers,
                                                  EncodeMapUpdate(member, map), "text/plain");
            if (const auto maps = map_response && map_response->status == 200
                                      ? ParseMapBoard(map_response->body)
                                      : std::nullopt) {
                std::scoped_lock lock{map_board_mutex};
                map_board = *maps;
                map_board_member = member;
                map_board_time = std::chrono::steady_clock::now();
            }
        }
        // In a session every member reads the board (to join a rest nearby); outside one only
        // a member that still has a rest on it posts, to take it off.
        const bool active = rest_board_active.load();
        const s32 lamp = active ? rest_board_lamp.load() : NotResting;
        if (!active && posted == NotResting) {
            continue;
        }
        std::optional<LampPosition> at;
        {
            std::scoped_lock lock{rest_board_position_mutex};
            at = rest_board_position;
        }
        const auto response = client.Post("/bloodborne/party/rest", headers,
                                          EncodeLampRestUpdate(member, lamp, at), "text/plain");
        const auto board =
            response && response->status == 200 ? ParseLampRestBoard(response->body) : std::nullopt;
        if (!board) {
            if (!failing) {
                LOG_WARNING(Debug, "Bloodborne party rest board unreachable ({})",
                            response ? std::to_string(response->status)
                                     : httplib::to_string(response.error()));
            }
            failing = true;
            continue;
        }
        failing = false;
        posted = lamp;
        std::scoped_lock lock{rest_board_mutex};
        rest_board = *board;
        rest_board_time = std::chrono::steady_clock::now();
    }
}

void StartRestBoardClient() {
    const char* party_setting = std::getenv("SHADPS4_BLOODBORNE_PARTY");
    const auto party = party_setting ? ParsePartySetting(party_setting) : std::nullopt;
    const std::string base_url = EmulatorSettings.GetShadNetWebApiServer();
    if (!lamp_rest_hooks_installed || !party || !EmulatorSettings.IsShadNetEnabled() ||
        base_url.empty()) {
        return;
    }
    rest_board_client = std::jthread(RunRestBoardClient, base_url, *party);
}

std::vector<LampRest> FreshRestBoard() {
    std::scoped_lock lock{rest_board_mutex};
    if (std::chrono::steady_clock::now() - rest_board_time > std::chrono::seconds{3}) {
        return {};
    }
    return rest_board;
}

// Characters hidden before a party rest (see KeepDisabledAfterRespawn). WorldChrMan's blocks are
// +0x20 (count) / +0x28 (0x160 bytes each); a block's characters +0xB8 / +0xC0 (0x38 bytes each,
// the character first), the same walk as the respawn (0x01915330). A character's explicit
// disable is [+0x18]+0x20 bit 0, set by 0x018C6390 (the SetDisable binding's call).
constexpr u64 SetCharacterDisabledOffset = 0x018C6390;
std::vector<u64> hidden_characters;

template <typename Visit>
void ForEachWorldCharacter(u64 world, Visit&& visit) {
    const s32 blocks = ReadValue<s32>(world, 0x20);
    const u64 block_array = ReadValue<u64>(world, 0x28);
    for (s32 block = 0; block_array >= 0x10000 && block < std::min(blocks, 64); ++block) {
        const u64 entry = block_array + static_cast<u64>(block) * 0x160;
        const s32 count = ReadValue<s32>(entry, 0xB8);
        const u64 characters = ReadValue<u64>(entry, 0xC0);
        for (s32 index = 0; characters >= 0x10000 && index < std::min(count, 1024); ++index) {
            const u64 character = ReadValue<u64>(characters + static_cast<u64>(index) * 0x38, 0);
            if (character >= 0x10000 && HasMemoryAccess(character, 0x3B8, MemoryProt::CpuRead)) {
                visit(character);
            }
        }
    }
}

bool IsCharacterDisabled(u64 character) {
    const u64 control = ReadValue<u64>(character, 0x18);
    return control >= 0x10000 && (ReadValue<u8>(control, 0x20) & 1) != 0;
}

s32 CharacterHp(u64 character) {
    const u64 modules = ReadValue<u64>(character, 0x3B0);
    const u64 data = modules >= 0x10000 ? ReadValue<u64>(modules, 0x20) : 0;
    return data >= 0x10000 ? ReadValue<s32>(data, 0xF8) : 0;
}

// Diagnostics: what the respawn changes on each character besides its HP, to find how the event
// scripts hide characters that the disable bit does not cover.
struct CharacterState {
    u64 character;
    u32 handle;        // +0x08
    u8 control_flags;  // [+0x18]+0x20
    s32 control_state; // [+0x18]+0x10
    u64 state_flags;   // +0x1E0
    s32 hp;
    bool operator==(const CharacterState&) const = default;
};
std::vector<CharacterState> characters_before_rest;
// A copy for the delayed comparison (LogCharactersActivatedByRest), 5 s after the respawn.
std::vector<CharacterState> characters_at_rest;
CharacterState ReadCharacterState(u64 character);
std::optional<std::chrono::steady_clock::time_point> compare_activation_at;

bool IsCharacterActive(const CharacterState& state) {
    return (state.control_flags & 1) == 0 && state.control_state == 4 && state.hp > 0;
}

// Diagnostics: characters that were inactive or hidden but alive before the rest and are active
// a few seconds after it, with their position and the words around +0x28..+0x60 to name them.
void LogCharactersActivatedByRest() {
    s32 logged = 0;
    for (const auto& before : characters_at_rest) {
        if (IsCharacterActive(before) || before.hp <= 0 ||
            !HasMemoryAccess(before.character, 0x3B8, MemoryProt::CpuRead) ||
            ReadValue<u32>(before.character, 0x08) != before.handle) {
            continue;
        }
        const auto now = ReadCharacterState(before.character);
        if (!IsCharacterActive(now) || ++logged > 30) {
            continue;
        }
        const u64 modules = ReadValue<u64>(before.character, 0x3B0);
        const u64 physics = modules >= 0x10000 ? ReadValue<u64>(modules, 0x68) : 0;
        std::string words;
        for (u64 offset = 0x20; offset < 0x80; offset += 4) {
            words += fmt::format(" {:x}", ReadValue<u32>(before.character, offset));
        }
        LOG_INFO(Debug,
                 "Bloodborne rest activated character {:#x} handle {:#x}: control {:#x}/{} "
                 "flags {:#x} hp {} at ({}, {}, {}) words{}",
                 before.character, before.handle, before.control_flags, before.control_state,
                 before.state_flags, before.hp,
                 physics >= 0x10000 ? ReadValue<float>(physics, 0x1E0) : 0.0f,
                 physics >= 0x10000 ? ReadValue<float>(physics, 0x1E4) : 0.0f,
                 physics >= 0x10000 ? ReadValue<float>(physics, 0x1E8) : 0.0f, words);
    }
    LOG_INFO(Debug, "Bloodborne rest: {} inactive characters became active", logged);
    characters_at_rest.clear();
}

CharacterState ReadCharacterState(u64 character) {
    const u64 control = ReadValue<u64>(character, 0x18);
    return CharacterState{
        .character = character,
        .handle = ReadValue<u32>(character, 0x08),
        .control_flags = control >= 0x10000 ? ReadValue<u8>(control, 0x20) : u8{0xFF},
        .control_state = control >= 0x10000 ? ReadValue<s32>(control, 0x10) : -1,
        .state_flags = ReadValue<u64>(character, 0x1E0),
        .hp = CharacterHp(character),
    };
}

// Puts back the script-set state flags the respawn cleared (see RestoreScriptFlagsAfterRespawn).
void RestoreScriptFlags() {
    s32 restored = 0;
    for (const auto& before : characters_before_rest) {
        if (!HasMemoryAccess(before.character, 0x3B8, MemoryProt::CpuWrite) ||
            ReadValue<u32>(before.character, 0x08) != before.handle) {
            continue;
        }
        const u64 now = ReadValue<u64>(before.character, 0x1E0);
        const u64 flags = RestoreScriptFlagsAfterRespawn(before.state_flags, now, before.hp);
        if (flags != now) {
            WriteValue(before.character, 0x1E0, flags);
            ++restored;
        }
    }
    LOG_INFO(Debug, "Bloodborne party rest: restored script flags on {} characters", restored);
}

void LogCharacterChanges() {
    s32 logged = 0;
    for (const auto& before : characters_before_rest) {
        if (!HasMemoryAccess(before.character, 0x3B8, MemoryProt::CpuRead)) {
            continue;
        }
        auto after = ReadCharacterState(before.character);
        if (after.handle == before.handle && after.control_flags == before.control_flags &&
            after.control_state == before.control_state &&
            after.state_flags == before.state_flags && (after.hp > 0) == (before.hp > 0)) {
            continue;
        }
        if (++logged > 60) {
            break;
        }
        LOG_INFO(Debug,
                 "Bloodborne rest changed character {:#x} handle {:#x}: control {:#x}/{} -> "
                 "{:#x}/{} flags {:#x} -> {:#x} hp {} -> {}",
                 before.character, before.handle, before.control_flags, before.control_state,
                 after.control_flags, after.control_state, before.state_flags, after.state_flags,
                 before.hp, after.hp);
    }
    // Characters that only exist after the respawn (a generator spawning, for instance).
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    s32 added = 0;
    if (world >= 0x10000) {
        ForEachWorldCharacter(world, [&](u64 character) {
            if (std::ranges::any_of(characters_before_rest, [&](const CharacterState& before) {
                    return before.character == character;
                })) {
                return;
            }
            const auto state = ReadCharacterState(character);
            ++added;
            LOG_INFO(Debug,
                     "Bloodborne rest added character {:#x} handle {:#x} control {:#x}/{} "
                     "flags {:#x} hp {}",
                     character, state.handle, state.control_flags, state.control_state,
                     state.state_flags, state.hp);
        });
    }
    // WorldChrMan +0xA40: the other list the respawn walks (0x018EB900 on each entry).
    const u64 others = world >= 0x10000 ? ReadValue<u64>(world, 0xA40) : 0;
    LOG_INFO(Debug, "Bloodborne rest: {} of {} characters changed, {} added, {} in +0xA40", logged,
             characters_before_rest.size(), added,
             others >= 0x10000 ? ReadValue<s32>(others, 8) : -1);
    characters_before_rest.clear();
}

// The hidden characters stay remembered while the player stays in the same map: one that showed
// up after an earlier rest and was killed is dead now, not hidden, and the next respawn would
// bring it back (Kaniel saw exactly that with the window monster).
u32 hidden_characters_map = 0;

void RememberHiddenCharacters(u64 world) {
    if (const u32 map = GetCurrentPackedMap(); map != hidden_characters_map) {
        hidden_characters.clear();
        hidden_characters_map = map;
    }
    characters_before_rest.clear();
    ForEachWorldCharacter(world, [](u64 character) {
        characters_before_rest.push_back(ReadCharacterState(character));
    });
    s32 disabled = 0;
    ForEachWorldCharacter(world, [&](u64 character) {
        const bool off = IsCharacterDisabled(character);
        disabled += off ? 1 : 0;
        if (KeepDisabledAfterRespawn(off, CharacterHp(character)) &&
            !std::ranges::contains(hidden_characters, character)) {
            hidden_characters.push_back(character);
        }
    });
    LOG_INFO(Debug, "Bloodborne party rest: {} characters disabled, {} of them hidden alive",
             disabled, hidden_characters.size());
}

void HideCharactersAgain() {
    characters_at_rest = characters_before_rest;
    compare_activation_at = std::chrono::steady_clock::now() + std::chrono::seconds{5};
    RestoreScriptFlags();
    LogCharacterChanges();
    s32 hidden = 0;
    // Only characters still in the world's blocks: the list outlives a rest, and a stale pointer
    // handed to the game's disable would crash it.
    std::unordered_set<u64> current;
    if (const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
        world >= 0x10000) {
        ForEachWorldCharacter(world, [&](u64 character) { current.insert(character); });
    }
    for (const u64 character : hidden_characters) {
        if (current.contains(character) && !IsCharacterDisabled(character)) {
            reinterpret_cast<void PS4_SYSV_ABI (*)(u64, bool)>(
                image_base + SetCharacterDisabledOffset)(character, true);
            ++hidden;
        }
    }
    if (!hidden_characters.empty()) {
        LOG_INFO(Debug, "Bloodborne party rest: hid {} of {} characters the respawn re-enabled",
                 hidden, hidden_characters.size());
    }
}

// Does what a reload at the lamp would (see RefreshAtLamp) and asks WorldChrMan for the bonfire
// respawn (see WorldRespawnStateOffset).
void RefreshWorldAtLamp(s32 lamp, s32 players) {
    static s32 respawn_before;
    respawn_before = -1;
    const LampRestCalls calls{
        .request_full_recover =
            [] {
                reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + RequestFullRecoverOffset)();
                if (const u64 player = GetLocalPlayer(); player >= 0x10000) {
                    reinterpret_cast<void PS4_SYSV_ABI (*)(u64, s32)>(
                        image_base + BonfireRegeneOffset)(player, 0);
                }
            },
        .clamp_restock_item =
            [] {
                reinterpret_cast<void PS4_SYSV_ABI (*)()>(image_base + RestockItemClampOffset)();
            },
        .restock_from_storage =
            [] {
                reinterpret_cast<void PS4_SYSV_ABI (*)()>(image_base + RestockFromStorageOffset)();
            },
        .request_world_respawn =
            [] {
                const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
                respawn_before =
                    world >= 0x10000 ? ReadValue<s32>(world, WorldRespawnStateOffset) : -1;
                if (respawn_before == 0) {
                    RememberHiddenCharacters(world);
                    party_rest_respawn_pending.store(true);
                    party_rest_respawn_running.store(true);
                    WriteValue(world, WorldRespawnStateOffset, s32{1});
                }
            },
    };
    const bool refreshed = RefreshAtLamp(calls);
    LOG_INFO(Debug,
             "Bloodborne party of {} rested at lamp {}: refreshed={} (healed, restocked) world "
             "respawn state {} -> {}",
             players, lamp, refreshed, respawn_before, respawn_before == 0 ? 1 : respawn_before);
}

void LogWorldRespawnChanges() {
    static s32 last = 0;
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const s32 state = world >= 0x10000 ? ReadValue<s32>(world, WorldRespawnStateOffset) : 0;
    if (state != last) {
        LOG_INFO(Debug, "Bloodborne world respawn state {} -> {}", last, state);
        if (state == 0) {
            HideCharactersAgain();
        }
        last = state;
    }
}

void SampleLivingCharacters() {
    static std::chrono::steady_clock::time_point next{};
    const auto now = std::chrono::steady_clock::now();
    const u64 world = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    if (now < next || world < 0x10000) {
        return;
    }
    next = now + std::chrono::seconds{1};
    std::scoped_lock lock{seen_alive_mutex};
    if (const u32 map = GetCurrentPackedMap(); map != seen_alive_map) {
        seen_alive.clear();
        seen_alive_map = map;
    }
    ForEachWorldCharacter(world, [](u64 character) {
        if (CharacterHp(character) > 0) {
            seen_alive.insert(character);
        }
    });
}

void TickLampRest() {
    if (!lamp_rest_hooks_installed || !lamp_rests_enabled) {
        return;
    }
    SampleLivingCharacters();
    LogWorldRespawnChanges();
    if (compare_activation_at && std::chrono::steady_clock::now() >= *compare_activation_at) {
        compare_activation_at.reset();
        LogCharactersActivatedByRest();
    }
    if (lamp_touch_animation_at && std::chrono::steady_clock::now() >= *lamp_touch_animation_at) {
        lamp_touch_animation_at.reset();
        using ForcePlayAnimation = bool PS4_SYSV_ABI (*)(u64 unused, s32 entity, s32 animation);
        const bool played = reinterpret_cast<ForcePlayAnimation>(
            image_base + ForcePlayAnimationOffset)(0, LocalPlayerEntity, LampTouchAnimation);
        LOG_INFO(Debug, "Bloodborne lamp touch animation {} played={}", LampTouchAnimation, played);
    }
    const bool in_party_session = InPartySession();
    const auto board = FreshRestBoard();
    const auto position = LocalPlayerPosition();
    std::optional<s32> resting;
    std::optional<LampPosition> rested_at;
    {
        std::scoped_lock lock{lamp_rest_mutex};
        if (in_party_session && lamp_rest.JoinNearby(board, position)) {
            LOG_INFO(Debug, "Bloodborne party member joined a rest nearby");
        }
        resting = lamp_rest.Tick(position, in_party_session);
        rested_at = lamp_rest.JoinablePosition();
    }
    static std::optional<s32> last_resting;
    if (resting != last_resting) {
        LOG_INFO(Debug, "Bloodborne party member {} lamp {}", resting ? "rests at" : "left the",
                 resting.value_or(last_resting.value_or(NotResting)));
        last_resting = resting;
    }
    {
        std::scoped_lock lock{rest_board_position_mutex};
        rest_board_position = rested_at;
    }
    rest_board_lamp.store(resting.value_or(NotResting));
    rest_board_active.store(in_party_session);

    const s32 players = SessionPlayers();
    if (lamp_rest_coordinator.Tick(resting, players, board)) {
        RefreshWorldAtLamp(*resting, players);
    }
}

void TickPartyTrip();
void LogJoinTaskChanges();
void TickPlayersOnThisMap();

void TickPartyController() {
    TickPartyTrip();
    TickPlayersOnThisMap();
    LogJoinTaskChanges();
    LogBloodstainStateChanges();
    LogLocalEffectChanges();
    LogOtherPlayersChanges();
    ObserveLocalAliveState();
    TickLampRest();
    if (!party_controller) {
        return;
    }
    const u64 player = GetLocalPlayer();
    const PartyObservation observation = ObserveParty(player, party_controller->SearchEffectId());
    if (party_controller->Tick(observation, PartyController::Clock::now()) !=
        PartyAction::StartSearch) {
        return;
    }
    const bool result = StartPlayerSummonSearch(player, party_controller->SearchEffectId());
    LOG_INFO(Debug, "Bloodborne party auto-rejoin started search effect={} map={:#x} result={}",
             party_controller->SearchEffectId(), GetCurrentPackedMap(), result);
}

void PS4_SYSV_ABI TraceEntry(u64 tag, const GuestRegisterSnapshot* registers);
bool PS4_SYSV_ABI OverrideHostDeathRespawn(u64 tag, const GuestRegisterSnapshot* registers);
bool PS4_SYSV_ABI StayWhenTheHostLeaves(u64 tag, const GuestRegisterSnapshot* registers);

bool MatchesImage(u64 base, u64 image_size, const CodePatch& patch) {
    return patch.offset <= image_size && patch.original.size() <= image_size - patch.offset &&
           std::ranges::equal(std::span<const u8>{reinterpret_cast<const u8*>(base + patch.offset),
                                                  patch.original.size()},
                              patch.original);
}

// Deaths that keep the party: a dead guest revives in the host's world instead of being sent
// home. Optional and all-or-nothing: the byte patches alone would leave the guest dead.
void InstallGhostDeathRevive(u64 base, u64 image_size) {
    const auto& patches = GhostDeathKeepSessionPatches();
    for (const auto& patch : patches) {
        if (!MatchesImage(base, image_size, patch)) {
            LOG_ERROR(Debug,
                      "Bloodborne ghost-death signature mismatch at {:#x}; deaths still send "
                      "guests home",
                      patch.offset);
            return;
        }
    }
    const auto site = std::ranges::find_if(
        Sites, [](const TraceSite& site) { return site.offset == GhostDeathReviveOffset; });
    if (site == Sites.end() ||
        !InstallGuestCodeHook(reinterpret_cast<void*>(base + GhostDeathReviveOffset),
                              std::span<const u8>{site->prologue.data(), site->prologue_size},
                              static_cast<size_t>(std::distance(Sites.begin(), site)),
                              TraceEntry)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne ghost-death revive hook at {:#x}",
                  GhostDeathReviveOffset);
        return;
    }
    ghost_death_revive_hook_installed = true;
    for (const auto& patch : patches) {
        std::memcpy(reinterpret_cast<void*>(base + patch.offset), patch.patched.data(),
                    patch.patched.size());
    }
    // Optional: without it the guest's stain is still dropped, it just can't pick it up.
    const auto& stain_patches = GuestBloodstainPatches();
    if (std::ranges::all_of(stain_patches, [&](const CodePatch& patch) {
            return MatchesImage(base, image_size, patch);
        })) {
        for (const auto& patch : stain_patches) {
            std::memcpy(reinterpret_cast<void*>(base + patch.offset), patch.patched.data(),
                        patch.patched.size());
        }
    } else {
        LOG_ERROR(Debug, "Bloodborne guest bloodstain pickup patch signature mismatch");
    }
    LOG_INFO(Debug, "Bloodborne ghost-death patch: party guests revive in the host's world");
}

// A host death with a party revives the host like a guest (no world reload) and the guests skip
// HostDead's warp home. All-or-nothing: guests that stay with a host that reloads its world
// would be stranded.
void InstallHostDeathKeepsGuests(u64 base, u64 image_size) {
    const auto& patches = HostDeathKeepGuestsPatches();
    if (!world_teardown_hook_installed ||
        !std::ranges::all_of(patches, [&](const CodePatch& patch) {
            return MatchesImage(base, image_size, patch);
        })) {
        LOG_ERROR(Debug, "Bloodborne host-death patch unavailable; host deaths send guests home");
        return;
    }
    const auto site = std::ranges::find_if(
        Sites, [](const TraceSite& site) { return site.offset == SoloPlayDeathRespawnOffset; });
    if (site == Sites.end() ||
        !InstallGuestFunctionOverride(
            reinterpret_cast<void*>(base + SoloPlayDeathRespawnOffset),
            std::span<const u8>{site->prologue.data(), site->prologue_size},
            static_cast<size_t>(std::distance(Sites.begin(), site)), OverrideHostDeathRespawn)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne host-death respawn hook at {:#x}",
                  SoloPlayDeathRespawnOffset);
        return;
    }
    host_death_respawn_hook_installed = true;
    for (const auto& patch : patches) {
        std::memcpy(reinterpret_cast<void*>(base + patch.offset), patch.patched.data(),
                    patch.patched.size());
    }
    LOG_INFO(Debug, "Bloodborne host-death patch: a host with a party revives without a reload");
}

// A boss kill keeps the party together: nobody leaves the session and the guest is not warped
// home. All-or-nothing: half of it would leave a guest in a session its host already left.
void InstallBossClearKeepsSession(u64 base, u64 image_size) {
    const auto& patches = BossClearKeepSessionPatches();
    if (!std::ranges::all_of(patches, [&](const CodePatch& patch) {
            return MatchesImage(base, image_size, patch);
        })) {
        LOG_ERROR(Debug, "Bloodborne boss-clear patch signature mismatch; boss kills end sessions");
        return;
    }
    for (const auto& patch : patches) {
        std::memcpy(reinterpret_cast<void*>(base + patch.offset), patch.patched.data(),
                    patch.patched.size());
    }
    LOG_INFO(Debug, "Bloodborne boss-clear patch: a boss kill keeps the session");
}

constexpr u64 LeaveLimitOffset = 0x0138A4A0;
constexpr std::array<u8, 6> LeaveLimitPrologue{0x55, 0x48, 0x89, 0xE5, 0x53, 0x50};

void InstallGuestStaysWhenTheHostLeaves(u64 base, u64 image_size) {
    if (!party_controller || LeaveLimitOffset + LeaveLimitPrologue.size() > image_size ||
        !InstallGuestFunctionOverride(reinterpret_cast<void*>(base + LeaveLimitOffset),
                                      LeaveLimitPrologue, 0, StayWhenTheHostLeaves)) {
        LOG_ERROR(Debug, "Bloodborne leave-limit hook unavailable; a guest whose host leaves "
                         "goes home");
        return;
    }
    LOG_INFO(Debug, "Bloodborne party guests stay where they are when their host leaves");
}

// Faster sign posts for party joins. Optional: on a mismatch the game keeps its own timers.
void PatchSignTimers(u64 base, u64 image_size) {
    for (const auto& site : SignTimerSites) {
        const auto expected = EncodeSignTimerStore(site.modrm, site.original_seconds);
        if (site.offset > image_size || expected.size() > image_size - site.offset ||
            !std::ranges::equal(std::span<const u8>{reinterpret_cast<const u8*>(base + site.offset),
                                                    expected.size()},
                                expected)) {
            LOG_ERROR(Debug,
                      "Bloodborne sign timer signature mismatch at {:#x}; keeping the "
                      "game's sign timers",
                      site.offset);
            return;
        }
    }
    for (const auto& site : SignTimerSites) {
        const auto patched = EncodeSignTimerStore(site.modrm, site.patched_seconds);
        std::memcpy(reinterpret_cast<void*>(base + site.offset), patched.data(), patched.size());
    }
    LOG_INFO(Debug, "Bloodborne sign timers patched: first post after {} s, re-post every {} s",
             FirstSignPostDelaySeconds, SignRepostSeconds);
}

// Seamless travel (see PartyTrip): the stage transition every trip goes through (lamps, the
// Dream's headstones, the event warps), and LeaveSession, which 01.09 calls from several places
// on the way (one is the stage change at 0x0193C689).
constexpr u64 StageTransitionEntryOffset = 0x013CDE30;
constexpr u64 LeaveSessionOffset = 0x015BED10;
std::mutex party_trip_mutex;
PartyTrip party_trip;

bool PartyTripActive() {
    std::scoped_lock lock{party_trip_mutex};
    return party_trip.Active(PartyTrip::Clock::now());
}

void SaveRemoteJoinTasks();
std::vector<s32> effects_before_trip; // multiplayer effects to put back after the trip

bool PS4_SYSV_ABI RecordPartyTrip(u64, const GuestRegisterSnapshot*) {
    if (InPartySession()) {
        std::scoped_lock lock{party_trip_mutex};
        party_trip.Start(PartyTrip::Clock::now(), GetCurrentPackedMap(), GetLocalPlayer());
        SaveRemoteJoinTasks();
        effects_before_trip = LocalEffects();
        LOG_INFO(Debug, "Bloodborne party trip started map={:#x}", GetCurrentPackedMap());
    }
    return false;
}

// Every LeaveSession while a party trip is under way is skipped (the stage change's own call at
// 0x0193C689 is only one of them), and logged with its caller.
bool PS4_SYSV_ABI KeepSessionAfterTrip(u64, const GuestRegisterSnapshot* registers) {
    if (!PartyTripActive()) {
        return false;
    }
    const u64 caller = ReadValue<u64>(registers->rsp, 0);
    LOG_INFO(Debug, "Bloodborne party trip: skipped LeaveSession from {:#x} map={:#x}",
             caller >= image_base ? caller - image_base : caller, GetCurrentPackedMap());
    return true;
}

// CSMultiPlayMan (0x05540230) keeps the join tasks in a vector at +0x10..+0x18 (see
// ShouldRecreateAfterTrip). The task's SetStep virtual (vtable +0x28) requests the next step.
constexpr u64 MultiPlayManPointerOffset = 0x05540230;

template <typename Visit>
void ForEachJoinTask(Visit&& visit) {
    const u64 manager = ReadValue<u64>(image_base + MultiPlayManPointerOffset, 0);
    if (manager < 0x10000 || !HasMemoryAccess(manager, 0x20, MemoryProt::CpuRead)) {
        return;
    }
    const u64 begin = ReadValue<u64>(manager, 0x10);
    const u64 end = ReadValue<u64>(manager, 0x18);
    if (begin < 0x10000 || end <= begin || end - begin > 8 * 16 ||
        !HasMemoryAccess(begin, end - begin, MemoryProt::CpuRead)) {
        return;
    }
    for (u64 entry = begin; entry < end; entry += 8) {
        const u64 task = ReadValue<u64>(entry, 0);
        if (task >= 0x10000 && HasMemoryAccess(task, 0xF0, MemoryProt::CpuRead)) {
            visit(task);
        }
    }
}

void LogJoinTaskChanges() {
    static std::string last;
    std::string tasks;
    ForEachJoinTask([&](u64 task) {
        tasks += fmt::format("[kind={} step={} next={} flags={:#x}] ", ReadValue<s32>(task, 0xE8),
                             ReadValue<s32>(task, 0x50), ReadValue<s32>(task, 0x54),
                             ReadValue<u8>(task, 0xED));
    });
    if (tasks != last) {
        last = tasks;
        LOG_INFO(Debug, "Bloodborne join tasks: {}", tasks.empty() ? "none" : tasks);
    }
}

// What the game does when a member joins (0x0178C20C): allocate 0x100 bytes from the allocator
// at 0x05540438 (vtable +0x58), construct the task (0x01E4F9F0: task, member id, kind) and add it
// to CSMultiPlayMan +0x08 (0x01E55A70). Member ids are 0x20 bytes: SprjSessionManager
// (0x05540290) lists the session's at +0xF8..+0x100 and holds the local one at +0xD0; the game
// compares them with 0x00CA1C70.
constexpr u64 SessionManagerPointerOffset = 0x05540290;
constexpr u64 TaskAllocatorPointerOffset = 0x05540438;
constexpr u64 JoinTaskConstructOffset = 0x01E4F9F0;
constexpr u64 JoinTaskListAddOffset = 0x01E55A70;
constexpr u64 MemberIdEqualOffset = 0x00CA1C70;

// The remote players' join state from before the trip: member id, slot (+0xEC) and flags (+0xED,
// bit 0 = the member's data is synced). The rebuilt task needs both to create the character.
// A member id (0x20 bytes) is a ref-counted identity pointer at +0 plus a lock at +8, so only the
// pointer is kept: copying the lock and comparing through it hung the game.
struct SavedJoinTask {
    s32 kind = 0;
    u64 identity = 0;
    u8 slot = 0;
    u8 flags = 0;
};
std::vector<SavedJoinTask> saved_join_tasks;

// Only replaced by a trip that starts with remote players: a trip from a map they were not on
// (the Dream) keeps what was saved before.
void SaveRemoteJoinTasks() {
    std::vector<SavedJoinTask> found;
    ForEachJoinTask([&](u64 task) {
        const s32 kind = ReadValue<s32>(task, 0xE8);
        if (kind != RemoteHostTask && kind != RemoteGuestTask && kind != LocalTaskKind(false)) {
            return;
        }
        SavedJoinTask saved{};
        saved.kind = kind;
        saved.identity = ReadValue<u64>(task, 0xC8);
        saved.slot = ReadValue<u8>(task, 0xEC);
        saved.flags = ReadValue<u8>(task, 0xED);
        found.push_back(saved);
    });
    if (!found.empty()) {
        saved_join_tasks = std::move(found);
    }
    LOG_INFO(Debug, "Bloodborne party trip: {} remote join tasks saved", saved_join_tasks.size());
}

void RecreateRemotePlayers() {
    const u64 session = ReadValue<u64>(image_base + SessionManagerPointerOffset, 0);
    const u64 manager = ReadValue<u64>(image_base + MultiPlayManPointerOffset, 0);
    if (session < 0x10000 || manager < 0x10000 ||
        !HasMemoryAccess(session, 0x128, MemoryProt::CpuRead)) {
        return;
    }
    using IdEqual = bool PS4_SYSV_ABI (*)(u64 a, u64 b);
    const auto id_equal = reinterpret_cast<IdEqual>(image_base + MemberIdEqualOffset);
    const bool hosting = ReadValue<s32>(session, 0x124) == MatchingStateHosting;
    const u64 self = session + 0xD0;
    const u64 begin = ReadValue<u64>(session, 0xF8);
    const u64 end = ReadValue<u64>(session, 0x100);
    LOG_INFO(Debug, "Bloodborne party trip: {} session members, hosting={}",
             begin < end ? (end - begin) / 0x20 : 0, hosting);
    if (begin < 0x10000 || end <= begin || end - begin > 0x20 * 8 ||
        !HasMemoryAccess(begin, end - begin, MemoryProt::CpuRead)) {
        return;
    }
    using SetStep = void PS4_SYSV_ABI (*)(u64 task, s32 step);
    const auto set_step = [](u64 task, s32 step) {
        reinterpret_cast<SetStep>(ReadValue<u64>(ReadValue<u64>(task, 0), 0x28))(task, step);
    };
    // What the game does when a member joins (see above), into the step that creates the
    // character, with the slot and synced-data flag saved at the trip's start.
    const auto rebuild = [&](u64 member_id, s32 kind, const SavedJoinTask* saved) {
        const u64 allocator = ReadValue<u64>(image_base + TaskAllocatorPointerOffset, 0);
        if (allocator < 0x10000) {
            return;
        }
        using Allocate = u64 PS4_SYSV_ABI (*)(u64 allocator, u64 size, u64 alignment);
        const u64 task = reinterpret_cast<Allocate>(
            ReadValue<u64>(ReadValue<u64>(allocator, 0), 0x58))(allocator, 0x100, 8);
        if (task < 0x10000) {
            return;
        }
        using Construct = void PS4_SYSV_ABI (*)(u64 task, u64 member_id, s32 kind);
        reinterpret_cast<Construct>(image_base + JoinTaskConstructOffset)(task, member_id, kind);
        if (saved) {
            WriteValue(task, 0xEC, saved->slot);
            WriteValue(task, 0xED, static_cast<u8>(saved->flags | 1));
        }
        u64 holder = task;
        using ListAdd = void PS4_SYSV_ABI (*)(u64 list, u64 entry);
        reinterpret_cast<ListAdd>(image_base + JoinTaskListAddOffset)(
            manager + 8, reinterpret_cast<u64>(&holder));
        set_step(task, CreateCharacterStep);
        LOG_INFO(Debug,
                 "Bloodborne party trip: rebuilt the join task of a member kind={} task={:#x} "
                 "slot known={}",
                 kind, task, saved != nullptr);
    };
    const auto task_for = [&](u64 member_id) {
        u64 found = 0;
        ForEachJoinTask([&](u64 task) {
            if (!found && id_equal(task + 0xC8, member_id)) {
                found = task;
            }
        });
        return found;
    };
    for (u64 member = begin; member < end; member += 0x20) {
        if (id_equal(member, self)) {
            continue;
        }
        if (const u64 found = task_for(member)) {
            const s32 kind = ReadValue<s32>(found, 0xE8);
            if (ShouldRecreateAfterTrip(kind, ReadValue<s32>(found, 0x50))) {
                set_step(found, CreateCharacterStep);
                LOG_INFO(Debug, "Bloodborne party trip: re-creating remote player kind={}", kind);
            }
            continue;
        }
        const s32 kind = RemoteTaskKind(hosting);
        const u64 identity = ReadValue<u64>(member, 0);
        const SavedJoinTask* saved = nullptr;
        for (const SavedJoinTask& entry : saved_join_tasks) {
            if (entry.kind == kind && (entry.identity == identity || !saved)) {
                saved = &entry; // by identity, else the one remote player there was
            }
        }
        rebuild(member, kind, saved);
    }
    // On a guest its own task (kind 2) is the one that creates the host's character there.
    if (!hosting && !task_for(self)) {
        const auto saved = std::ranges::find_if(saved_join_tasks, [](const SavedJoinTask& entry) {
            return entry.kind == LocalTaskKind(false);
        });
        rebuild(self, LocalTaskKind(false), saved != saved_join_tasks.end() ? &*saved : nullptr);
    }
}

// Whether everyone else in the party (for a guest: the host) is on this member's map.
bool PartyHostOnMyMap() {
    std::scoped_lock lock{map_board_mutex};
    if (std::chrono::steady_clock::now() - map_board_time > std::chrono::seconds{3}) {
        return false;
    }
    return EveryoneElseOnMap(map_board, map_board_member, GetCurrentPackedMap());
}

// After this member's trip the other players are re-created once they are all on this map
// (creating a player into a map it is not on makes its game leave the session).
bool recreate_players_pending{};

// The bell's search effect is lost with the old stage; put it back once the new one is up.
bool restore_search_pending{};

void RestoreSearchAfterTrip(bool in_session) {
    if (!restore_search_pending || !party_controller) {
        return;
    }
    restore_search_pending = false;
    const u64 player = GetLocalPlayer();
    if (player < 0x10000 || !in_session) {
        return;
    }
    for (const s32 effect : EffectsToRestoreAfterTrip(effects_before_trip, LocalEffects())) {
        const bool result = StartPlayerSummonSearch(player, effect);
        LOG_INFO(Debug, "Bloodborne party trip: put effect {} back result={}", effect, result);
    }
}

// Experiment: SHADPS4_BLOODBORNE_GUEST_AS_HOST=1 makes a party guest a live player (SetChrType 0,
// the event binding) for as long as it is in the session.
const bool guest_as_host = SettingIsOn(std::getenv("SHADPS4_BLOODBORNE_GUEST_AS_HOST"));

void PlayGuestAsHost(bool in_session) {
    const u64 player = GetLocalPlayer();
    if (player < 0x10000 || !HasMemoryAccess(player, 0x7C, MemoryProt::CpuRead) ||
        !ShouldPlayGuestAsHost(guest_as_host, in_session, ReadValue<s32>(player, 0x78))) {
        return;
    }
    reinterpret_cast<void PS4_SYSV_ABI (*)(u64, s32, s32)>(image_base + SetChrTypeOffset)(
        0, LocalPlayerEntity, 0);
    LOG_INFO(Debug, "Bloodborne guest-as-host experiment: the guest is a live player now");
}

void LogEsdAnswers(bool party_guest);

void TickGuestLampTrip();

// The multiplayer walls (MultiplayerWallsOn) turned off in a party session the way the event
// scripts' own Lua_InvalidMultiWall does: collision and drawing off, the fog SFX invalidated.
// Event 7600 shows them again on its next multiplayer edge, so this runs about once a second.
constexpr u64 SetColiEnableOffset = 0x0132DEF0;
constexpr u64 SetDrawEnableOffset = 0x0132DEC0;
constexpr u64 InvalidSfxOffset = 0x01330870;

void TurnOffMultiplayerWalls(bool in_session, u32 map) {
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (!in_session || now - last < std::chrono::seconds{1}) {
        return;
    }
    last = now;
    using EntitySwitch = void PS4_SYSV_ABI (*)(u64 unused, s32 entity, bool on);
    for (const MultiplayerWall& wall : MultiplayerWallsOn(map)) {
        reinterpret_cast<EntitySwitch>(image_base + SetColiEnableOffset)(0, wall.object, false);
        reinterpret_cast<EntitySwitch>(image_base + SetDrawEnableOffset)(0, wall.object, false);
        reinterpret_cast<EntitySwitch>(image_base + InvalidSfxOffset)(0, wall.sfx, true);
    }
}

// The loaded SpEffectParam, found through SoloParamRepository (0x05540340): its param slots hold
// a pointer whose +0x70 -> +0x70 is the PARAM file (the goods lookup at 0x01F1E3D0 reads one this
// way). Once found, row 9006 (the summoned guest's) gets a max HP rate of 1.0.
constexpr u64 SoloParamRepositoryPointerOffset = 0x05540340;

u64 FindLoadedParam(std::string_view type) {
    const u64 repository = ReadValue<u64>(image_base + SoloParamRepositoryPointerOffset, 0);
    if (repository < 0x10000 || !HasMemoryAccess(repository, 0x2000, MemoryProt::CpuRead)) {
        return 0;
    }
    for (u64 slot = 0; slot < 0x2000; slot += 8) {
        const u64 holder = ReadValue<u64>(repository, slot);
        if (holder < 0x10000 || !HasMemoryAccess(holder, 0x78, MemoryProt::CpuRead)) {
            continue;
        }
        const u64 inner = ReadValue<u64>(holder, 0x70);
        if (inner < 0x10000 || !HasMemoryAccess(inner, 0x78, MemoryProt::CpuRead)) {
            continue;
        }
        const u64 param = ReadValue<u64>(inner, 0x70);
        if (param < 0x10000 || !HasMemoryAccess(param, 0x40, MemoryProt::CpuRead)) {
            continue;
        }
        if (IsParamOfType({reinterpret_cast<const u8*>(param), 0x40}, type)) {
            return param;
        }
    }
    return 0;
}

void KeepGuestFullHp() {
    static bool done = false;
    static int attempts = 0;
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (done || !party_controller || now - last < std::chrono::seconds{1}) {
        return;
    }
    last = now;
    const u64 param = FindLoadedParam("SP_EFFECT_PARAM_ST");
    if (param == 0) {
        if (++attempts == 30) {
            LOG_ERROR(Debug, "Bloodborne SpEffectParam not found in SoloParamRepository");
        }
        return;
    }
    done = true;
    const u16 rows = ReadValue<u16>(param, 0x0A);
    const u64 table_size = 0x40 + u64{rows} * 24;
    if (!HasMemoryAccess(param, table_size, MemoryProt::CpuRead)) {
        return;
    }
    const auto row =
        FindParamRow({reinterpret_cast<const u8*>(param), table_size}, PhantomHpEffect);
    if (!row || !HasMemoryAccess(param + *row, SpEffectMaxHpRateOffset + 4, MemoryProt::CpuWrite)) {
        LOG_ERROR(Debug, "Bloodborne SpEffect {} not found; summoned guests keep 70% HP",
                  PhantomHpEffect);
        return;
    }
    const float before = ReadValue<float>(param + *row, SpEffectMaxHpRateOffset);
    WriteValue(param + *row, SpEffectMaxHpRateOffset, 1.0f);
    LOG_INFO(Debug, "Bloodborne party guests keep full HP (SpEffect {} max HP rate {} -> 1)",
             PhantomHpEffect, before);
}

// The level sync check (see LevelSyncEffect): `seta bl` has just decided whether this player is
// above the host's band; the next instructions compare it with the last answer ([r14+0x2C]) and
// add or remove effect 9200 when it changed. Clearing bl in a party session removes the effect.
constexpr u64 LevelSyncDecisionOffset = 0x01507DB5;
constexpr std::array<u8, 8> LevelSyncDecision{0x41, 0x0F, 0xB6, 0x46, 0x2C, 0x0F, 0xB6, 0xCB};

void PS4_SYSV_ABI KeepPartyGuestsLevel(u64, GuestRegisterSnapshot* registers) {
    if (!LevelSyncsGuest(InPartySession(), (registers->rbx & 0xFF) != 0)) {
        registers->rbx &= ~u64{0xFF};
    }
}

void InstallNoLevelSyncInParty(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    if (LevelSyncDecisionOffset + LevelSyncDecision.size() > image_size ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + LevelSyncDecisionOffset),
                                      LevelSyncDecision, 0, KeepPartyGuestsLevel)) {
        LOG_ERROR(Debug, "Bloodborne level sync hook unavailable; party guests are synced to the "
                         "host's level");
        return;
    }
    LOG_INFO(Debug, "Bloodborne party guests keep their own level (no effect {})", LevelSyncEffect);
}

// Both NPC creation sites read the flag with `mov reg, [rax+0x145]` and test bit 7 right after.
// Pointing rax at an all-zero stand-in row for that read keeps the NPC a live character.
constexpr u64 NpcWanderCheckOffset = 0x018E4FB6;
constexpr std::array<u8, 9> NpcWanderCheck{0x8B, 0x88, 0x45, 0x01, 0x00, 0x00, 0xF6, 0xC1, 0x80};
constexpr u64 NpcWanderTypeCheckOffset = 0x018D8934;
constexpr std::array<u8, 8> NpcWanderTypeCheck{0x8B, 0x80, 0x45, 0x01, 0x00, 0x00, 0xA8, 0x80};
alignas(8) constexpr std::array<u8, NpcWanderGhostFlagOffset + 8> no_wander_npc_param{};

void PS4_SYSV_ABI KeepNpcsSolidForPartyGuests(u64, GuestRegisterSnapshot* registers) {
    if (registers->rax < 0x10000 ||
        !HasMemoryAccess(registers->rax + NpcWanderGhostFlagOffset, 4, MemoryProt::CpuRead)) {
        return;
    }
    const bool wander =
        (ReadValue<u32>(registers->rax, NpcWanderGhostFlagOffset) & NpcWanderGhostFlag) != 0;
    if (wander && !NpcTurnsIntoWanderGhost(InPartySession(), wander)) {
        registers->rax = reinterpret_cast<u64>(no_wander_npc_param.data());
    }
}

void InstallSolidNpcsInParty(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    if (NpcWanderCheckOffset + NpcWanderCheck.size() > image_size ||
        NpcWanderTypeCheckOffset + NpcWanderTypeCheck.size() > image_size ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + NpcWanderCheckOffset),
                                      NpcWanderCheck, 0, KeepNpcsSolidForPartyGuests) ||
        !InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + NpcWanderTypeCheckOffset),
                                      NpcWanderTypeCheck, 0, KeepNpcsSolidForPartyGuests)) {
        LOG_ERROR(Debug, "Bloodborne NPC hooks unavailable; party guests see NPCs as ghosts");
        return;
    }
    LOG_INFO(Debug, "Bloodborne party guests see NPCs as the host does");
}

// Diagnostics for shared loot (RE trace only): the map item module (MapItemMan, 0x017D-0x017E)
// asks "am I a summoned guest" (matching state 6) or "am I the host" (3) at these spots. Counting
// which run while a guest walks up to and picks up an item shows where the guest is held back.
struct ItemModuleCheck {
    u64 offset;
    std::array<u8, 7> bytes;
};
constexpr std::array<u8, 7> GuestStateCheck{0x83, 0xB8, 0x24, 0x01, 0x00, 0x00, 0x06};
constexpr std::array<u8, 7> GuestStateCheckRcx{0x83, 0xB9, 0x24, 0x01, 0x00, 0x00, 0x06};
constexpr std::array<u8, 7> HostStateCheck{0x83, 0xB8, 0x24, 0x01, 0x00, 0x00, 0x03};
constexpr auto ItemModuleChecks = std::to_array<ItemModuleCheck>({
    {0x017D3759, GuestStateCheck},
    {0x017D4F54, GuestStateCheck},
    {0x017D5642, GuestStateCheck},
    {0x017D6818, GuestStateCheck},
    {0x017D6AD2, GuestStateCheck},
    {0x017D83CB, GuestStateCheck},
    {0x017D853B, GuestStateCheck},
    {0x017D88BB, GuestStateCheck},
    {0x017D9AA8, GuestStateCheckRcx},
    {0x017D9E1C, GuestStateCheck},
    {0x017D9EE0, GuestStateCheckRcx},
    {0x017DD17C, GuestStateCheck},
    {0x017DDCD9, GuestStateCheck},
    {0x017DE5EC, GuestStateCheck},
    {0x017DE738, GuestStateCheck},
    {0x017DE79D, HostStateCheck},
    {0x017DE8CB, GuestStateCheck},
    {0x017DE925, HostStateCheck},
});
std::array<std::atomic<u64>, ItemModuleChecks.size()> item_module_check_hits{};

void PS4_SYSV_ABI CountItemModuleCheck(u64 tag, const GuestRegisterSnapshot*) {
    if (tag < item_module_check_hits.size()) {
        item_module_check_hits[tag].fetch_add(1, std::memory_order_relaxed);
    }
}

// Shared loot (see SoloMapItemStandIn): each hook points the checked register at the stand-in.
// In every spot the register is reloaded or overwritten right after the branch.
constexpr u64 MapItemCreateStateCheckOffset = 0x017DC3CF;   // cmp [rax+0x124], 6
constexpr u64 MapItemCreateSessionCheckOffset = 0x017DC3DC; // cmp [rdx+0x98], -1
constexpr std::array<u8, 7> MapItemCreateSessionCheck{0x83, 0xBA, 0x98, 0x00, 0x00, 0x00, 0xFF};
// A new map item gets its own handle (counter << 4 | kind in [item+0x20]) unless this player is
// a summoned guest, which waits for the host's; without one the pickup prompt never finds it.
constexpr u64 MapItemHandleStateCheckOffset = 0x017D52D2;   // cmp [rax+0x124], 6
constexpr u64 MapItemHandleSessionCheckOffset = 0x017D52DB; // cmp [r15+0x98], -1
constexpr std::array<u8, 8> MapItemHandleSessionCheck{0x41, 0x83, 0xBF, 0x98,
                                                      0x00, 0x00, 0x00, 0xFF};
constexpr std::array<u64, 2> MapItemPickupChecks{0x017D6831, 0x017D6AEF}; // [rax+0x1592], 0
// Picking up a networked map item (0x017D8570): a summoned guest only asks the host for it.
constexpr u64 MapItemTakeStateCheckOffset = 0x017D85F4; // cmp [rax+0x124], 6
constexpr std::array<u8, 7> MapItemPickupCheck{0x80, 0xB8, 0x92, 0x15, 0x00, 0x00, 0x00};

u64 SoloMapItemStandInAddress() {
    return reinterpret_cast<u64>(SoloMapItemStandIn().data());
}

void PS4_SYSV_ABI CreateMapItemsForPartyGuest(u64, GuestRegisterSnapshot* registers) {
    if (GuestHandlesMapItemsAlone(InPartySession())) {
        registers->rax = SoloMapItemStandInAddress();
    }
}

void PS4_SYSV_ABI CreateMapItemsInJoinedSession(u64, GuestRegisterSnapshot* registers) {
    if (GuestHandlesMapItemsAlone(InPartySession())) {
        registers->rdx = SoloMapItemStandInAddress();
    }
}

void PS4_SYSV_ABI GiveMapItemsAHandleInJoinedSession(u64, GuestRegisterSnapshot* registers) {
    if (GuestHandlesMapItemsAlone(InPartySession())) {
        registers->r15 = SoloMapItemStandInAddress();
    }
}

void PS4_SYSV_ABI LetPartyGuestPickUpMapItems(u64, GuestRegisterSnapshot* registers) {
    if (GuestHandlesMapItemsAlone(InPartySession())) {
        registers->rax = SoloMapItemStandInAddress();
    }
}

void InstallSharedLoot(u64 base, u64 image_size) {
    if (!party_controller) {
        return;
    }
    const auto hook = [base, image_size](u64 offset, std::span<const u8> bytes,
                                         GuestRegisterEditHook edit) {
        return offset + bytes.size() <= image_size &&
               InstallGuestRegisterEditHook(reinterpret_cast<void*>(base + offset), bytes, 0, edit);
    };
    if (!hook(MapItemCreateStateCheckOffset, GuestStateCheck, CreateMapItemsForPartyGuest) ||
        !hook(MapItemCreateSessionCheckOffset, MapItemCreateSessionCheck,
              CreateMapItemsInJoinedSession) ||
        !hook(MapItemHandleStateCheckOffset, GuestStateCheck, CreateMapItemsForPartyGuest) ||
        !hook(MapItemTakeStateCheckOffset, GuestStateCheck, CreateMapItemsForPartyGuest) ||
        !hook(MapItemHandleSessionCheckOffset, MapItemHandleSessionCheck,
              GiveMapItemsAHandleInJoinedSession)) {
        LOG_ERROR(Debug, "Bloodborne map item hooks unavailable; party guests see no map items");
        return;
    }
    for (const u64 offset : MapItemPickupChecks) {
        if (!hook(offset, MapItemPickupCheck, LetPartyGuestPickUpMapItems)) {
            LOG_ERROR(Debug, "Bloodborne map item pickup hook unavailable at {:#x}", offset);
        }
    }
    LOG_INFO(Debug, "Bloodborne party guests see and pick up their own map items");
}

void InstallItemModuleDiagnostics(u64 base, u64 image_size) {
    if (!party_controller || !EnvFlagEnabled("SHADPS4_BLOODBORNE_RE_TRACE")) {
        return;
    }
    for (size_t index = 0; index < ItemModuleChecks.size(); ++index) {
        const ItemModuleCheck& check = ItemModuleChecks[index];
        if (check.offset + check.bytes.size() > image_size ||
            !InstallGuestCodeHook(reinterpret_cast<void*>(base + check.offset), check.bytes, index,
                                  CountItemModuleCheck)) {
            LOG_ERROR(Debug, "Bloodborne item diagnostics: no hook at {:#x}", check.offset);
        }
    }
}

void LogItemModuleChecks() {
    static std::array<u64, ItemModuleChecks.size()> logged{};
    static std::chrono::steady_clock::time_point last{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last < std::chrono::seconds{2}) {
        return;
    }
    last = now;
    std::string text;
    for (size_t index = 0; index < ItemModuleChecks.size(); ++index) {
        const u64 hits = item_module_check_hits[index].load(std::memory_order_relaxed);
        if (hits != logged[index]) {
            text += fmt::format(" {:#x}+{}", ItemModuleChecks[index].offset, hits - logged[index]);
            logged[index] = hits;
        }
    }
    if (!text.empty()) {
        const u64 multi_play = GetMatchingState();
        const s32 state = multi_play >= 0x10000 ? ReadValue<s32>(multi_play, 0x124) : -1;
        LOG_INFO(Debug, "Bloodborne item checks (state {}):{}", state, text);
    }
}

// Diagnostics for the guest's missing pick-up prompt: the player's item button update
// (0x012FD880) asks MapItemMan for the nearest usable map item (0x017DA500, list +0x28,
// [rbp-0x58] = its handle), passes a busy check (0x0176B7E0), shows the prompt and, once
// pressed (0x01CD91F0 true), picks it up (0x017D8570). Inside the search, a usable item is
// examined at 0x017DA58B and one in the button's range lands at 0x017DA7CE.
constexpr u64 ItemButtonUpdateOffset = 0x012FD8CD;
constexpr std::array<u8, 10> ItemButtonUpdate{0x48, 0x8D, 0x7D, 0xC0, 0x41,
                                              0xBC, 0x04, 0x10, 0x00, 0x00};
constexpr u64 ItemButtonFoundOffset = 0x012FDC17;
constexpr std::array<u8, 8> ItemButtonFound{0x8B, 0x45, 0xA8, 0x89, 0x45, 0x9C, 0x85, 0xC0};
constexpr u64 ItemButtonNotBusyOffset = 0x012FDC84;
constexpr u64 ItemButtonPressedOffset = 0x012FDD95;
constexpr std::array<u8, 7> ItemButtonPlayerLoad{0x49, 0x8B, 0x85, 0xB0, 0x03, 0x00, 0x00};
constexpr u64 MapItemSearchExaminedOffset = 0x017DA58B;
constexpr std::array<u8, 6> MapItemSearchExamined{0xC4, 0xC1, 0x7A, 0x10, 0x46, 0x30};
constexpr u64 MapItemSearchInRangeOffset = 0x017DA7CE;
constexpr std::array<u8, 11> MapItemSearchInRange{0x41, 0x8B, 0x46, 0x20, 0x48, 0x8B,
                                                  0x8D, 0x10, 0xFF, 0xFF, 0xFF};
constexpr u64 MapItemManPointerOffset = 0x0553D6E0;
std::array<std::atomic<u64>, 6> item_button_counts{};
std::atomic<u32> item_button_last_handle{};

void PS4_SYSV_ABI NoteItemButton(u64 tag, const GuestRegisterSnapshot* registers) {
    if (tag >= item_button_counts.size()) {
        return;
    }
    if (tag == 1) {
        const u32 handle = ReadValue<u32>(registers->rbp - 0x58, 0);
        if (handle == 0) {
            return;
        }
        item_button_last_handle.store(handle, std::memory_order_relaxed);
    }
    item_button_counts[tag].fetch_add(1, std::memory_order_relaxed);
}

void InstallItemButtonDiagnostics(u64 base) {
    if (!party_controller || !EnvFlagEnabled("SHADPS4_BLOODBORNE_RE_TRACE")) {
        return;
    }
    const auto probe = [base](u64 offset, std::span<const u8> bytes, u64 tag) {
        return InstallGuestCodeHook(reinterpret_cast<void*>(base + offset), bytes, tag,
                                    NoteItemButton);
    };
    if (!probe(ItemButtonUpdateOffset, ItemButtonUpdate, 0) ||
        !probe(ItemButtonFoundOffset, ItemButtonFound, 1) ||
        !probe(ItemButtonNotBusyOffset, ItemButtonPlayerLoad, 2) ||
        !probe(ItemButtonPressedOffset, ItemButtonPlayerLoad, 3) ||
        !probe(MapItemSearchExaminedOffset, MapItemSearchExamined, 4) ||
        !probe(MapItemSearchInRangeOffset, MapItemSearchInRange, 5)) {
        LOG_ERROR(Debug, "Bloodborne item button diagnostics unavailable");
    }
}

// The local player's position, where the map item search reads it
// ([[[player+0x58]+8]+0x3B0]+0x68]+0x1E0).
std::optional<std::array<float, 3>> MapItemSearchPlayerPosition() {
    const u64 player = GetLocalPlayer();
    u64 node = player >= 0x10000 ? ReadValue<u64>(player, 0x58) : 0;
    for (const u64 offset : {u64{8}, u64{0x3B0}, u64{0x68}}) {
        if (node < 0x10000 || !HasMemoryAccess(node, offset + 8, MemoryProt::CpuRead)) {
            return std::nullopt;
        }
        node = ReadValue<u64>(node, offset);
    }
    if (node < 0x10000 || !HasMemoryAccess(node, 0x1EC, MemoryProt::CpuRead)) {
        return std::nullopt;
    }
    return std::array<float, 3>{ReadValue<float>(node, 0x1E0), ReadValue<float>(node, 0x1E4),
                                ReadValue<float>(node, 0x1E8)};
}

void LogItemButtonAndMapItems() {
    static std::string last;
    static std::chrono::steady_clock::time_point last_time{};
    const auto now = std::chrono::steady_clock::now();
    if (now - last_time < std::chrono::seconds{3}) {
        return;
    }
    last_time = now;
    std::string lists;
    std::string nearest;
    const auto position = MapItemSearchPlayerPosition();
    if (position) {
        nearest = fmt::format(" player at ({:.1f}, {:.1f}, {:.1f});", (*position)[0],
                              (*position)[1], (*position)[2]);
    }
    const u64 manager = ReadValue<u64>(image_base + MapItemManPointerOffset, 0);
    if (manager >= 0x10000 && HasMemoryAccess(manager, 0x40, MemoryProt::CpuRead)) {
        std::vector<std::pair<float, std::string>> closest;
        for (const u64 list : {0x18, 0x28, 0x38}) {
            u32 count = 0, usable = 0, with_handle = 0;
            u64 item = ReadValue<u64>(manager, list);
            while (item >= 0x10000 && count < 4096 &&
                   HasMemoryAccess(item, 0x58, MemoryProt::CpuRead)) {
                ++count;
                const u16 flags = ReadValue<u16>(item, 0x48);
                usable += (flags & 0x40) != 0;
                with_handle += (ReadValue<u32>(item, 0x20) >> 4) != 0;
                if (position) {
                    const float x = ReadValue<float>(item, 0x30);
                    const float y = ReadValue<float>(item, 0x34);
                    const float z = ReadValue<float>(item, 0x38);
                    const float dx = x - (*position)[0];
                    const float dy = y - (*position)[1];
                    const float dz = z - (*position)[2];
                    closest.emplace_back(
                        std::sqrt(dx * dx + dy * dy + dz * dz),
                        fmt::format(" [{:#x} at ({:.1f}, {:.1f}, {:.1f}) flags {:#x}]",
                                    ReadValue<u32>(item, 0x20), x, y, z, flags));
                }
                item = ReadValue<u64>(item, 0);
            }
            lists += fmt::format(" [{:#x}: {} items, {} usable, {} with handle]", list, count,
                                 usable, with_handle);
        }
        std::ranges::sort(closest, {}, &std::pair<float, std::string>::first);
        for (size_t index = 0; index < std::min<size_t>(closest.size(), 4); ++index) {
            nearest += fmt::format(" {:.1f} m{}", closest[index].first, closest[index].second);
        }
    }
    const std::string text = fmt::format(
        "button updates {} examined {} in range {} found {} (last handle {:#x}) not busy {} "
        "pressed {};{}{}",
        item_button_counts[0].load() > 0 ? "yes" : "no", item_button_counts[4].load(),
        item_button_counts[5].load(), item_button_counts[1].load(), item_button_last_handle.load(),
        item_button_counts[2].load(), item_button_counts[3].load(), lists, nearest);
    if (text != last) {
        last = text;
        LOG_INFO(Debug, "Bloodborne map items: {}", text);
    }
}

void TickPlayersOnThisMap() {
    LogItemModuleChecks();
    LogItemButtonAndMapItems();
    KeepGuestFullHp();
    const bool in_session = InPartySession();
    TurnOffMultiplayerWalls(in_session, GetCurrentPackedMap());
    PlayGuestAsHost(in_session);
    TickGuestLampTrip();
    LogEsdAnswers(in_session && party_controller &&
                  party_controller->SearchEffectId() == SmallResonantBellEffect);
    RestoreSearchAfterTrip(in_session);
    const u32 map = GetCurrentPackedMap();
    // Posted in and out of a session: a guest apart from the host waits for the host's map.
    party_map_now.store(party_controller && IsUsablePackedMap(map) ? map : 0);
    guest_apart.Tick(in_session);
    if (!in_session) {
        recreate_players_pending = false;
        saved_join_tasks.clear();
        return;
    }
    if (!recreate_players_pending) {
        return;
    }
    std::vector<MemberMap> board;
    std::string self;
    {
        std::scoped_lock lock{map_board_mutex};
        if (std::chrono::steady_clock::now() - map_board_time > std::chrono::seconds{3}) {
            return;
        }
        board = map_board;
        self = map_board_member;
    }
    if (!EveryoneElseOnMap(board, self, map)) {
        return;
    }
    // Not while the player is still being placed: a guest's own warp has no position for a moment
    // and gets moved to its lamp (guest_trip_fixup_at); re-created then, the host never appeared.
    if (guest_trip_fixup_at || !LocalPlayerPosition()) {
        return;
    }
    recreate_players_pending = false;
    LOG_INFO(Debug, "Bloodborne party trip: everyone is on map {:#x}; re-creating players", map);
    RecreateRemotePlayers();
}

// From the party tick: ends the trip once the player is on the new stage (GlobalState +0x08 is
// the loading flag).
void TickPartyTrip() {
    const u64 state = GetGlobalState();
    if (state < 0x10000 || !HasMemoryAccess(state, 0x09, MemoryProt::CpuRead)) {
        return;
    }
    std::scoped_lock lock{party_trip_mutex};
    const auto now = PartyTrip::Clock::now();
    const bool was_active = party_trip.Active(now);
    party_trip.ObserveStage(GetCurrentPackedMap(), ReadValue<u8>(state, 0x08) != 0,
                            GetLocalPlayer());
    if (was_active && !party_trip.Active(now)) {
        const auto at = LocalPlayerPosition();
        LOG_INFO(Debug, "Bloodborne party trip: arrived map={:#x} at=({}, {}, {}) lamp={}",
                 GetCurrentPackedMap(), at ? (*at)[0] : 0.0f, at ? (*at)[1] : 0.0f,
                 at ? (*at)[2] : 0.0f, ReadValue<s32>(state, 0x1528));
        recreate_players_pending = true;
        restore_search_pending = true;
        if (guest_trip_in_flight) {
            guest_trip_in_flight = false;
            guest_trip_fixup_at = std::chrono::steady_clock::now() + std::chrono::seconds{1};
        }
    }
}

// CSMultiPlayMan's member-leave handler (mgr, member id): marks the member's join task as
// exiting (reason 1 for anyone but the local player). Logged with its caller while in a party
// session, to find who drops a member after a trip.
constexpr u64 MemberLeaveOffset = 0x01E54DB0;

bool PS4_SYSV_ABI LogMemberLeave(u64, const GuestRegisterSnapshot* registers) {
    if (!party_controller) {
        return false;
    }
    const u64 caller = ReadValue<u64>(registers->rsp, 0);
    const u64 session = ReadValue<u64>(image_base + SessionManagerPointerOffset, 0);
    const bool self = session >= 0x10000 && registers->rsi >= 0x10000 &&
                      ReadValue<u64>(registers->rsi, 0) == ReadValue<u64>(session, 0xD0);
    LOG_INFO(Debug, "Bloodborne member leave from {:#x} self={} trip={} map={:#x}",
             caller >= image_base ? caller - image_base : caller, self, PartyTripActive(),
             GetCurrentPackedMap());
    return false;
}

// CSMultiPlayMan StopRequest (0x01ED07A0): a phantom's stage transition calls it (0x013CDEF6)
// before LeaveSession, which takes the guest out of the room. Skipped during a party trip.
bool PS4_SYSV_ABI KeepMatchingDuringTrip(u64, const GuestRegisterSnapshot* registers) {
    if (!PartyTripActive()) {
        return false;
    }
    const u64 caller = ReadValue<u64>(registers->rsp, 0);
    LOG_INFO(Debug, "Bloodborne party trip: skipped StopRequest from {:#x}",
             caller >= image_base ? caller - image_base : caller);
    return true;
}

void InstallSeamlessTravel(u64 base, u64 image_size) {
    if (!party_controller || !world_teardown_hook_installed ||
        LeaveSessionOffset + StandardR15Prologue.size() > image_size) {
        return;
    }
    const std::span<const u8> prologue{StandardR15Prologue.data(), 6};
    if (!InstallGuestFunctionOverride(reinterpret_cast<void*>(base + LeaveSessionOffset), prologue,
                                      0, KeepSessionAfterTrip) ||
        !InstallGuestFunctionOverride(reinterpret_cast<void*>(base + StageTransitionEntryOffset),
                                      prologue, 0, RecordPartyTrip)) {
        LOG_ERROR(Debug, "Bloodborne seamless travel hooks unavailable; a trip ends the session");
        return;
    }
    seamless_travel_hooks_installed = true;
    if (!InstallGuestFunctionOverride(reinterpret_cast<void*>(base + MultiPlayStopRequestOffset),
                                      prologue, 0, KeepMatchingDuringTrip)) {
        LOG_ERROR(Debug, "Bloodborne StopRequest hook unavailable; a guest's trip leaves the room");
    } else {
        stop_request_hook_installed = true;
    }
    if (!InstallGuestFunctionOverride(reinterpret_cast<void*>(base + MemberLeaveOffset), prologue,
                                      0, LogMemberLeave)) {
        LOG_ERROR(Debug, "Bloodborne member-leave log unavailable");
    }
    LOG_INFO(Debug, "Bloodborne seamless travel: a party member's trip keeps the session");
}

// A summoned guest leaving the host's world, or a host leaving its own (quit to title, death
// respawn), ends the session like the unpatched game would; the keep-matching patch otherwise
// leaves a ghost member in the room.
void EndSessionOnWorldTeardown() {
    const u64 multi_play = GetMatchingState();
    const u64 rules = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    if (multi_play < 0x10000 || !HasMemoryAccess(multi_play, 0x128, MemoryProt::CpuRead) ||
        rules < 0x10000 || !HasMemoryAccess(rules, 0x88, MemoryProt::CpuRead)) {
        return;
    }
    const s32 matching_state = ReadValue<s32>(multi_play, 0x124);
    const s32 reload_state = ReadValue<s32>(rules, 0x84);
    const bool reload_pending =
        pending_summon_reload_map.load(std::memory_order_acquire) != 0 ||
        pending_summon_reload_state_map.load(std::memory_order_acquire) != 0;
    if (!ShouldEndSessionOnWorldTeardown(matching_state, reload_state, reload_pending)) {
        return;
    }
    if (PartyTripActive()) {
        LOG_INFO(Debug, "Bloodborne party trip: kept the session through the world teardown");
        return;
    }
    using StopRequest = bool PS4_SYSV_ABI (*)(u64 multi_play);
    const bool result =
        reinterpret_cast<StopRequest>(image_base + MultiPlayStopRequestOffset)(multi_play);
    LOG_INFO(Debug, "Bloodborne {} left its world; ended the session result={}",
             matching_state == MatchingStateHosting ? "host" : "guest", result);
}

// UpDateBloodMark (native 0x013D2910) takes the stain position from the player only while
// GlobalState +0x1592 is set, which it is in the player's own world. In the host's world it
// copies a stored position instead (+0x1500, where the guest was summoned from), so the stain
// went somewhere unseen. Set it for the call.
bool PS4_SYSV_ABI UpdateGuestBloodMark() {
    const u64 global = GetGlobalState();
    const bool can_flag =
        global >= 0x10000 && HasMemoryAccess(global, 0x1593, MemoryProt::CpuWrite);
    const u8 own_world = can_flag ? ReadValue<u8>(global, 0x1592) : 0;
    if (can_flag) {
        WriteValue(global, 0x1592, u8{1});
    }
    const bool result =
        reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + UpDateBloodMarkOffset)();
    if (can_flag) {
        WriteValue(global, 0x1592, own_world);
    }
    return result;
}

// The lamp a death respawns the player at: resting at a lamp stores its respawn point id in
// GlobalState +0x1528 (SetReturnPointEntityId), and a death respawn resolves that id with
// 0x0154AFC0 against the loaded blocks ([0x0553B148], like Warp.RespawnTransform.Resolve). The
// same resolver finds the lamp while it is in a block the host's world has loaded; the guest is
// then moved there the way the Warp binding moves a character (vtable +0x5F8, then +0x1E1 |= 2).
// Otherwise the guest stays where it fell.
void WarpGuestToLastLamp() {
    const u64 global = GetGlobalState();
    const s32 point = global >= 0x10000 ? ReadValue<s32>(global, 0x1528) : -1;
    const u64 blocks = ReadValue<u64>(image_base + LoadedBlocksPointerOffset, 0);
    const u64 player = GetLocalPlayer();
    if (point == -1 || blocks < 0x10000 || player < 0x10000) {
        LOG_INFO(Debug, "Bloodborne revive: no last lamp ({}); staying where the guest fell",
                 point);
        return;
    }
    alignas(16) std::array<float, 4> position{};
    alignas(16) std::array<float, 4> rotation{};
    using Resolve = bool PS4_SYSV_ABI (*)(u64 blocks, float* position, float* rotation, s32 point);
    if (!reinterpret_cast<Resolve>(image_base + ResolveRespawnPointOffset)(
            blocks, position.data(), rotation.data(), point)) {
        LOG_INFO(Debug, "Bloodborne revive: last lamp {} is not in a loaded block; staying put",
                 point);
        return;
    }
    using Teleport = void PS4_SYSV_ABI (*)(u64 character, const float* position,
                                           const float* rotation, s32 mode);
    const u64 vtable = ReadValue<u64>(player, 0);
    const u64 teleport = vtable >= 0x10000 ? ReadValue<u64>(vtable, 0x5F8) : 0;
    if (teleport < 0x10000) {
        return;
    }
    reinterpret_cast<Teleport>(teleport)(player, position.data(), rotation.data(), 1);
    WriteValue(player, 0x1E1, static_cast<u8>(ReadValue<u8>(player, 0x1E1) | 2));
    LOG_INFO(Debug, "Bloodborne revive: moved to last lamp {} at ({}, {}, {})", point, position[0],
             position[1], position[2]);
}

AliveStateSnapshot local_alive_state;

// The local player's state flags (+0x1E0, 8 bytes) and action state ([+0x3B0]+8: +0x1B4/+0x1B8).
u64 LocalActionModule(u64 player) {
    const u64 model = ReadValue<u64>(player, 0x3B0);
    return model >= 0x10000 ? ReadValue<u64>(model, 0x08) : 0;
}

std::optional<AliveState> ReadLocalAliveState(u64 player, bool& alive) {
    alive = false;
    const u64 module = LocalActionModule(player);
    const u64 model = ReadValue<u64>(player, 0x3B0);
    const u64 data = model >= 0x10000 ? ReadValue<u64>(model, 0x20) : 0;
    if (player < 0x10000 || module < 0x10000 || data < 0x10000) {
        return std::nullopt;
    }
    const AliveState state{
        .flags = ReadValue<u64>(player, 0x1E0),
        .action = ReadValue<s32>(module, 0x1B4),
        .action_sub = ReadValue<s32>(module, 0x1B8),
    };
    alive = ReadValue<s32>(data, 0xF8) > 0 && (ReadValue<u8>(player, 0x1E3) & 1) == 0;
    return state;
}

void ObserveLocalAliveState() {
    bool alive = false;
    if (const auto state = ReadLocalAliveState(GetLocalPlayer(), alive)) {
        local_alive_state.Observe(alive, *state);
    }
}

void RestoreLocalAliveState() {
    const u64 player = GetLocalPlayer();
    bool alive = false;
    const auto before = ReadLocalAliveState(player, alive);
    const auto& saved = local_alive_state.Saved();
    if (!before || !saved) {
        LOG_ERROR(Debug, "Bloodborne revive: no alive state to restore");
        return;
    }
    WriteValue(player, 0x1E0, saved->flags);
    const u64 module = LocalActionModule(player);
    WriteValue(module, 0x1B4, saved->action);
    WriteValue(module, 0x1B8, saved->action_sub);
    LOG_INFO(Debug,
             "Bloodborne revive restored alive state flags {:#x}->{:#x} action {}->{} "
             "sub {}->{}",
             before->flags, saved->flags, before->action, saved->action, before->action_sub,
             saved->action_sub);
}

GhostDeathCalls MakeReviveCalls() {
    return GhostDeathCalls{
        .is_white_ghost =
            reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + IsWhiteGhostOffset),
        .set_event_flag = reinterpret_cast<bool PS4_SYSV_ABI (*)(u64, s32, bool)>(
            image_base + PartyGhostSetEventFlagOffset),
        .clear_return_state =
            [] {
                // OnEvent_4000 (0x013813D0) sets it to 4 and skips deaths while it is above 0.
                const u64 events = ReadValue<u64>(image_base + EventManPointerOffset, 0);
                const u64 state = events >= 0x10000 ? ReadValue<u64>(events, 0x10) : 0;
                if (state >= 0x10000) {
                    WriteValue(state, 0x08, s32{0});
                }
            },
        .request_full_recover =
            reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + RequestFullRecoverOffset),
        .update_blood_mark = UpdateGuestBloodMark,
        .revive_player = reinterpret_cast<bool PS4_SYSV_ABI (*)()>(image_base + RevivePlayerOffset),
        .revive_local_character =
            reinterpret_cast<void PS4_SYSV_ABI (*)()>(image_base + ReviveLocalCharacterOffset),
        .restore_alive_state = RestoreLocalAliveState,
        .set_chr_type =
            reinterpret_cast<void PS4_SYSV_ABI (*)(u64, s32, s32)>(image_base + SetChrTypeOffset),
        .chr_reset_animation =
            reinterpret_cast<void PS4_SYSV_ABI (*)(u64, s32)>(image_base + ChrResetAnimationOffset),
        .warp_to_last_lamp = WarpGuestToLastLamp,
    };
}

// A party guest that died gets up again where the game would show the "returned to your world"
// menu (the warp home behind it is patched out, see GhostDeathKeepSessionPatches).
void ReviveGuestAfterGhostDeath() {
    const bool revived = ReviveDeadPartyGuest(MakeReviveCalls());
    const u64 player = GetLocalPlayer();
    LOG_INFO(Debug, "Bloodborne party guest died; revived in place={} type={} flags1e3={:#x}",
             revived, player >= 0x10000 ? ReadValue<s32>(player, 0x78) : -1,
             player >= 0x10000 ? ReadValue<u8>(player, 0x1E3) : 0xFF);
}

// Replaces native SoloPlayDeath_2 (bloodstain, then the reload at the lamp) for a host with a
// session: it revives like a guest and the world is not reloaded.
// OnLeave_Limit (event 4046), 5 s after a white phantom lost its host (the host left, travelled,
// kicked it or the room went away): the reset and warp into the phantom's own world. A party
// guest stays where it is instead; its host travelling is no reason to move it.
bool PS4_SYSV_ABI StayWhenTheHostLeaves(u64, const GuestRegisterSnapshot*) {
    const u64 player = GetLocalPlayer();
    if (!party_controller || player < 0x10000 ||
        !HasMemoryAccess(player, 0x7C, MemoryProt::CpuRead) ||
        ReadValue<s32>(player, 0x78) != WhiteGhostCharacterType) {
        return false;
    }
    // The leave set 4047 ("on the way out"); left set, it swallows the next leave and death.
    reinterpret_cast<bool PS4_SYSV_ABI (*)(u64, s32, bool)>(
        image_base + PartyGhostSetEventFlagOffset)(0, 4047, false);
    LOG_INFO(Debug, "Bloodborne party guest lost its host; staying where it is map={:#x}",
             GetCurrentPackedMap());
    return true;
}

bool PS4_SYSV_ABI OverrideHostDeathRespawn(u64, const GuestRegisterSnapshot*) {
    const u64 multi_play = GetMatchingState();
    const s32 matching_state =
        multi_play >= 0x10000 && HasMemoryAccess(multi_play, 0x128, MemoryProt::CpuRead)
            ? ReadValue<s32>(multi_play, 0x124)
            : -1;
    if (!ShouldReviveHostInPlace(matching_state)) {
        return false;
    }
    ReviveDeadPartyHost(MakeReviveCalls());
    LOG_INFO(Debug,
             "Bloodborne live player died in a party session (state {}); revived without a "
             "world reload",
             matching_state);
    return true;
}

bool InPartySession();

void ApplyHealingFountainHostAvailability(const GuestRegisterSnapshot& registers) {
    healing_fountain_availability = {};
    auto& record = healing_fountain_availability;
    record.result = "invalid_object";
    record.object = registers.r13;
    if (record.object < 0x10000 || !HasMemoryAccess(record.object, 0x4C, MemoryProt::CpuRead)) {
        return;
    }

    record.blocked_before = ReadValue<u8>(record.object, 0x49) != 0;
    record.blocked_after = record.blocked_before;
    if (!record.blocked_before) {
        record.result = "already_allowed";
        return;
    }

    record.player = GetLocalPlayer();
    record.host_effect = HasPlayerEffect(record.player, 9001);
    // A party session (either role, see InstallLampsAsCampfires) uses its lamps too.
    if (!record.host_effect && !InPartySession()) {
        record.result = "not_multiplayer_host";
        return;
    }

    if (!WriteValue(record.object, 0x49, u8{})) {
        record.result = "write_rejected";
        return;
    }
    record.blocked_after = false;
    record.result = "applied";
    // Diagnostics for lamps in a party session: the other availability bytes (+0x48 special map,
    // +0x4A missing entity) and the lamp level (+0x28), once per lamp and change.
    static std::mutex log_mutex;
    static std::unordered_map<s32, std::string> logged;
    const s32 entity = ReadValue<s32>(record.object, 0x20);
    const std::string state =
        fmt::format("special={} missing={} level={}", ReadValue<u8>(record.object, 0x48),
                    ReadValue<u8>(record.object, 0x4A), ReadValue<s32>(record.object, 0x28));
    std::scoped_lock lock{log_mutex};
    if (logged[entity] != state) {
        logged[entity] = state;
        LOG_INFO(Debug, "Bloodborne lamp {} unlocked for the party: {}", entity, state);
    }
}

u64 GetSummonManagerRoot() {
    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (image_base == 0 || image_size < sizeof(u64) ||
        SummonManagerRootPointerOffset > image_size - sizeof(u64)) {
        return 0;
    }
    return ReadValue<u64>(image_base + SummonManagerRootPointerOffset, 0);
}

u64 GetSummonManager() {
    const u64 root = GetSummonManagerRoot();
    return root >= 0x10000 ? ReadValue<u64>(root, 0xC50) : 0;
}

u64 GetMatchingState() {
    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (image_base == 0 || image_size < sizeof(u64) ||
        MatchingStatePointerOffset > image_size - sizeof(u64)) {
        return 0;
    }
    return ReadValue<u64>(image_base + MatchingStatePointerOffset, 0);
}

void WriteHex(std::ostream& out, u64 value) {
    out << "\"0x" << std::hex << value << std::dec << '\"';
}

void WriteRoleSelectionMask(std::ostream& out, std::string_view field, s32 role_code) {
    if (role_code < 0 || role_code >= RoleMetadataCount || image_base == 0) {
        return;
    }
    const u64 offset =
        RoleMetadataTableOffset + static_cast<u64>(role_code) * RoleMetadataStride + sizeof(u32);
    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (image_size < sizeof(u32) || offset > image_size - sizeof(u32)) {
        return;
    }
    out << ",\"" << field << "\":";
    WriteHex(out, ReadValue<u32>(image_base + offset, 0));
}

void WriteObjectState(std::ostream& out, u64 object) {
    if (object < 0x10000) {
        return;
    }
    out << ",\"object\":{";
    out << "\"state_54\":";
    WriteHex(out, ReadValue<u32>(object, 0x54));
    out << ",\"c8\":";
    WriteHex(out, ReadValue<u64>(object, 0xC8));
    out << ",\"d0\":";
    WriteHex(out, ReadValue<u64>(object, 0xD0));
    out << ",\"d8\":";
    WriteHex(out, ReadValue<u32>(object, 0xD8));
    out << ",\"e0\":";
    WriteHex(out, ReadValue<u64>(object, 0xE0));
    out << ",\"e8\":";
    WriteHex(out, ReadValue<u32>(object, 0xE8));
    out << ",\"ec\":";
    WriteHex(out, ReadValue<u32>(object, 0xEC));
    out << ",\"f0\":";
    WriteHex(out, ReadValue<u8>(object, 0xF0));
    out << ",\"f8\":";
    WriteHex(out, ReadValue<u32>(object, 0xF8));
    out << '}';
}

void WriteGetSosState(std::ostream& out, u64 object) {
    if (object < 0x10000) {
        return;
    }
    out << ",\"get_sos\":{";
    const u64 begin = ReadValue<u64>(object, 0xD0);
    const u64 current = ReadValue<u64>(object, 0xD8);
    const u64 end = ReadValue<u64>(object, 0xE0);
    out << "\"begin\":";
    WriteHex(out, begin);
    out << ",\"current\":";
    WriteHex(out, current);
    out << ",\"end\":";
    WriteHex(out, end);
    if (begin >= 0x10000 && current >= begin && current <= end &&
        (current - begin) % sizeof(s32) == 0) {
        const size_t count = std::min<size_t>((current - begin) / sizeof(s32), 16);
        out << ",\"summon_types\":[";
        for (size_t index = 0; index < count; ++index) {
            if (index != 0) {
                out << ',';
            }
            out << ReadValue<s32>(begin, index * sizeof(s32));
        }
        out << ']';
    }
    out << ",\"request_active\":" << static_cast<u32>(ReadValue<u8>(object, 0x120));
    out << ",\"retry_count\":" << ReadValue<u32>(object, 0x124);
    out << '}';
}

void WriteMultiplayerEffects(std::ostream& out, u64 player) {
    if (player < 0x10000) {
        return;
    }

    const u64 manager = ReadValue<u64>(player, 0x1C8);
    if (manager < 0x10000) {
        return;
    }

    out << ",\"multiplayer_effects\":{";
    out << "\"manager\":";
    WriteHex(out, manager);
    out << ",\"mask\":";
    WriteHex(out, ReadValue<u32>(manager, 0x20));
    out << ",\"entries\":[";

    bool first = true;
    u64 node = ReadValue<u64>(manager, 0x08);
    for (size_t index = 0; node >= 0x10000 && index < 512; ++index) {
        const u64 next = ReadValue<u64>(node, 0x58);
        const u32 flags = ReadValue<u32>(node, 0x1C);
        const u64 param = ReadValue<u64>(node, 0x48);
        if ((flags & 0x800C0003U) == 0 && param >= 0x10000) {
            const s32 effect_id = ReadValue<s32>(node, 0x40);
            const u16 special_state = ReadValue<u16>(param, 0x156);
            if ((special_state >= 188 && special_state <= 190) || effect_id == 9003 ||
                effect_id == 9004) {
                if (!first) {
                    out << ',';
                }
                first = false;
                out << "{\"effect_id\":" << effect_id << ",\"special_state\":" << special_state
                    << '}';
            }
        }
        if (next == node) {
            break;
        }
        node = next;
    }
    out << "]}";
}

void WriteSummonCandidate(std::ostream& out, const TraceSite& site,
                          const GuestRegisterSnapshot& registers) {
    const bool is_entry = site.offset == 0x014BA980;
    const bool is_submit = site.offset == 0x014BABCB;
    const bool is_inserted = site.offset == 0x014B8E8D;
    const bool is_filter_cycle = site.offset == 0x014B6F09;
    const bool is_manager_scan = site.offset == 0x014B6F5D;
    const bool is_filter_progress = site.offset == 0x014B6F9D || site.offset == 0x014B6FE1 ||
                                    site.offset == 0x014B7070 || site.offset == 0x014B70FF ||
                                    site.offset == 0x014B7150 || site.offset == 0x014B71B2 ||
                                    site.offset == 0x014B71F4;
    const bool is_pending_inserted = site.offset == 0x014B750B;
    const bool is_observer_result = site.offset == 0x014C0238;
    const u64 wrapper = is_entry ? registers.rsi : 0;
    const u64 storage =
        is_entry ? (wrapper >= 0x10000 ? ReadValue<u64>(wrapper, 0x08) : 0)
                 : (!is_inserted && !is_filter_cycle && !is_manager_scan && !is_filter_progress &&
                            !is_pending_inserted && !is_observer_result
                        ? registers.r15
                        : 0);

    out << ",\"summon_candidate\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"manager\":";
    WriteHex(out, is_entry              ? registers.rdi
                  : is_inserted         ? registers.rdx
                  : is_pending_inserted ? registers.r14
                                        : GetSummonManager());
    out << ",\"wrapper\":";
    WriteHex(out, wrapper);
    out << ",\"storage\":";
    WriteHex(out, storage);
    if (storage >= 0x10000) {
        out << ",\"decoded_size\":" << ReadValue<u16>(storage, 0x02);
        out << ",\"role_code\":" << static_cast<u32>(ReadValue<u8>(storage, 0x7A));
        out << ",\"field_54\":";
        WriteHex(out, ReadValue<u64>(storage, 0x54));
        out << ",\"field_d4\":";
        WriteHex(out, ReadValue<u32>(storage, 0xD4));
        out << ",\"field_dc\":";
        WriteHex(out, ReadValue<u64>(storage, 0xDC));
    }
    if (is_filter_cycle) {
        const u64 manager = GetSummonManager();
        out << ",\"manager_identity_state_register\":" << static_cast<s32>(registers.r15);
        out << ",\"local_identity_pointer\":";
        WriteHex(out, registers.r12);
        if (manager >= 0x10000) {
            out << ",\"primary_head\":";
            WriteHex(out, ReadValue<u64>(manager, 0x30));
            out << ",\"primary_count\":" << ReadValue<u64>(manager, 0x38);
        }
    } else if (is_manager_scan) {
        const u64 root = GetSummonManagerRoot();
        const u64 local_identity =
            registers.rsp >= 0x10000 ? ReadValue<u64>(registers.rsp, 0x88) : 0;
        out << ",\"primary_node\":";
        WriteHex(out, registers.r15);
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r12);
        if (registers.r12 >= 0x10000) {
            out << ",\"role_code\":" << static_cast<s32>(ReadValue<s8>(registers.r12, 0x08));
            out << ",\"account_token\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x0C));
            out << ",\"user_id\":" << ReadValue<u32>(registers.r12, 0x14);
            out << ",\"character_id\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x18));
            out << ",\"candidate_identity\":[";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x20));
            out << ',';
            WriteHex(out, ReadValue<u64>(registers.r12, 0x28));
            out << ']';
            out << ",\"candidate_observer\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0xF0));
            out << ",\"candidate_area\":" << ReadValue<s32>(registers.r12, 0x98);
            out << ",\"candidate_generation\":"
                << static_cast<u32>(ReadValue<u8>(registers.r12, 0xEC));
            out << ",\"candidate_available_count\":"
                << static_cast<u32>(ReadValue<u8>(registers.r12, 0xED));
            out << ",\"candidate_stale_count\":"
                << static_cast<u32>(ReadValue<u8>(registers.r12, 0xEE));
        }
        out << ",\"local_identity_pointer\":";
        WriteHex(out, local_identity);
        if (local_identity >= 0x10000) {
            out << ",\"local_identity\":[";
            WriteHex(out, ReadValue<u64>(local_identity, 0));
            out << ',';
            WriteHex(out, ReadValue<u64>(local_identity, sizeof(u64)));
            out << ']';
        }
        out << ",\"manager_root\":";
        WriteHex(out, root);
        if (root >= 0x10000) {
            out << ",\"manager_identity\":[";
            WriteHex(out, ReadValue<u64>(root, 0xA68));
            out << ',';
            WriteHex(out, ReadValue<u64>(root, 0xA70));
            out << ']';
            out << ",\"manager_identity_state\":" << ReadValue<s32>(root, 0xA78);
            out << ",\"manager_area\":" << ReadValue<s32>(root, 0xA80);
            out << ",\"manager_gate_ac0\":" << ReadValue<s32>(root, 0xAC0);
            out << ",\"manager_flags_b14\":";
            WriteHex(out, ReadValue<u8>(root, 0xB14));
        }
    } else if (is_filter_progress) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r12);
    } else if (is_pending_inserted) {
        const u64 pending_head =
            registers.r14 >= 0x10000 ? ReadValue<u64>(registers.r14, 0x1A8) : 0;
        const u64 pending_node =
            pending_head >= 0x10000 ? ReadValue<u64>(pending_head, sizeof(u64)) : 0;
        const u64 pending_wrapper =
            pending_node >= 0x10000 ? ReadValue<u64>(pending_node, 0x10) : 0;
        out << ",\"pending_node\":";
        WriteHex(out, pending_node);
        out << ",\"pending_wrapper\":";
        WriteHex(out, pending_wrapper);
        out << ",\"constructed_candidate\":";
        WriteHex(out, pending_wrapper >= 0x10000 ? ReadValue<u64>(pending_wrapper, 0) : 0);
        if (registers.r14 >= 0x10000) {
            out << ",\"pending_count\":" << ReadValue<u64>(registers.r14, 0x1B0);
        }
    } else if (is_inserted) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r15);
        if (registers.r15 >= 0x10000) {
            out << ",\"candidate_observer_before_registration\":";
            WriteHex(out, ReadValue<u64>(registers.r15, 0xF0));
        }
    } else if (is_observer_result) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r14);
        out << ",\"interface\":";
        WriteHex(out, registers.r15);
        out << ",\"observer\":";
        WriteHex(out, registers.r12);
        if (registers.r12 >= 0x10000) {
            out << ",\"observer_callback\":";
            WriteHex(out, ReadValue<u64>(registers.r12, 0x58));
        }
        out << ",\"observer_registered\":" << static_cast<u32>(static_cast<u8>(registers.rax));
    } else if (is_submit) {
        out << ",\"constructed_candidate\":";
        WriteHex(out, registers.r14);
    }
    out << '}';

    if (!is_entry || image_base == 0 || MemoryPatcher::g_eboot_image_size < sizeof(u64) ||
        CandidateLocalStatePointerOffset > MemoryPatcher::g_eboot_image_size - sizeof(u64)) {
        return;
    }

    const u64 root = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
    const u64 player = root >= 0x10000 ? ReadValue<u64>(root, 0x60) : 0;
    out << ",\"candidate_local_state\":{";
    out << "\"root\":";
    WriteHex(out, root);
    out << ",\"player\":";
    WriteHex(out, player);
    if (player >= 0x10000) {
        const u64 effects = ReadValue<u64>(player, 0x1C8);
        out << ",\"effects\":";
        WriteHex(out, effects);
        if (effects >= 0x10000) {
            out << ",\"effect_mask\":";
            WriteHex(out, ReadValue<u32>(effects, 0x20));
        }
    }
    out << '}';
    WriteMultiplayerEffects(out, player);
}

void WriteCharacterWarp(std::ostream& out, const TraceSite& site,
                        const GuestRegisterSnapshot& registers) {
    out << ",\"character_warp\":{";
    out << "\"operation\":\"" << site.name << "\"";
    const u64 return_address = ReadValue<u64>(registers.rsp, 0);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '\"';
    }
    if (site.offset == 0x013CB3D0) {
        out << ",\"source_entity_id\":" << static_cast<s32>(registers.rdi);
        out << ",\"target_area_entity_id\":" << static_cast<s32>(registers.rsi);
        out << ",\"command_variant_argument\":" << static_cast<s32>(registers.rdx);
    } else if (site.offset == 0x013CB870) {
        out << ",\"source_entity_id\":" << static_cast<s32>(registers.rdi);
        out << ",\"target_object_entity_id\":" << static_cast<s32>(registers.rsi);
        out << ",\"target_dummypoly_id\":" << static_cast<s32>(registers.rdx);
        out << ",\"command_variant_argument\":" << static_cast<s32>(registers.rcx);
    } else if (site.offset == 0x013CCBD0) {
        out << ",\"source_entity_id\":" << static_cast<s32>(registers.rdx);
        out << ",\"target_character_entity_id\":" << static_cast<s32>(registers.rdi);
        out << ",\"target_dummypoly_id\":" << static_cast<s32>(registers.rsi);
    } else {
        out << ",\"character_object\":";
        WriteHex(out, registers.rdi);
        out << ",\"floor_object\":";
        WriteHex(out, registers.rsi);
    }
    out << '}';
}

void WriteSummonSelection(std::ostream& out, const TraceSite& site,
                          const GuestRegisterSnapshot& registers) {
    const bool is_scan = site.offset == 0x01873269;
    const bool is_match = site.offset == 0x018732DD;
    const bool is_queue_insert = site.offset == 0x01875320;
    const bool is_queued = site.offset == 0x01873605;
    const bool is_start = site.offset == 0x014BAEC0;
    const bool is_allocated = site.offset == 0x014BAFE3;
    const u64 manager =
        is_scan ? registers.rax : (is_allocated ? registers.r12 : GetSummonManager());
    const u64 status =
        is_scan && registers.rbp >= 0x6F0 + 0x10000
            ? ReadValue<u64>(registers.rbp - 0x6F0, 0)
            : ((is_match ? registers.r15
                         : (is_queue_insert ? registers.rdi : (is_queued ? registers.rbx : 0))));

    out << ",\"summon_selection\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"manager\":";
    WriteHex(out, manager);
    out << ",\"status\":";
    WriteHex(out, status);

    const u64 matching_state = GetMatchingState();
    out << ",\"matching_state\":";
    WriteHex(out, matching_state);
    if (matching_state >= 0x10000) {
        out << ",\"matching_state_124\":" << ReadValue<s32>(matching_state, 0x124);
    }

    if (is_scan || is_match) {
        out << ",\"selection_mask\":";
        WriteHex(out, static_cast<u32>(registers.r13));
    }
    if (manager >= 0x10000) {
        const u64 candidate_head = ReadValue<u64>(manager, 0x30);
        out << ",\"candidate_head\":";
        WriteHex(out, candidate_head);
        out << ",\"candidate_count\":" << ReadValue<u64>(manager, 0x38);
        const u64 pending_head = ReadValue<u64>(manager, 0x1A8);
        out << ",\"pending_head\":";
        WriteHex(out, pending_head);
        out << ",\"pending_count\":" << ReadValue<u64>(manager, 0x1B0);
        out << ",\"request_handle\":";
        WriteHex(out, ReadValue<u64>(manager, 0x138));
        if (pending_head >= 0x10000) {
            const u64 node = ReadValue<u64>(pending_head, 0);
            out << ",\"pending_first\":";
            WriteHex(out, node == pending_head ? 0 : node);
            if (node >= 0x10000 && node != pending_head) {
                const u64 item = ReadValue<u64>(node, 0x10);
                out << ",\"pending_item\":";
                WriteHex(out, item);
                if (item >= 0x10000) {
                    const u64 candidate = ReadValue<u64>(item, 0);
                    const u64 prepared = ReadValue<u64>(item, 0x08);
                    out << ",\"pending_state\":" << static_cast<u32>(ReadValue<u8>(item, 0x10));
                    out << ",\"pending_candidate\":";
                    WriteHex(out, candidate);
                    out << ",\"pending_prepared\":";
                    WriteHex(out, prepared);
                    if (candidate >= 0x10000) {
                        const s32 role_code = ReadValue<s8>(candidate, 0x08);
                        out << ",\"pending_role_code\":" << role_code;
                        WriteRoleSelectionMask(out, "pending_role_selection_mask", role_code);
                    }
                    if (prepared >= 0x10000) {
                        out << ",\"pending_prepared_role_code\":"
                            << static_cast<u32>(ReadValue<u8>(prepared, 0x22));
                    }
                }
            }
        }
    }
    if (is_match && registers.rsi >= 0x10000) {
        const u64 candidate = ReadValue<u64>(registers.rsi, 0);
        const u64 prepared = ReadValue<u64>(registers.rsi, 0x08);
        out << ",\"matched_item\":";
        WriteHex(out, registers.rsi);
        out << ",\"matched_candidate\":";
        WriteHex(out, candidate);
        out << ",\"matched_prepared\":";
        WriteHex(out, prepared);
        if (candidate >= 0x10000) {
            const s32 role_code = ReadValue<s8>(candidate, 0x08);
            out << ",\"matched_role_code\":" << role_code;
            WriteRoleSelectionMask(out, "matched_role_selection_mask", role_code);
        }
        if (prepared >= 0x10000) {
            out << ",\"matched_prepared_role_code\":"
                << static_cast<u32>(ReadValue<u8>(prepared, 0x22));
        }
    }
    if (status >= 0x10000) {
        out << ",\"queued_count\":" << ReadValue<u64>(status, 0x78);
        out << ",\"request_state_88\":" << ReadValue<s32>(status, 0x88);
        out << ",\"active_request_id\":" << ReadValue<s32>(status, 0x90);
    }
    if (is_start) {
        out << ",\"target_id\":" << static_cast<u32>(registers.rdi);
        out << ",\"session_id\":" << static_cast<u32>(registers.rsi);
        out << ",\"request_context\":";
        WriteHex(out, registers.rdx);
    } else if (is_allocated) {
        out << ",\"allocated_handle\":";
        WriteHex(out, registers.rax);
    } else if (is_queue_insert) {
        out << ",\"queue_object\":";
        WriteHex(out, registers.rsi);
    }
    out << '}';
}

void WriteSummonBuild(std::ostream& out, const TraceSite& site,
                      const GuestRegisterSnapshot& registers) {
    u64 prepared{};
    switch (site.offset) {
    case SummonBuildEntryOffset:
        prepared = registers.rsi;
        break;
    case 0x01874C94:
        prepared = registers.r15;
        break;
    case 0x01874E5A:
        prepared = registers.rbx;
        break;
    case 0x018750F7:
        prepared = registers.rbx;
        break;
    default:
        prepared = registers.r8;
        break;
    }

    out << ",\"summon_build\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"prepared\":";
    WriteHex(out, prepared);
    if (site.offset == SummonBuildEntryOffset) {
        if (host_placement_rewrite.prepared == prepared) {
            const auto write_placement = [&out](std::string_view name,
                                                const SummonPlacementDescriptor& placement) {
                out << ",\"" << name << "\":{";
                out << "\"packed_region\":";
                WriteHex(out, placement.packed_region);
                out << ",\"area\":" << placement.area;
                out << ",\"x\":" << placement.x;
                out << ",\"y\":" << placement.y;
                out << ",\"z\":" << placement.z;
                out << ",\"heading\":" << placement.heading << '}';
            };
            out << ",\"host_placement_rewrite\":{\"result\":\"" << host_placement_rewrite.result
                << '\"';
            write_placement("candidate", host_placement_rewrite.candidate);
            write_placement("host", host_placement_rewrite.host);
            out << '}';
        }
        out << ",\"status\":";
        WriteHex(out, registers.rdi);
        out << ",\"destructive_reject\":" << static_cast<u32>(registers.rdx & 0xFF);
        if (registers.rdi >= 0x10000) {
            out << ",\"status_extra_players\":" << ReadValue<s32>(registers.rdi, 0x1C8);
        }

        const u64 matching_state = GetMatchingState();
        out << ",\"matching_state\":";
        WriteHex(out, matching_state);
        if (matching_state >= 0x10000) {
            out << ",\"matching_state_124\":" << ReadValue<s32>(matching_state, 0x124);
        }

        const u64 session_rules = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
        out << ",\"session_rules\":";
        WriteHex(out, session_rules);
        if (session_rules >= 0x10000) {
            out << ",\"session_capacity\":" << ReadValue<s32>(session_rules, 0x0C);
        }

        const u64 global_state = ReadValue<u64>(image_base + GlobalStatePointerOffset, 0);
        const u64 population = global_state >= 0x10000 ? ReadValue<u64>(global_state, 0x16F8) : 0;
        out << ",\"global_state\":";
        WriteHex(out, global_state);
        out << ",\"online_ready\":" << static_cast<u32>(ReadValue<u8>(global_state, 0x1592));
        out << ",\"population\":";
        WriteHex(out, population);
        if (population >= 0x10000) {
            out << ",\"population_08\":" << ReadValue<s32>(population, 0x08);
            out << ",\"population_0c\":" << ReadValue<s32>(population, 0x0C);
            out << ",\"population_10\":" << ReadValue<s32>(population, 0x10);
            out << ",\"population_14\":" << ReadValue<s32>(population, 0x14);
            out << ",\"population_request_98\":" << ReadValue<s32>(population, 0x98);
        }

        const u64 local_state = ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
        const u64 local_world = local_state >= 0x10000 ? ReadValue<u64>(local_state, 0x60) : 0;
        out << ",\"local_state\":";
        WriteHex(out, local_state);
        out << ",\"local_world\":";
        WriteHex(out, local_world);
        if (local_world >= 0x10000) {
            out << ",\"local_world_state\":" << ReadValue<s32>(local_world, 0x78);
        }

        const u64 manager_root = GetSummonManagerRoot();
        out << ",\"manager_root\":";
        WriteHex(out, manager_root);
        if (manager_root >= 0x10000) {
            out << ",\"manager_area\":" << ReadValue<s32>(manager_root, 0xA80);
            out << ",\"manager_flags_b14\":";
            WriteHex(out, ReadValue<u8>(manager_root, 0xB14));
        }
    }
    if (site.offset == 0x018750F7) {
        out << ",\"result\":";
        WriteHex(out, registers.rbx);
    }

    if (prepared >= 0x10000) {
        const u32 role_code = ReadValue<u8>(prepared, 0x22);
        out << ",\"role_code\":" << role_code;
        out << ",\"packed_region\":";
        WriteHex(out, ReadValue<u32>(prepared, 0x04));
        out << ",\"area\":" << ReadValue<s32>(prepared, 0x18);
        out << ",\"request_id\":" << ReadValue<s32>(prepared, 0x88);

        if (role_code < RoleMetadataCount) {
            const u64 metadata = image_base + RoleMetadataTableOffset +
                                 static_cast<u64>(role_code) * RoleMetadataStride;
            const u64 build_role =
                image_base + SummonBuildRoleTableOffset + static_cast<u64>(role_code) * 12;
            out << ",\"selection_mask\":";
            WriteHex(out, ReadValue<u32>(metadata, 0x04));
            out << ",\"role_flags\":";
            WriteHex(out, ReadValue<u8>(metadata, 0x08));
            out << ",\"capacity_class\":" << ReadValue<s32>(metadata, 0x10);
            out << ",\"role_rule\":" << static_cast<u32>(ReadValue<u8>(metadata, 0x14));
            out << ",\"delay_enabled\":" << static_cast<u32>(ReadValue<u8>(build_role, 0));
            out << ",\"delay_seconds\":" << ReadValue<float>(build_role, 0x04);
        }
    }
    out << '}';
}

void WriteMessengerState(std::ostream& out, u64 object) {
    if (object < 0x10000) {
        return;
    }
    out << ",\"messenger\":{";
    out << "\"request\":";
    WriteHex(out, ReadValue<u64>(object, 0xE0));
    out << ",\"waiting\":" << static_cast<u32>(ReadValue<u8>(object, 0xF0));
    out << ",\"flags_144\":";
    WriteHex(out, ReadValue<u8>(object, 0x144));
    out << '}';
}

bool HasActionTransition(u64 object) {
    if (object < 0x10000) {
        return false;
    }
    const u64 current = ReadValue<u64>(object, 0x10);
    const u64 previous = ReadValue<u64>(object, 0x18);
    return current != previous;
}

void WriteActionFlags(std::ostream& out, u64 object, u64 stack_pointer) {
    if (object < 0x10000 || stack_pointer < 0x10000) {
        return;
    }
    const u64 current = ReadValue<u64>(object, 0x10);
    const u64 previous = ReadValue<u64>(object, 0x18);
    const u64 return_address = ReadValue<u64>(stack_pointer, 0);
    out << ",\"action_flags\":{";
    out << "\"module\":";
    WriteHex(out, object);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '\"';
    }
    out << ",\"current\":";
    WriteHex(out, current);
    out << ",\"previous\":";
    WriteHex(out, previous);
    out << ",\"pressed\":";
    WriteHex(out, current & ~previous);
    out << ",\"released\":";
    WriteHex(out, previous & ~current);
    out << ",\"field_30\":";
    WriteHex(out, ReadValue<u64>(object, 0x30));
    out << ",\"field_38\":";
    WriteHex(out, ReadValue<u64>(object, 0x38));
    out << ",\"field_40\":";
    WriteHex(out, ReadValue<u64>(object, 0x40));
    out << ",\"field_48\":";
    WriteHex(out, ReadValue<u64>(object, 0x48));
    out << ",\"state_90\":";
    WriteHex(out, ReadValue<u32>(object, 0x90));
    out << ",\"state_94\":";
    WriteHex(out, ReadValue<u32>(object, 0x94));
    out << ",\"flags_9c\":";
    WriteHex(out, ReadValue<u8>(object, 0x9C));
    out << '}';
}

void WritePlayerItemState(std::ostream& out, u64 player) {
    out << "\"player\":";
    WriteHex(out, player);
    if (player >= 0x10000) {
        const u64 vtable = ReadValue<u64>(player, 0);
        out << ",\"vtable\":";
        WriteHex(out, vtable);
        if (vtable >= 0x10000) {
            out << ",\"vfunc_1c0\":";
            WriteHex(out, ReadValue<u64>(vtable, 0x1C0));
            out << ",\"vfunc_1c8\":";
            WriteHex(out, ReadValue<u64>(vtable, 0x1C8));
        }
        out << ",\"entity_60\":";
        WriteHex(out, ReadValue<u64>(player, 0x60));
        out << ",\"handle_90\":";
        WriteHex(out, ReadValue<u32>(player, 0x90));
        out << ",\"active_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x1C0));
        out << ",\"resolved_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x478));
        out << ",\"override_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x47C));
        out << ",\"queued_goods\":";
        WriteHex(out, ReadValue<u32>(player, 0x488));
        out << ",\"item_state\":";
        WriteHex(out, ReadValue<u32>(player, 0x490));
        WriteMultiplayerEffects(out, player);
    }
}

void WriteUseItemState(std::ostream& out, const TraceSite& site,
                       const GuestRegisterSnapshot& registers) {
    out << ",\"use_item\":{";
    WritePlayerItemState(out, registers.r13);
    out << ",\"rax\":";
    WriteHex(out, registers.rax);
    out << ",\"eax_signed\":" << static_cast<s64>(static_cast<s32>(registers.rax));
    if (site.offset == 0x018F751C && registers.rax >= 0x10000) {
        const u64 current = ReadValue<u64>(registers.rax, 0x10);
        const u64 previous = ReadValue<u64>(registers.rax, 0x18);
        out << ",\"action_current\":";
        WriteHex(out, current);
        out << ",\"action_previous\":";
        WriteHex(out, previous);
        out << ",\"action_pressed\":";
        WriteHex(out, ReadValue<u64>(registers.rax, 0x20));
    }
    if (site.offset == 0x018F7529 && registers.rax >= 0x10000) {
        out << ",\"inventory\":";
        WriteHex(out, registers.rax);
        out << ",\"inventory_selection\":[";
        for (size_t index = 0; index < 8; ++index) {
            if (index != 0) {
                out << ',';
            }
            WriteHex(out, ReadValue<u64>(registers.rax, 0x1D0 + index * sizeof(u64)));
        }
        out << ']';
    }
    out << '}';
}

bool IsTrackedGoods(s32 goods_id) {
    constexpr std::array TrackedGoods{100, 111, 200, 201, 205, 206, 225, 226};
    return std::ranges::find(TrackedGoods, goods_id) != TrackedGoods.end();
}

s32 GetUseItemExecutionGoods(const TraceSite& site, const GuestRegisterSnapshot& registers) {
    if (site.offset == 0x018C9364) {
        return registers.rbp >= 0xD8 ? ReadValue<s32>(registers.rbp - 0xD8, 0) : -1;
    }
    return static_cast<s32>(registers.rsi);
}

void WriteUseItemExecution(std::ostream& out, const TraceSite& site,
                           const GuestRegisterSnapshot& registers) {
    const bool is_result = site.offset == 0x018C9364;
    const u64 player = is_result ? registers.r14 : registers.rdi;
    const s32 goods_id = GetUseItemExecutionGoods(site, registers);
    const s32 argument = static_cast<s32>(is_result ? registers.rbx : registers.rdx);
    out << ",\"use_item_execution\":{";
    WritePlayerItemState(out, player);
    out << ",\"goods_id\":" << goods_id;
    out << ",\"argument\":" << argument;
    if (is_result) {
        out << ",\"result\":" << static_cast<u32>(static_cast<u8>(registers.r12));
    }
    out << '}';
}

void WriteGoodsActionSubmit(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"goods_action\":{";
    out << "\"player\":";
    WriteHex(out, registers.rdi);
    out << ",\"goods_id\":" << static_cast<s32>(registers.rsi);
    out << ",\"argument\":" << static_cast<s32>(registers.rdx);
    out << ",\"sp_effect_id\":" << static_cast<s32>(registers.rcx);
    out << ",\"executor_allowed\":" << static_cast<u32>(static_cast<u8>(registers.r8));
    WriteMultiplayerEffects(out, registers.rdi);
    out << '}';
}

void WriteGoodsParamLookup(std::ostream& out, const GuestRegisterSnapshot& registers) {
    const s32 goods_id = static_cast<s32>(registers.rsi);
    const u64 return_address = ReadValue<u64>(registers.rsp, 0);
    out << ",\"goods_lookup\":{";
    out << "\"goods_id\":" << goods_id;
    out << ",\"sos_area\":" << observed_sos_area.load(std::memory_order_relaxed);
    out << ",\"result_storage\":";
    WriteHex(out, registers.rdi);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '\"';
    }
    out << '}';
}

void WriteBellAvailability(std::ostream& out, const TraceSite& site,
                           const GuestRegisterSnapshot& registers) {
    const bool is_native_result = site.offset == 0x0157F962;
    const u64 selected_area = static_cast<u8>(registers.rax) != 0 ? registers.rcx : registers.rbx;
    const s32 area =
        is_native_result ? static_cast<s32>(registers.rbx) : ReadValue<s32>(selected_area, 0);
    out << ",\"bell_availability\":{";
    out << "\"area\":" << area;
    out << ",\"observed_sos_area\":" << observed_sos_area.load(std::memory_order_relaxed);
    out << ",\"player\":";
    WriteHex(out, registers.r14);
    if (is_native_result) {
        out << ",\"native_blocked\":" << static_cast<u32>(static_cast<u8>(registers.rax));
    }
    WriteMultiplayerEffects(out, registers.r14);
    out << '}';
}

void WriteResponderBellAvailability(std::ostream& out, const TraceSite& site,
                                    const GuestRegisterSnapshot& registers, s32 goods_id) {
    out << ",\"responder_bell_availability\":{";
    out << "\"stage\":\"" << site.name << "\"";
    out << ",\"goods_id\":" << goods_id;
    out << ",\"player\":";
    WriteHex(out, registers.r14);
    out << ",\"original_goods_argument\":" << static_cast<s32>(registers.rbx);
    out << ",\"resolved_goods_id\":" << static_cast<s32>(registers.r12);
    if (site.offset == 0x015800D1) {
        out << ",\"final_result\":" << static_cast<u32>(static_cast<u8>(registers.rcx));
    }
    out << '}';
    if (site.offset == 0x0157F686) {
        WriteMultiplayerEffects(out, registers.r14);
    }
}

void WriteQwords(std::ostream& out, std::string_view key, u64 address, size_t count) {
    if (address < 0x10000) {
        return;
    }
    out << ",\"" << key << "\":[";
    for (size_t index = 0; index < count; ++index) {
        if (index != 0) {
            out << ',';
        }
        WriteHex(out, ReadValue<u64>(address, index * sizeof(u64)));
    }
    out << ']';
}

void WriteFloatVector(std::ostream& out, std::string_view key, u64 address) {
    if (address < 0x10000) {
        return;
    }
    out << ",\"" << key << "\":[";
    for (size_t index = 0; index < 4; ++index) {
        if (index != 0) {
            out << ',';
        }
        const float value = ReadValue<float>(address, index * sizeof(float));
        if (std::isfinite(value)) {
            out << std::setprecision(9) << value;
        } else {
            out << "null";
        }
    }
    out << ']';
}

void WriteMapId(std::ostream& out, std::string_view key, u64 address) {
    if (address < 0x10000) {
        return;
    }
    const u32 map_id = ReadValue<u32>(address, 0);
    out << ",\"" << key << "\":";
    WriteHex(out, map_id);
    out << ",\"" << key << "_parts\":[" << ((map_id >> 24) & 0xFF) << ',' << ((map_id >> 16) & 0xFF)
        << ',' << ((map_id >> 8) & 0xFF) << ',' << (map_id & 0xFF) << ']';
}

void WriteU32(std::ostream& out, std::string_view key, u64 address) {
    if (address < 0x10000) {
        return;
    }
    out << ",\"" << key << "\":";
    WriteHex(out, ReadValue<u32>(address, 0));
}

void WritePlacementState(std::ostream& out, u64 state) {
    WriteQwords(out, "global_qwords", state, 6);
    WriteQwords(out, "sos_placement_qwords", state + 0x14A0, 5);
    WriteQwords(out, "summoned_placement_qwords", state + 0x14D0, 5);
    WriteQwords(out, "warp_qwords", state + 0x1518, 3);
    WriteQwords(out, "received_placement_qwords", state + 0x1620, 6);
}

void WriteStageWarpState(std::ostream& out, u64 state) {
    if (state < 0x10000) {
        return;
    }
    out << ",\"stage_warp_state\":{";
    out << "\"state\":";
    WriteHex(out, state);
    out << ",\"request_active\":" << static_cast<u32>(ReadValue<u8>(state, 0x08));
    const u32 map_id = ReadValue<u32>(state, 0x0C);
    out << ",\"map_id\":";
    WriteHex(out, map_id);
    out << ",\"map_parts\":[" << ((map_id >> 24) & 0xFF) << ',' << ((map_id >> 16) & 0xFF) << ','
        << ((map_id >> 8) & 0xFF) << ',' << (map_id & 0xFF) << ']';
    out << ",\"warp_info_id\":" << ReadValue<s32>(state, 0x10);
    out << ",\"mode_1538\":" << ReadValue<s32>(state, 0x1538);
    out << ",\"respawn_point_id_153c\":" << ReadValue<s32>(state, 0x153C);
    out << ",\"auxiliary_id_1540\":" << ReadValue<s32>(state, 0x1540);
    out << ",\"warp_fields_153c\":";
    WriteHex(out, ReadValue<u64>(state, 0x153C));
    out << ",\"state_1592\":" << static_cast<u32>(ReadValue<u8>(state, 0x1592));
    out << ",\"transition_16f8\":";
    WriteHex(out, ReadValue<u64>(state, 0x16F8));
    out << '}';
}

void WriteStageWarp(std::ostream& out, const TraceSite& site,
                    const GuestRegisterSnapshot& registers) {
    out << ",\"stage_warp\":{";
    if (site.offset == 0x0132E010) {
        const u32 map_id = (static_cast<u32>(registers.rsi) & 0xFF) << 24 |
                           (static_cast<u32>(registers.rdx) & 0xFF) << 16 |
                           (static_cast<u32>(registers.rcx) & 0xFF) << 8 |
                           (static_cast<u32>(registers.r8) & 0xFF);
        out << "\"source\":\"event\",\"map_id\":";
        WriteHex(out, map_id);
        out << ",\"map_parts\":[" << (registers.rsi & 0xFF) << ',' << (registers.rdx & 0xFF) << ','
            << (registers.rcx & 0xFF) << ',' << (registers.r8 & 0xFF) << ']';
        out << ",\"warp_info_id\":" << static_cast<s32>(registers.r9);
    } else if (site.offset == 0x0132E050) {
        const s32 warp_info_id = static_cast<s32>(registers.rsi);
        const s32 area = warp_info_id / 100000;
        const s32 block = (warp_info_id / 10000) % 10;
        out << "\"source\":\"bonfire_event\",\"warp_info_id\":" << warp_info_id;
        out << ",\"derived_map_parts\":[" << area << ',' << block << ",0,0]";
    } else {
        out << "\"source\":\"transition\"";
    }
    const u64 player = GetLocalPlayer();
    out << ",\"current_map\":";
    WriteHex(out, GetCurrentPackedMap());
    out << ",\"player\":";
    WriteHex(out, player);
    out << ",\"host_effect_9001\":" << (HasPlayerEffect(player, 9001) ? "true" : "false");
    out << '}';
    WriteStageWarpState(out, GetGlobalState());
}

void WriteStageWarpDescriptor(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"stage_warp_descriptor\":{";
    out << "\"descriptor\":";
    WriteHex(out, registers.r9);
    out << ",\"mode\":" << static_cast<s32>(registers.r13);
    if (registers.r9 >= 0x10000) {
        out << ",\"request_type\":" << ReadValue<s32>(registers.r9, 0x54);
        out << ",\"request_subtype\":" << ReadValue<s32>(registers.r9, 0x58);
        out << ",\"descriptor_mode\":" << ReadValue<s32>(registers.r9, 0x98);
        out << ",\"previous_respawn_point\":" << ReadValue<s32>(registers.r9, 0x80);
        out << ",\"previous_auxiliary_id\":" << ReadValue<s32>(registers.r9, 0x84);
    }
    out << '}';
    WriteStageWarpState(out, registers.rax);
}

void WriteHealingFountainRegistration(std::ostream& out, const GuestRegisterSnapshot& registers) {
    float reaction_distance{};
    float reaction_angle{};
    std::memcpy(&reaction_distance, registers.ymm[0].data(), sizeof(reaction_distance));
    std::memcpy(&reaction_angle, registers.ymm[1].data(), sizeof(reaction_angle));

    out << ",\"healing_fountain_registration\":{";
    out << "\"manager\":";
    WriteHex(out, registers.rdi);
    out << ",\"event_flag\":" << static_cast<s32>(registers.rsi);
    out << ",\"entity_id\":" << static_cast<s32>(registers.rdx);
    out << ",\"reaction_distance\":";
    if (std::isfinite(reaction_distance)) {
        out << std::setprecision(9) << reaction_distance;
    } else {
        out << "null";
    }
    out << ",\"reaction_angle\":";
    if (std::isfinite(reaction_angle)) {
        out << std::setprecision(9) << reaction_angle;
    } else {
        out << "null";
    }
    out << ",\"initial_sword_number\":" << static_cast<s32>(registers.r8);
    out << ",\"sword_level\":" << static_cast<s32>(registers.rcx);
    out << '}';
}

void WriteHealingFountainAvailability(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"healing_fountain_availability\":{";
    out << "\"result\":\"" << healing_fountain_availability.result << '\"';
    out << ",\"object\":";
    WriteHex(out, healing_fountain_availability.object);
    out << ",\"player\":";
    WriteHex(out, healing_fountain_availability.player);
    out << ",\"host_effect_9001\":"
        << (healing_fountain_availability.host_effect ? "true" : "false");
    out << ",\"blocked_before\":"
        << (healing_fountain_availability.blocked_before ? "true" : "false");
    out << ",\"blocked_after\":"
        << (healing_fountain_availability.blocked_after ? "true" : "false");
    if (registers.r13 >= 0x10000 && HasMemoryAccess(registers.r13, 0x4C, MemoryProt::CpuRead)) {
        out << ",\"special_map_48\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x48));
        out << ",\"multiplayer_49\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x49));
        out << ",\"entity_missing_4a\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x4A));
        out << ",\"native_gate_4b\":" << static_cast<u32>(ReadValue<u8>(registers.r13, 0x4B));
    }
    out << '}';
}

void WriteChairRespawnNotification(std::ostream& out, const GuestRegisterSnapshot& registers) {
    const u64 task = registers.rdi;
    const u64 manager = GetSummonManagerRoot();
    const u64 online_state =
        image_base != 0 ? ReadValue<u64>(image_base + OnlineStatePointerOffset, 0) : 0;

    out << ",\"chair_messenger_respawn_notify\":{";
    out << "\"task\":";
    WriteHex(out, task);
    if (task >= 0x10000 && HasMemoryAccess(task, 0xDC, MemoryProt::CpuRead)) {
        out << ",\"state_50\":" << ReadValue<s32>(task, 0x50);
        out << ",\"step_54\":" << ReadValue<s32>(task, 0x54);
        out << ",\"wait_active_c8\":" << static_cast<u32>(ReadValue<u8>(task, 0xC8));
        out << ",\"selected_respawn_point_cc\":" << ReadValue<s32>(task, 0xCC);
        const float timer = ReadValue<float>(task, 0xD8);
        out << ",\"timer_d8\":";
        if (std::isfinite(timer)) {
            out << std::setprecision(9) << timer;
        } else {
            out << "null";
        }
    }

    out << ",\"manager\":";
    WriteHex(out, manager);
    if (manager >= 0x10000 && HasMemoryAccess(manager, 0xA84, MemoryProt::CpuRead)) {
        out << ",\"active_08\":" << static_cast<u32>(ReadValue<u8>(manager, 0x08));
        out << ",\"state_9f4\":";
        WriteHex(out, ReadValue<u64>(manager, 0x9F4));
        out << ",\"lan_connected_9f5\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F5));
        out << ",\"lan_disconnected_9f6\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F6));
        out << ",\"signed_in_9f7\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F7));
        out << ",\"signed_out_9f8\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F8));
        out << ",\"state_9f9\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9F9));
        out << ",\"state_9fa\":" << static_cast<u32>(ReadValue<u8>(manager, 0x9FA));
        out << ",\"network_flags_a05\":";
        WriteHex(out, ReadValue<u8>(manager, 0xA05));
        out << ",\"gate_a50\":" << static_cast<u32>(ReadValue<u8>(manager, 0xA50));
        out << ",\"map_a78\":";
        WriteHex(out, ReadValue<u32>(manager, 0xA78));
        out << ",\"area_a80\":" << ReadValue<s32>(manager, 0xA80);
        out << ",\"latch_d30\":" << static_cast<u32>(ReadValue<u8>(manager, 0xD30));
        out << ",\"latch_d31\":" << static_cast<u32>(ReadValue<u8>(manager, 0xD31));
    }
    out << ",\"online_state\":";
    WriteHex(out, online_state);
    if (online_state >= 0x10000 && HasMemoryAccess(online_state, 0x130, MemoryProt::CpuRead)) {
        out << ",\"online_d4\":" << ReadValue<s32>(online_state, 0xD4);
        out << ",\"online_d8\":" << static_cast<u32>(ReadValue<u8>(online_state, 0xD8));
        out << ",\"online_e0\":" << static_cast<u32>(ReadValue<u8>(online_state, 0xE0));
        out << ",\"online_120\":" << ReadValue<s32>(online_state, 0x120);
        out << ",\"online_124\":" << ReadValue<s32>(online_state, 0x124);
        out << ",\"online_128\":" << ReadValue<s32>(online_state, 0x128);
        out << ",\"online_12c\":" << ReadValue<s32>(online_state, 0x12C);
    }
    out << '}';
}

void WriteStageWarpAcknowledge(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"stage_warp_acknowledge\":{";
    out << "\"state\":";
    WriteHex(out, registers.rcx);
    const bool frame_valid = registers.rbp >= 0x198 + 0x10000;
    if (frame_valid) {
        const bool will_clear_request = ReadValue<u8>(registers.rbp - 0x198, 0) != 0;
        out << ",\"will_clear_request\":" << (will_clear_request ? "true" : "false");
    }
    out << '}';
    WriteStageWarpState(out, registers.rcx);
}

void WriteStageWarpTask(std::ostream& out, u64 task, bool include_resolved_transform) {
    if (task < 0x10000) {
        return;
    }

    const u32 map_id = ReadValue<u32>(task, 0x58);
    out << ",\"stage_warp_task\":{";
    out << "\"task\":";
    WriteHex(out, task);
    out << ",\"map_id\":";
    WriteHex(out, map_id);
    out << ",\"map_parts\":[" << ((map_id >> 24) & 0xFF) << ',' << ((map_id >> 16) & 0xFF) << ','
        << ((map_id >> 8) & 0xFF) << ',' << (map_id & 0xFF) << ']';
    out << ",\"request_type\":" << ReadValue<s32>(task, 0xA4);
    out << ",\"request_subtype\":" << ReadValue<s32>(task, 0xA8);
    out << ",\"respawn_point_id\":" << ReadValue<s32>(task, 0xD0);
    out << ",\"auxiliary_id\":" << ReadValue<s32>(task, 0xD4);
    out << ",\"mode\":" << ReadValue<s32>(task, 0xE8);
    out << '}';
    if (include_resolved_transform) {
        WriteFloatVector(out, "resolved_position", task + 0xF0);
        const float facing = ReadValue<float>(task, 0x100);
        out << ",\"resolved_facing\":";
        if (std::isfinite(facing)) {
            out << std::setprecision(9) << facing;
        } else {
            out << "null";
        }
    }
}

void WriteRespawnTransformResolve(std::ostream& out, const GuestRegisterSnapshot& registers) {
    out << ",\"respawn_transform_resolve\":{";
    out << "\"map_resource\":";
    WriteHex(out, registers.rdi);
    out << ",\"position_output\":";
    WriteHex(out, registers.rsi);
    out << ",\"orientation_output\":";
    WriteHex(out, registers.rdx);
    out << ",\"respawn_point_id\":" << static_cast<s32>(registers.rcx);
    out << '}';
}

u64 GetGlobalState() {
    const u64 image_size = MemoryPatcher::g_eboot_image_size;
    if (image_base == 0 || image_size < sizeof(u64) ||
        GlobalStatePointerOffset > image_size - sizeof(u64)) {
        return 0;
    }
    return ReadValue<u64>(image_base + GlobalStatePointerOffset, 0);
}

void WriteWorldStateValidation(std::ostream& out, const TraceSite& site,
                               const GuestRegisterSnapshot& registers) {
    s32 area = -1;
    switch (site.offset) {
    case 0x018C5365:
        area = ReadValue<s32>(registers.rbp - 0x38, 0);
        break;
    case 0x018CF97A:
        area = ReadValue<s32>(registers.rbp - 0x1A8, 0);
        break;
    case 0x0191A980:
    case 0x0191AA71:
        area = static_cast<s32>(registers.rbx);
        break;
    case 0x01AC6557:
    case 0x01AC661E:
        area = ReadValue<s32>(registers.rbx, 0);
        break;
    case 0x01FEB611:
    case 0x01FEBD01:
    case 0x01FEC381:
        area = ReadValue<s32>(registers.rbp - 0x1B8, 0);
        break;
    }

    const u64 return_address = ReadValue<u64>(registers.rbp, sizeof(u64));
    out << ",\"world_state_validation\":{";
    out << "\"call_offset\":\"0x" << std::hex << site.offset << std::dec << '"';
    out << ",\"result_low8\":" << static_cast<u32>(static_cast<u8>(registers.rax));
    out << ",\"area\":" << area;
    out << ",\"manager_area\":";
    const u64 root = GetSummonManagerRoot();
    out << (root >= 0x10000 ? ReadValue<s32>(root, 0xA80) : -1);
    out << ",\"return_address\":";
    WriteHex(out, return_address);
    if (return_address >= image_base &&
        return_address - image_base < MemoryPatcher::g_eboot_image_size) {
        out << ",\"caller_offset\":\"0x" << std::hex << return_address - image_base << std::dec
            << '"';
    }
    out << '}';
}

void PS4_SYSV_ABI TraceEntry(u64 tag, const GuestRegisterSnapshot* registers) {
    if (tag >= Sites.size() || registers == nullptr) {
        return;
    }

    const auto& site = Sites[tag];
    if (summon_build_host_placement_hook_installed && site.offset == SummonBuildEntryOffset) {
        ApplyCrossMapSummonHostPlacement(*registers);
    }
    if (cross_map_guest_handoff_hook_installed && site.offset == CrossMapGuestHandoffOffset) {
        ApplyCrossMapSummonGuestPlacement(*registers);
    }
    if (summon_reload_state_hook_installed && site.offset == StageWarpDescriptorFinalizeOffset) {
        ApplySummonReloadStateAtDescriptor(*registers);
    }
    if (deferred_summon_reload_hook_installed && site.offset == SosStatusUpdateOffset) {
        RefreshSeamlessLocalPlacement();
        ApplyPreMatchCrossMapGuestWarp();
        ApplyDeferredSummonReload();
        ResumePreMatchCrossMapResponder();
        TickPartyController();
    }
    if (healing_fountain_host_availability_hook_installed &&
        site.offset == HealingFountainAvailabilityOffset) {
        ApplyHealingFountainHostAvailability(*registers);
    }
    if (world_teardown_hook_installed && site.offset == WorldTeardownMatchingOffset) {
        EndSessionOnWorldTeardown();
    }
    if (ghost_death_revive_hook_installed && site.offset == GhostDeathReviveOffset) {
        ReviveGuestAfterGhostDeath();
    }
    if (site.kind == TraceKind::ActionFlags && !HasActionTransition(registers->rdi)) {
        return;
    }
    if (site.kind == TraceKind::GoodsParamLookup &&
        !IsTrackedGoods(static_cast<s32>(registers->rsi))) {
        return;
    }
    if (site.offset == CharacterWarpSetOrCopyFloorOffset) {
        const u64 return_address = ReadValue<u64>(registers->rsp, 0);
        if (return_address >= image_base) {
            const u64 return_offset = return_address - image_base;
            if (std::ranges::contains(BackgroundFloorCopyReturnOffsets, return_offset)) {
                return;
            }
        }
    }
    if ((site.kind == TraceKind::UseItemExecution || site.kind == TraceKind::GoodsActionSubmit) &&
        !IsTrackedGoods(GetUseItemExecutionGoods(site, *registers))) {
        return;
    }
    if (site.kind == TraceKind::SosValidityCode) {
        observed_sos_area.store(static_cast<s32>(registers->rax), std::memory_order_relaxed);
    }
    s32 responder_goods = -1;
    if (site.kind == TraceKind::ResponderBellAvailability) {
        if (site.offset == 0x0157F686) {
            responder_goods = static_cast<s32>(registers->r12);
            if (responder_goods != 205 && responder_goods != 225) {
                return;
            }
            responder_availability_frame = registers->rbp;
            responder_availability_goods = responder_goods;
        } else {
            if (responder_availability_frame != registers->rbp) {
                return;
            }
            responder_goods = responder_availability_goods;
            if (site.offset == 0x015800D1) {
                responder_availability_frame = 0;
                responder_availability_goods = -1;
            }
        }
    }

    const u64 hit = site_hits[tag].fetch_add(1, std::memory_order_relaxed) + 1;
    const u64 dense_capture_limit =
        site.kind == TraceKind::ActionFlags || site.kind == TraceKind::GoodsParamLookup ? 2048 : 64;
    if (hit > dense_capture_limit && hit % 300 != 0) {
        return;
    }

    const u64 sequence = event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    try {
        std::scoped_lock lock{capture_mutex};
        if (!capture_file) {
            return;
        }
        capture_file << "{\"type\":\"entry\",\"seq\":" << sequence << ",\"time_ms\":" << timestamp
                     << ",\"site\":\"" << site.name << "\",\"offset\":\"0x" << std::hex
                     << site.offset << std::dec << "\",\"hit\":" << hit << ",\"registers\":{";
        capture_file << "\"rax\":";
        WriteHex(capture_file, registers->rax);
        capture_file << ",\"rdi\":";
        WriteHex(capture_file, registers->rdi);
        capture_file << ",\"rsi\":";
        WriteHex(capture_file, registers->rsi);
        capture_file << ",\"rdx\":";
        WriteHex(capture_file, registers->rdx);
        capture_file << ",\"rcx\":";
        WriteHex(capture_file, registers->rcx);
        capture_file << ",\"r8\":";
        WriteHex(capture_file, registers->r8);
        capture_file << ",\"r9\":";
        WriteHex(capture_file, registers->r9);
        capture_file << ",\"rsp\":";
        WriteHex(capture_file, registers->rsp);
        capture_file << '}';

        switch (site.kind) {
        case TraceKind::SosCondition:
            WriteQwords(capture_file, "condition_qwords", registers->rdi, 8);
            break;
        case TraceKind::SosValidityDispatch: {
            capture_file << ",\"validity_dispatch\":{";
            capture_file << "\"player\":";
            WriteHex(capture_file, registers->rbx);
            const u64 vtable = ReadValue<u64>(registers->rbx, 0);
            const u64 function = ReadValue<u64>(vtable, 0x628);
            capture_file << ",\"vtable\":";
            WriteHex(capture_file, vtable);
            capture_file << ",\"function\":";
            WriteHex(capture_file, function);
            if (function >= image_base &&
                function - image_base < MemoryPatcher::g_eboot_image_size) {
                capture_file << ",\"function_offset\":\"0x" << std::hex << function - image_base
                             << std::dec << '\"';
            }
            capture_file << '}';
            break;
        }
        case TraceKind::SosValidityCode:
            capture_file << ",\"validity_code\":{";
            capture_file << "\"previous\":" << static_cast<s64>(static_cast<s32>(registers->r15));
            capture_file << ",\"current\":" << static_cast<s64>(static_cast<s32>(registers->rax));
            capture_file << ",\"object\":";
            WriteHex(capture_file, registers->r14);
            capture_file << ",\"source\":";
            WriteHex(capture_file, registers->rbx);
            capture_file << '}';
            WriteQwords(capture_file, "condition_qwords", registers->r14, 6);
            break;
        case TraceKind::SosEvent: {
            const u64 state = GetGlobalState();
            capture_file << ",\"sos_event\":{";
            capture_file << "\"global_state\":";
            WriteHex(capture_file, state);
            if (state >= 0x10000) {
                capture_file << ",\"warp_1520\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1520));
                capture_file << ",\"menu_1544\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1544));
                capture_file << ",\"transition_1547\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1547));
                capture_file << ",\"active_1578\":";
                WriteHex(capture_file, ReadValue<u8>(state, 0x1578));
            }
            capture_file << '}';
            WritePlacementState(capture_file, state);
            break;
        }
        case TraceKind::MatchingEvent:
            WriteQwords(capture_file, "event_qwords", registers->rdx, 6);
            break;
        case TraceKind::MatchingSignal:
            break;
        case TraceKind::MatchingLeaveDecision: {
            const u64 return_address = ReadValue<u64>(registers->rsp, 0);
            const u64 reload_state =
                ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
            capture_file << ",\"matching_leave_decision\":{";
            const bool multi_play_site = site.offset == MultiPlayStageUidPolicyOffset ||
                                         site.offset == MultiPlayStopRequestOffset;
            const bool stop_task_site = site.offset == MatchingStopTaskEnqueueOffset;
            const u64 object = registers->rdi;
            capture_file << "\"object\":";
            WriteHex(capture_file, object);
            capture_file << ",\"return_address\":";
            WriteHex(capture_file, return_address);
            capture_file << ",\"caller_offset\":";
            WriteHex(capture_file, return_address >= image_base ? return_address - image_base : 0);
            capture_file << ",\"reload_state\":";
            WriteHex(capture_file, reload_state);
            capture_file << ",\"reload_phase\":"
                         << (reload_state >= 0x10000 ? ReadValue<s32>(reload_state, 0x84) : -1);
            capture_file << ",\"current_map\":";
            WriteHex(capture_file, GetCurrentPackedMap());
            if (multi_play_site && object >= 0x10000 &&
                HasMemoryAccess(object, 0x288, MemoryProt::CpuRead)) {
                const u64 matching_controller = ReadValue<u64>(object, 0x18);
                capture_file << ",\"multi_play_state\":" << ReadValue<s32>(object, 0x124);
                capture_file << ",\"multi_play_aux_state\":" << ReadValue<s32>(object, 0x120);
                capture_file << ",\"matching_controller\":";
                WriteHex(capture_file, matching_controller);
                capture_file << ",\"stop_completion_task\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0x280));

                if (site.offset == MultiPlayStageUidPolicyOffset) {
                    const u64 local_state_root =
                        ReadValue<u64>(image_base + CandidateLocalStatePointerOffset, 0);
                    const u64 local_state =
                        local_state_root >= 0x10000 &&
                                HasMemoryAccess(local_state_root, 0x68, MemoryProt::CpuRead)
                            ? ReadValue<u64>(local_state_root, 0x60)
                            : 0;
                    const u64 stage_manager =
                        registers->rbp >= 0x148 + 0x10000 &&
                                HasMemoryAccess(registers->rbp - 0x148, sizeof(u64),
                                                MemoryProt::CpuRead)
                            ? ReadValue<u64>(registers->rbp - 0x148, 0)
                            : 0;
                    capture_file << ",\"stage_uid\":";
                    WriteHex(capture_file, static_cast<u32>(registers->r13));
                    capture_file << ",\"cached_stage_uid\":";
                    WriteHex(capture_file, local_state >= 0x10000 &&
                                                   HasMemoryAccess(local_state + 0x3F8, sizeof(u32),
                                                                   MemoryProt::CpuRead)
                                               ? ReadValue<u32>(local_state, 0x3F8)
                                               : 0);
                    capture_file << ",\"stage_manager\":";
                    WriteHex(capture_file, stage_manager);
                }
            } else if (stop_task_site && object >= 0x10000 &&
                       HasMemoryAccess(object, 0x408, MemoryProt::CpuRead)) {
                capture_file << ",\"stop_reason\":";
                WriteHex(capture_file, static_cast<u32>(registers->rsi));
                capture_file << ",\"matching_state\":" << ReadValue<s32>(object, 0x118);
                capture_file << ",\"controller_operation\":" << ReadValue<s32>(object, 0x3C0);
                capture_file << ",\"room_id\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0x400));
            } else if (object >= 0x10000 && HasMemoryAccess(object, 0x460, MemoryProt::CpuRead)) {
                capture_file << ",\"vtable\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0));
                capture_file << ",\"matching_state\":" << ReadValue<s32>(object, 0x118);
                capture_file << ",\"room_id\":";
                WriteHex(capture_file, ReadValue<u64>(object, 0x400));
            }
            capture_file << ",\"frame_chain\":[";
            u64 frame = registers->rbp;
            for (size_t index = 0; index < 8 && frame >= 0x10000 &&
                                   HasMemoryAccess(frame, 2 * sizeof(u64), MemoryProt::CpuRead);
                 ++index) {
                if (index != 0) {
                    capture_file << ',';
                }
                const u64 frame_return = ReadValue<u64>(frame, sizeof(u64));
                WriteHex(capture_file, frame_return);
                const u64 next = ReadValue<u64>(frame, 0);
                if (next <= frame || next - frame > 0x100000) {
                    break;
                }
                frame = next;
            }
            capture_file << ']';
            capture_file << '}';
            if (site.offset == 0x00CC5390) {
                WriteQwords(capture_file, "matching_leave_object_400", object + 0x400, 12);
            }
            break;
        }
        case TraceKind::SummonRequest:
        case TraceKind::MultiplayerInsert:
            WriteObjectState(capture_file, registers->rdi);
            break;
        case TraceKind::SummonCandidate:
            WriteSummonCandidate(capture_file, site, *registers);
            break;
        case TraceKind::SummonSelection:
            WriteSummonSelection(capture_file, site, *registers);
            break;
        case TraceKind::SummonBuild:
            WriteSummonBuild(capture_file, site, *registers);
            break;
        case TraceKind::GetSosRequest:
            WriteGetSosState(capture_file, registers->rdi);
            break;
        case TraceKind::GetSosArea:
            capture_file << ",\"get_sos_area\":{";
            capture_file << "\"player\":";
            WriteHex(capture_file, registers->r13);
            capture_file << ",\"source\":";
            WriteHex(capture_file, registers->rdx);
            if (registers->rdx >= 0x10000) {
                capture_file << ",\"value\":"
                             << static_cast<s64>(ReadValue<s32>(registers->rdx, 0));
            }
            capture_file << '}';
            break;
        case TraceKind::GetSosInsert:
            capture_file << ",\"get_sos_insert\":{";
            capture_file << "\"event_code\":" << static_cast<s64>(static_cast<s32>(registers->rsi));
            capture_file << ",\"observed_sos_area\":"
                         << observed_sos_area.load(std::memory_order_relaxed);
            capture_file << '}';
            WriteGetSosState(capture_file, registers->rdi);
            break;
        case TraceKind::SosStatus:
            capture_file << ",\"sos_status\":{";
            capture_file << "\"object\":";
            WriteHex(capture_file, registers->rbx);
            capture_file << ",\"observed_sos_area\":"
                         << observed_sos_area.load(std::memory_order_relaxed);
            if (site.name == "SosStatus.NativeResult") {
                capture_file << ",\"primary_present\":" << ReadValue<s32>(registers->rbp - 0x9C, 0);
                capture_file << ",\"primary_value\":" << ReadValue<s32>(registers->rbp - 0x90, 0);
                capture_file << ",\"secondary_present\":"
                             << ReadValue<s32>(registers->rbp - 0x84, 0);
                capture_file << ",\"secondary_value\":" << ReadValue<s32>(registers->rbp - 0x78, 0);
                capture_file << ",\"event_bits\":"
                             << static_cast<u32>(ReadValue<u8>(registers->rbp - 0x6C, 0));
            }
            capture_file << '}';
            if (site.offset == SosStatusUpdateOffset) {
                capture_file << ",\"pre_match_guest_warp\":{";
                capture_file << "\"result\":\"" << pre_match_guest_warp.result << '"';
                capture_file << ",\"state\":";
                WriteHex(capture_file, pre_match_guest_warp.state);
                capture_file << ",\"current_map\":";
                WriteHex(capture_file, pre_match_guest_warp.current_map);
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, pre_match_guest_warp.target_map);
                capture_file << ",\"selected_map\":";
                WriteHex(capture_file, pre_match_guest_warp.selected_map);
                capture_file << ",\"multi_play_state\":" << pre_match_guest_warp.multi_play_state;
                capture_file << ",\"placement_written\":"
                             << (pre_match_guest_warp.placement_written ? "true" : "false");
                capture_file << ",\"select_result\":"
                             << (pre_match_guest_warp.select_result ? "true" : "false");
                capture_file << ",\"responder_resume_armed\":"
                             << (pre_match_guest_warp.responder_resume_armed ? "true" : "false");
                capture_file << ",\"transition_result\":" << pre_match_guest_warp.transition_result;
                capture_file << '}';
                capture_file << ",\"responder_resume\":{";
                capture_file << "\"result\":\"" << responder_resume.result << '"';
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, responder_resume.target_map);
                capture_file << ",\"current_map\":";
                WriteHex(capture_file, responder_resume.current_map);
                capture_file << ",\"target_area\":" << responder_resume.target_area;
                capture_file << ",\"current_area\":" << responder_resume.current_area;
                capture_file << ",\"state\":";
                WriteHex(capture_file, responder_resume.state);
                capture_file << ",\"multi_play\":";
                WriteHex(capture_file, responder_resume.multi_play);
                capture_file << ",\"multi_play_state\":" << responder_resume.multi_play_state;
                capture_file << ",\"matching_controller\":";
                WriteHex(capture_file, responder_resume.matching_controller);
                capture_file << ",\"player\":";
                WriteHex(capture_file, responder_resume.player);
                capture_file << ",\"ready_observations\":" << responder_resume.ready_observations;
                capture_file << ",\"attempts\":" << responder_resume.attempts;
                capture_file << ",\"effect_active\":"
                             << (responder_resume.effect_active ? "true" : "false");
                capture_file << ",\"native_result\":"
                             << (responder_resume.native_result ? "true" : "false") << '}';
                capture_file << ",\"deferred_summon_reload\":{";
                capture_file << "\"result\":\"" << deferred_summon_reload.result << '"';
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, deferred_summon_reload.target_map);
                capture_file << ",\"current_map\":";
                WriteHex(capture_file, deferred_summon_reload.current_map);
                capture_file << ",\"summon_reload_started\":"
                             << (deferred_summon_reload.summon_reload_started ? "true" : "false");
                capture_file << '}';
            }
            break;
        case TraceKind::SosMessenger:
            WriteMessengerState(capture_file, registers->rdi);
            break;
        case TraceKind::ActionFlags:
            WriteActionFlags(capture_file, registers->rdi, registers->rsp);
            break;
        case TraceKind::UseItem:
            WriteUseItemState(capture_file, site, *registers);
            break;
        case TraceKind::UseItemExecution:
            WriteUseItemExecution(capture_file, site, *registers);
            break;
        case TraceKind::GoodsActionSubmit:
            WriteGoodsActionSubmit(capture_file, *registers);
            break;
        case TraceKind::GoodsParamLookup:
            WriteGoodsParamLookup(capture_file, *registers);
            break;
        case TraceKind::BellAvailability:
            WriteBellAvailability(capture_file, site, *registers);
            break;
        case TraceKind::ResponderBellAvailability:
            WriteResponderBellAvailability(capture_file, site, *registers, responder_goods);
            break;
        case TraceKind::NetworkAreaRegion:
            capture_file << ",\"network_area_region\":{";
            capture_file << "\"object\":";
            WriteHex(capture_file, registers->r14);
            capture_file << ",\"sos_area\":" << observed_sos_area.load(std::memory_order_relaxed);
            capture_file << ",\"previous_region\":"
                         << static_cast<s64>(ReadValue<s32>(registers->r14, 0x44));
            capture_file << ",\"selected_region\":"
                         << static_cast<s64>(static_cast<s32>(registers->r15));
            capture_file << '}';
            break;
        case TraceKind::CrossMapGuestHandoff:
            capture_file << ",\"cross_map_guest_handoff\":{";
            capture_file << "\"result\":\"" << guest_placement_handoff.result << '"';
            capture_file << ",\"object\":";
            WriteHex(capture_file, guest_placement_handoff.object);
            capture_file << ",\"state\":";
            WriteHex(capture_file, guest_placement_handoff.state);
            capture_file << ",\"current_map\":";
            WriteHex(capture_file, guest_placement_handoff.current_map);
            capture_file << ",\"received_map\":";
            WriteHex(capture_file, guest_placement_handoff.received_map);
            capture_file << ",\"selected_map\":";
            WriteHex(capture_file, guest_placement_handoff.selected_map);
            capture_file << ",\"transported_host\":"
                         << (guest_placement_handoff.transported_host ? "true" : "false");
            capture_file << ",\"placement_refreshed\":"
                         << (guest_placement_handoff.placement_refreshed ? "true" : "false");
            capture_file << ",\"select_result\":"
                         << (guest_placement_handoff.select_result ? "true" : "false");
            capture_file << ",\"summon_reload_armed\":"
                         << (guest_placement_handoff.summon_reload_armed ? "true" : "false");
            capture_file << ",\"transition_result\":" << guest_placement_handoff.transition_result;
            capture_file << '}';
            WritePlacementState(capture_file, guest_placement_handoff.state);
            break;
        case TraceKind::SosPlacement:
            WriteQwords(capture_file, "position_qwords", registers->rax + 0x14A0, 2);
            WriteQwords(capture_file, "orientation_qwords", registers->rax + 0x14B0, 2);
            capture_file << ",\"map_id\":";
            WriteHex(capture_file, static_cast<u32>(registers->rcx));
            break;
        case TraceKind::GlobalState:
            WritePlacementState(capture_file, registers->rax);
            break;
        case TraceKind::ReceivedPlacementConditional:
            WriteFloatVector(capture_file, "received_position", registers->rdi);
            WriteFloatVector(capture_file, "received_orientation", registers->rsi);
            WriteU32(capture_file, "matching_key", registers->rdx);
            WriteMapId(capture_file, "received_map_id", registers->rcx);
            WritePlacementState(capture_file, registers->r8);
            break;
        case TraceKind::ReceivedPlacementSet:
            WriteFloatVector(capture_file, "received_position", registers->rdi);
            WriteFloatVector(capture_file, "received_orientation", registers->rsi);
            WriteU32(capture_file, "matching_key", registers->rdx);
            WriteMapId(capture_file, "received_map_id", registers->rcx);
            WritePlacementState(capture_file, registers->rax);
            break;
        case TraceKind::PlacementSelect:
            capture_file << (site.offset == 0x01332CD0 ? ",\"selected_map_id\":"
                                                       : ",\"default_map_id\":");
            WriteHex(capture_file, static_cast<u32>(registers->rax));
            WritePlacementState(capture_file, registers->rcx);
            break;
        case TraceKind::WarpCommand:
            WriteQwords(capture_file, "parameter_qwords", registers->rsi, 12);
            break;
        case TraceKind::CharacterWarp:
            WriteCharacterWarp(capture_file, site, *registers);
            break;
        case TraceKind::WarpResolved:
            WriteFloatVector(capture_file, "position", registers->rdi);
            WriteFloatVector(capture_file, "orientation", registers->rsi);
            WriteFloatVector(capture_file, "camera_orientation", registers->rdx);
            WriteMapId(capture_file, "map_id", registers->rcx);
            break;
        case TraceKind::WarpLocalApply:
            WriteMapId(capture_file, "map_id", registers->rsi);
            WriteFloatVector(capture_file, "position", registers->rdx);
            WriteFloatVector(capture_file, "orientation", registers->rcx);
            break;
        case TraceKind::StageWarp:
            WriteStageWarp(capture_file, site, *registers);
            break;
        case TraceKind::RespawnPointWarp:
            capture_file << ",\"respawn_point_warp\":{";
            capture_file << "\"warp_info_id\":" << static_cast<s32>(registers->rdi) << '}';
            WriteStageWarpState(capture_file, GetGlobalState());
            break;
        case TraceKind::StageWarpDescriptor:
            WriteStageWarpDescriptor(capture_file, *registers);
            if (site.offset == StageWarpDescriptorFinalizeOffset) {
                capture_file << ",\"summon_reload_state\":{";
                capture_file << "\"result\":\"" << summon_reload_state.result << '"';
                capture_file << ",\"target_map\":";
                WriteHex(capture_file, summon_reload_state.target_map);
                capture_file << ",\"descriptor_map\":";
                WriteHex(capture_file, summon_reload_state.descriptor_map);
                capture_file << ",\"reload_state\":";
                WriteHex(capture_file, summon_reload_state.reload_state);
                capture_file << ",\"state_before\":" << summon_reload_state.state_before;
                capture_file << ",\"state_after\":" << summon_reload_state.state_after;
                capture_file << ",\"summon_reload_started\":"
                             << (summon_reload_state.summon_reload_started ? "true" : "false");
                capture_file << ",\"state_setter_used\":"
                             << (summon_reload_state.state_setter_used ? "true" : "false");
                capture_file << ",\"placement_refreshed\":"
                             << (summon_reload_state.placement_refreshed ? "true" : "false") << '}';
            }
            break;
        case TraceKind::StageWarpAcknowledge:
            WriteStageWarpAcknowledge(capture_file, *registers);
            break;
        case TraceKind::StageWarpRespawnResolve:
            WriteStageWarpTask(capture_file, registers->r13, false);
            break;
        case TraceKind::StageWarpTransformResolve:
            WriteRespawnTransformResolve(capture_file, *registers);
            break;
        case TraceKind::StageWarpPlacement:
            WriteStageWarpTask(capture_file, registers->rdi, true);
            break;
        case TraceKind::SummonPoint:
            WriteQwords(capture_file, "query_qwords", registers->rsi, 8);
            break;
        case TraceKind::MapReload:
            WritePlacementState(capture_file, GetGlobalState());
            break;
        case TraceKind::HealingFountainRegistration:
            WriteHealingFountainRegistration(capture_file, *registers);
            break;
        case TraceKind::HealingFountainAvailability:
            WriteHealingFountainAvailability(capture_file, *registers);
            break;
        case TraceKind::ChairRespawnNotification:
            WriteChairRespawnNotification(capture_file, *registers);
            break;
        case TraceKind::WorldStateValidation:
            WriteWorldStateValidation(capture_file, site, *registers);
            break;
        }
        capture_file << "}\n";
        capture_file.flush();

        if (hit <= 8 || hit % 300 == 0) {
            LOG_INFO(Debug,
                     "Bloodborne RE entry {} hit={} rdi={:#x} rsi={:#x} rdx={:#x} capture={}",
                     site.name, hit, registers->rdi, registers->rsi, registers->rdx,
                     Common::FS::PathToUTF8String(capture_path));
        }
    } catch (...) {
        // An observer must never unwind into guest code.
    }
}

bool VerifySites() {
    const auto image_size = MemoryPatcher::g_eboot_image_size;
    for (const auto& site : Sites) {
        if ((summon_build_host_placement_hook_installed && site.offset == SummonBuildEntryOffset) ||
            (cross_map_guest_handoff_hook_installed && site.offset == CrossMapGuestHandoffOffset) ||
            (summon_reload_state_hook_installed &&
             site.offset == StageWarpDescriptorFinalizeOffset) ||
            (deferred_summon_reload_hook_installed && site.offset == SosStatusUpdateOffset) ||
            (healing_fountain_host_availability_hook_installed &&
             site.offset == HealingFountainAvailabilityOffset) ||
            (world_teardown_hook_installed && site.offset == WorldTeardownMatchingOffset) ||
            (ghost_death_revive_hook_installed && site.offset == GhostDeathReviveOffset) ||
            (host_death_respawn_hook_installed && site.offset == SoloPlayDeathRespawnOffset) ||
            (seamless_travel_hooks_installed && site.offset == StageTransitionEntryOffset) ||
            (stop_request_hook_installed && site.offset == MultiPlayStopRequestOffset)) {
            continue;
        }
        const auto expected = std::span<const u8>{site.prologue.data(), site.prologue_size};
        if (site.offset > image_size || expected.size() > image_size - site.offset) {
            LOG_ERROR(Debug, "Bloodborne RE site {} lies outside the eboot image", site.name);
            return false;
        }
        const auto actual = std::span<const u8>{
            reinterpret_cast<const u8*>(image_base + site.offset), expected.size()};
        if (!std::ranges::equal(actual, expected)) {
            LOG_ERROR(Debug,
                      "Bloodborne RE signature mismatch at {} ({:#x}): expected={} actual={}",
                      site.name, site.offset, BytesToHex(expected), BytesToHex(actual));
            return false;
        }
    }
    return true;
}

} // namespace

std::optional<std::string> GetSeamlessHostPlacementHeader() {
    std::optional<SummonPlacementDescriptor> placement;
    {
        std::scoped_lock lock{seamless_placement_mutex};
        placement = seamless_host_placement;
    }
    if (!placement.has_value()) {
        return std::nullopt;
    }

    std::ostringstream out;
    out << "1," << std::hex << std::setfill('0') << std::setw(8) << placement->packed_region << ','
        << std::setw(8) << std::bit_cast<u32>(placement->x) << ',' << std::setw(8)
        << std::bit_cast<u32>(placement->y) << ',' << std::setw(8)
        << std::bit_cast<u32>(placement->z) << ',' << std::setw(8)
        << std::bit_cast<u32>(placement->heading) << ',' << std::dec << placement->area;
    return out.str();
}

bool SetSeamlessHostPlacementHeader(std::string_view value) {
    std::array<std::string_view, 7> fields{};
    size_t begin = 0;
    for (size_t field_index = 0; field_index < fields.size(); ++field_index) {
        const size_t end = value.find(',', begin);
        const bool final_field = field_index + 1 == fields.size();
        if ((final_field && end != std::string_view::npos) ||
            (!final_field && end == std::string_view::npos)) {
            return false;
        }
        fields[field_index] =
            value.substr(begin, end == std::string_view::npos ? value.size() - begin : end - begin);
        begin = end == std::string_view::npos ? value.size() : end + 1;
    }
    if (fields[0] != "1") {
        return false;
    }

    auto parse_u32 = [](std::string_view field, int base, u32& output) {
        const auto [end, error] =
            std::from_chars(field.data(), field.data() + field.size(), output, base);
        return error == std::errc{} && end == field.data() + field.size();
    };
    SummonPlacementDescriptor placement{};
    std::array<u32, 4> float_bits{};
    if (!parse_u32(fields[1], 16, placement.packed_region) ||
        !parse_u32(fields[2], 16, float_bits[0]) || !parse_u32(fields[3], 16, float_bits[1]) ||
        !parse_u32(fields[4], 16, float_bits[2]) || !parse_u32(fields[5], 16, float_bits[3])) {
        return false;
    }
    const auto [area_end, area_error] =
        std::from_chars(fields[6].data(), fields[6].data() + fields[6].size(), placement.area, 10);
    if (area_error != std::errc{} || area_end != fields[6].data() + fields[6].size()) {
        return false;
    }
    placement.x = std::bit_cast<float>(float_bits[0]);
    placement.y = std::bit_cast<float>(float_bits[1]);
    placement.z = std::bit_cast<float>(float_bits[2]);
    placement.heading = std::bit_cast<float>(float_bits[3]);
    if (!IsUsablePackedMap(placement.packed_region) || !std::isfinite(placement.x) ||
        !std::isfinite(placement.y) || !std::isfinite(placement.z) ||
        !std::isfinite(placement.heading)) {
        return false;
    }

    std::scoped_lock lock{seamless_placement_mutex};
    if (!seamless_received_host_placement.has_value() ||
        !IsSameSummonPlacement(*seamless_received_host_placement, placement)) {
        seamless_received_host_placement_consumed = false;
    }
    seamless_received_host_placement = placement;
    return true;
}

void ClearSeamlessHostPlacementHeader() {
    std::scoped_lock lock{seamless_placement_mutex};
    seamless_received_host_placement.reset();
    seamless_received_host_placement_consumed = false;
}

void TraceMatching2LeaveRoom(std::uintptr_t return_address, std::uint64_t room_id) {
    if (image_base == 0 || MemoryPatcher::g_game_serial != "CUSA03173") {
        return;
    }

    const u64 caller_offset = return_address >= image_base && return_address - image_base <
                                                                  MemoryPatcher::g_eboot_image_size
                                  ? return_address - image_base
                                  : 0;
    const u64 reload_state = ReadValue<u64>(image_base + SummonSessionRulesPointerOffset, 0);
    const s32 reload_phase = reload_state >= 0x10000 ? ReadValue<s32>(reload_state, 0x84) : -1;
    const u64 global_state = GetGlobalState();
    const u32 current_map = GetCurrentPackedMap();
    const u32 requested_map = global_state >= 0x10000 ? ReadValue<u32>(global_state, 0x0C) : 0;
    const u32 pending_reload = pending_summon_reload_map.load(std::memory_order_acquire);
    const u32 pending_state = pending_summon_reload_state_map.load(std::memory_order_acquire);
    const u64 sequence = event_sequence.fetch_add(1, std::memory_order_relaxed) + 1;
    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();

    try {
        std::scoped_lock lock{capture_mutex};
        if (capture_file) {
            capture_file << "{\"type\":\"matching2_leave_room\",\"seq\":" << sequence
                         << ",\"time_ms\":" << timestamp << ",\"return_address\":";
            WriteHex(capture_file, return_address);
            capture_file << ",\"caller_offset\":";
            WriteHex(capture_file, caller_offset);
            capture_file << ",\"room_id\":" << room_id << ",\"reload_state\":";
            WriteHex(capture_file, reload_state);
            capture_file << ",\"reload_phase\":" << reload_phase << ",\"global_state\":";
            WriteHex(capture_file, global_state);
            capture_file << ",\"current_map\":";
            WriteHex(capture_file, current_map);
            capture_file << ",\"requested_map\":";
            WriteHex(capture_file, requested_map);
            capture_file << ",\"pending_reload_map\":";
            WriteHex(capture_file, pending_reload);
            capture_file << ",\"pending_state_map\":";
            WriteHex(capture_file, pending_state);
            capture_file << "}\n";
            capture_file.flush();
        }
    } catch (...) {
        // Diagnostics must never unwind into an NP library call.
    }

    LOG_INFO(Debug,
             "Bloodborne RE Matching2 leave room={} caller={:#x} offset={:#x} reload_phase={} "
             "current_map={:#x} requested_map={:#x}",
             room_id, return_address, caller_offset, reload_phase, current_map, requested_map);
}

void InstallSeamlessCoopPatches() {
    if ((negative_area_patch_installed && area_flag_patch_installed &&
         responder_bell_area_patch_installed && responder_bell_common_patch_installed &&
         active_bell_negative_area_patch_installed && active_bell_area_flag_patch_installed &&
         responder_search_negative_area_patch_installed && sos_status_area_patch_installed &&
         summon_candidate_area_patch_installed && summon_build_area_patch_installed &&
         summon_build_world_state_patch_installed && summon_build_negative_event_patch_installed &&
         summon_build_host_placement_hook_installed && cross_map_guest_handoff_hook_installed &&
         summon_reload_state_hook_installed && deferred_summon_reload_hook_installed &&
         healing_fountain_host_availability_hook_installed) ||
        !EnvFlagEnabled("SHADPS4_BLOODBORNE_SEAMLESS_COOP")) {
        return;
    }
    if (MemoryPatcher::g_game_serial != "CUSA03173") {
        return;
    }

    const auto* param_sfo = Common::Singleton<PSF>::Instance();
    const std::string_view app_version = param_sfo->GetString("APP_VER").value_or("Unknown");
    if (app_version != "01.09") {
        LOG_ERROR(Debug,
                  "Bloodborne seamless patches require CUSA03173 01.09; loaded version is {}",
                  app_version);
        return;
    }

    const auto base = MemoryPatcher::g_eboot_address;
    const auto image_size = MemoryPatcher::g_eboot_image_size;
    const auto summon_build_entry = std::ranges::find_if(
        Sites, [](const TraceSite& site) { return site.offset == SummonBuildEntryOffset; });
    const auto cross_map_guest_handoff = std::ranges::find_if(
        Sites, [](const TraceSite& site) { return site.offset == CrossMapGuestHandoffOffset; });
    const auto deferred_summon_reload = std::ranges::find_if(
        Sites, [](const TraceSite& site) { return site.offset == SosStatusUpdateOffset; });
    const auto summon_reload_state = std::ranges::find_if(Sites, [](const TraceSite& site) {
        return site.offset == StageWarpDescriptorFinalizeOffset;
    });
    const auto healing_fountain_host_availability = std::ranges::find_if(
        Sites, [](const auto& site) { return site.offset == HealingFountainAvailabilityOffset; });
    const auto world_teardown = std::ranges::find_if(
        Sites, [](const auto& site) { return site.offset == WorldTeardownMatchingOffset; });
    if (summon_build_entry == Sites.end() || cross_map_guest_handoff == Sites.end() ||
        deferred_summon_reload == Sites.end() || summon_reload_state == Sites.end() ||
        healing_fountain_host_availability == Sites.end() || world_teardown == Sites.end()) {
        LOG_ERROR(Debug, "Bloodborne seamless functional hook site is missing");
        return;
    }
    const auto summon_build_entry_expected =
        std::span<const u8>{summon_build_entry->prologue.data(), summon_build_entry->prologue_size};
    const auto cross_map_guest_handoff_expected = std::span<const u8>{
        cross_map_guest_handoff->prologue.data(), cross_map_guest_handoff->prologue_size};
    const auto deferred_summon_reload_expected = std::span<const u8>{
        deferred_summon_reload->prologue.data(), deferred_summon_reload->prologue_size};
    const auto summon_reload_state_expected = std::span<const u8>{
        summon_reload_state->prologue.data(), summon_reload_state->prologue_size};
    const auto healing_fountain_host_availability_expected =
        std::span<const u8>{healing_fountain_host_availability->prologue.data(),
                            healing_fountain_host_availability->prologue_size};
    if (base == 0 || BeckoningAreaComparisonOffset > image_size ||
        BeckoningAreaComparison.size() > image_size - BeckoningAreaComparisonOffset ||
        BeckoningAreaFlagResultOffset > image_size ||
        BeckoningAreaFlagResult.size() > image_size - BeckoningAreaFlagResultOffset ||
        ResponderBellAreaResultOffset > image_size ||
        ResponderBellAreaResult.size() > image_size - ResponderBellAreaResultOffset ||
        ResponderBellCommonResultOffset > image_size ||
        ResponderBellCommonResult.size() > image_size - ResponderBellCommonResultOffset ||
        ActiveBellAreaComparisonOffset > image_size ||
        ActiveBellAreaComparison.size() > image_size - ActiveBellAreaComparisonOffset ||
        ActiveBellAreaFlagResultOffset > image_size ||
        ActiveBellAreaFlagResult.size() > image_size - ActiveBellAreaFlagResultOffset ||
        ResponderSearchAreaRangeResultOffset > image_size ||
        ResponderSearchAreaRangeResult.size() > image_size - ResponderSearchAreaRangeResultOffset ||
        SosStatusAreaRestrictionOffset > image_size ||
        SosStatusAreaRestriction.size() > image_size - SosStatusAreaRestrictionOffset ||
        SummonCandidateAreaRestrictionOffset > image_size ||
        SummonCandidateAreaRestriction.size() > image_size - SummonCandidateAreaRestrictionOffset ||
        SummonBuildAreaRestrictionOffset > image_size ||
        SummonBuildAreaRestriction.size() > image_size - SummonBuildAreaRestrictionOffset ||
        SummonBuildWorldStateRestrictionOffset > image_size ||
        SummonBuildWorldStateRestriction.size() >
            image_size - SummonBuildWorldStateRestrictionOffset ||
        SummonBuildNegativeEventRestrictionOffset > image_size ||
        SummonBuildNegativeEventRestriction.size() >
            image_size - SummonBuildNegativeEventRestrictionOffset ||
        StageTransitionStopMatchingCallOffset > image_size ||
        StageTransitionStopMatchingCall.size() >
            image_size - StageTransitionStopMatchingCallOffset ||
        SummonBuildEntryOffset > image_size ||
        summon_build_entry_expected.size() > image_size - SummonBuildEntryOffset ||
        CrossMapGuestHandoffOffset > image_size ||
        cross_map_guest_handoff_expected.size() > image_size - CrossMapGuestHandoffOffset ||
        SosStatusUpdateOffset > image_size ||
        deferred_summon_reload_expected.size() > image_size - SosStatusUpdateOffset ||
        StageWarpDescriptorFinalizeOffset > image_size ||
        summon_reload_state_expected.size() > image_size - StageWarpDescriptorFinalizeOffset ||
        HealingFountainAvailabilityOffset > image_size ||
        healing_fountain_host_availability_expected.size() >
            image_size - HealingFountainAvailabilityOffset ||
        std::ranges::any_of(CrossMapNativeCalls, [image_size](const auto& site) {
            return site.offset > image_size || site.prologue_size > image_size - site.offset;
        })) {
        LOG_ERROR(Debug, "Bloodborne seamless patches lie outside the eboot image");
        return;
    }

    auto* area_comparison = reinterpret_cast<u8*>(base + BeckoningAreaComparisonOffset);
    const auto actual_area_comparison =
        std::span<const u8>{area_comparison, BeckoningAreaComparison.size()};
    if (!std::ranges::equal(actual_area_comparison, BeckoningAreaComparison)) {
        LOG_ERROR(Debug,
                  "Bloodborne Beckoning area signature mismatch at {:#x}: expected={} actual={}",
                  BeckoningAreaComparisonOffset, BytesToHex(BeckoningAreaComparison),
                  BytesToHex(actual_area_comparison));
        return;
    }

    auto* area_flag_result = reinterpret_cast<u8*>(base + BeckoningAreaFlagResultOffset);
    const auto actual_area_flag_result =
        std::span<const u8>{area_flag_result, BeckoningAreaFlagResult.size()};
    if (!std::ranges::equal(actual_area_flag_result, BeckoningAreaFlagResult)) {
        LOG_ERROR(Debug,
                  "Bloodborne Beckoning area-flag signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  BeckoningAreaFlagResultOffset, BytesToHex(BeckoningAreaFlagResult),
                  BytesToHex(actual_area_flag_result));
        return;
    }

    auto* responder_bell_area_result = reinterpret_cast<u8*>(base + ResponderBellAreaResultOffset);
    const auto actual_responder_bell_area_result =
        std::span<const u8>{responder_bell_area_result, ResponderBellAreaResult.size()};
    if (!std::ranges::equal(actual_responder_bell_area_result, ResponderBellAreaResult)) {
        LOG_ERROR(Debug,
                  "Bloodborne responder Bell area signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  ResponderBellAreaResultOffset, BytesToHex(ResponderBellAreaResult),
                  BytesToHex(actual_responder_bell_area_result));
        return;
    }

    auto* responder_bell_common_result =
        reinterpret_cast<u8*>(base + ResponderBellCommonResultOffset);
    const auto actual_responder_bell_common_result =
        std::span<const u8>{responder_bell_common_result, ResponderBellCommonResult.size()};
    if (!std::ranges::equal(actual_responder_bell_common_result, ResponderBellCommonResult)) {
        LOG_ERROR(Debug,
                  "Bloodborne responder Bell common signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  ResponderBellCommonResultOffset, BytesToHex(ResponderBellCommonResult),
                  BytesToHex(actual_responder_bell_common_result));
        return;
    }

    auto* active_bell_area_comparison =
        reinterpret_cast<u8*>(base + ActiveBellAreaComparisonOffset);
    const auto actual_active_bell_area_comparison =
        std::span<const u8>{active_bell_area_comparison, ActiveBellAreaComparison.size()};
    if (!std::ranges::equal(actual_active_bell_area_comparison, ActiveBellAreaComparison)) {
        LOG_ERROR(Debug,
                  "Bloodborne active Bell area signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  ActiveBellAreaComparisonOffset, BytesToHex(ActiveBellAreaComparison),
                  BytesToHex(actual_active_bell_area_comparison));
        return;
    }

    auto* active_bell_area_flag_result =
        reinterpret_cast<u8*>(base + ActiveBellAreaFlagResultOffset);
    const auto actual_active_bell_area_flag_result =
        std::span<const u8>{active_bell_area_flag_result, ActiveBellAreaFlagResult.size()};
    if (!std::ranges::equal(actual_active_bell_area_flag_result, ActiveBellAreaFlagResult)) {
        LOG_ERROR(Debug,
                  "Bloodborne active Bell area-flag signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  ActiveBellAreaFlagResultOffset, BytesToHex(ActiveBellAreaFlagResult),
                  BytesToHex(actual_active_bell_area_flag_result));
        return;
    }

    auto* responder_search_area_range_result =
        reinterpret_cast<u8*>(base + ResponderSearchAreaRangeResultOffset);
    const auto actual_responder_search_area_range_result = std::span<const u8>{
        responder_search_area_range_result, ResponderSearchAreaRangeResult.size()};
    if (!std::ranges::equal(actual_responder_search_area_range_result,
                            ResponderSearchAreaRangeResult)) {
        LOG_ERROR(Debug,
                  "Bloodborne responder-search area-range signature mismatch at {:#x}: "
                  "expected={} actual={}",
                  ResponderSearchAreaRangeResultOffset, BytesToHex(ResponderSearchAreaRangeResult),
                  BytesToHex(actual_responder_search_area_range_result));
        return;
    }

    auto* sos_status_area_restriction =
        reinterpret_cast<u8*>(base + SosStatusAreaRestrictionOffset);
    const auto actual_sos_status_area_restriction =
        std::span<const u8>{sos_status_area_restriction, SosStatusAreaRestriction.size()};
    if (!std::ranges::equal(actual_sos_status_area_restriction, SosStatusAreaRestriction)) {
        LOG_ERROR(Debug,
                  "Bloodborne SOS-status area signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  SosStatusAreaRestrictionOffset, BytesToHex(SosStatusAreaRestriction),
                  BytesToHex(actual_sos_status_area_restriction));
        return;
    }

    auto* summon_candidate_area_restriction =
        reinterpret_cast<u8*>(base + SummonCandidateAreaRestrictionOffset);
    const auto actual_summon_candidate_area_restriction = std::span<const u8>{
        summon_candidate_area_restriction, SummonCandidateAreaRestriction.size()};
    if (!std::ranges::equal(actual_summon_candidate_area_restriction,
                            SummonCandidateAreaRestriction)) {
        LOG_ERROR(Debug,
                  "Bloodborne summon-candidate area signature mismatch at {:#x}: "
                  "expected={} actual={}",
                  SummonCandidateAreaRestrictionOffset, BytesToHex(SummonCandidateAreaRestriction),
                  BytesToHex(actual_summon_candidate_area_restriction));
        return;
    }

    auto* summon_build_area_restriction =
        reinterpret_cast<u8*>(base + SummonBuildAreaRestrictionOffset);
    const auto actual_summon_build_area_restriction =
        std::span<const u8>{summon_build_area_restriction, SummonBuildAreaRestriction.size()};
    if (!std::ranges::equal(actual_summon_build_area_restriction, SummonBuildAreaRestriction)) {
        LOG_ERROR(Debug,
                  "Bloodborne summon-builder area signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  SummonBuildAreaRestrictionOffset, BytesToHex(SummonBuildAreaRestriction),
                  BytesToHex(actual_summon_build_area_restriction));
        return;
    }

    auto* summon_build_world_state_restriction =
        reinterpret_cast<u8*>(base + SummonBuildWorldStateRestrictionOffset);
    const auto actual_summon_build_world_state_restriction = std::span<const u8>{
        summon_build_world_state_restriction, SummonBuildWorldStateRestriction.size()};
    if (!std::ranges::equal(actual_summon_build_world_state_restriction,
                            SummonBuildWorldStateRestriction)) {
        LOG_ERROR(Debug,
                  "Bloodborne summon-builder world-state signature mismatch at {:#x}: "
                  "expected={} actual={}",
                  SummonBuildWorldStateRestrictionOffset,
                  BytesToHex(SummonBuildWorldStateRestriction),
                  BytesToHex(actual_summon_build_world_state_restriction));
        return;
    }

    auto* summon_build_negative_event_restriction =
        reinterpret_cast<u8*>(base + SummonBuildNegativeEventRestrictionOffset);
    const auto actual_summon_build_negative_event_restriction = std::span<const u8>{
        summon_build_negative_event_restriction, SummonBuildNegativeEventRestriction.size()};
    if (!std::ranges::equal(actual_summon_build_negative_event_restriction,
                            SummonBuildNegativeEventRestriction)) {
        LOG_ERROR(Debug,
                  "Bloodborne summon-builder negative-event signature mismatch at {:#x}: "
                  "expected={} actual={}",
                  SummonBuildNegativeEventRestrictionOffset,
                  BytesToHex(SummonBuildNegativeEventRestriction),
                  BytesToHex(actual_summon_build_negative_event_restriction));
        return;
    }

    auto* stage_transition_stop_matching_call =
        reinterpret_cast<u8*>(base + StageTransitionStopMatchingCallOffset);
    const auto actual_stage_transition_stop_matching_call = std::span<const u8>{
        stage_transition_stop_matching_call, StageTransitionStopMatchingCall.size()};
    if (!std::ranges::equal(actual_stage_transition_stop_matching_call,
                            StageTransitionStopMatchingCall)) {
        LOG_ERROR(Debug,
                  "Bloodborne stage-transition matching-stop signature mismatch at {:#x}: "
                  "expected={} actual={}",
                  StageTransitionStopMatchingCallOffset,
                  BytesToHex(StageTransitionStopMatchingCall),
                  BytesToHex(actual_stage_transition_stop_matching_call));
        return;
    }

    const auto actual_summon_build_entry =
        std::span<const u8>{reinterpret_cast<const u8*>(base + SummonBuildEntryOffset),
                            summon_build_entry_expected.size()};
    if (!std::ranges::equal(actual_summon_build_entry, summon_build_entry_expected)) {
        LOG_ERROR(Debug,
                  "Bloodborne SummonBuild entry signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  SummonBuildEntryOffset, BytesToHex(summon_build_entry_expected),
                  BytesToHex(actual_summon_build_entry));
        return;
    }
    const auto actual_cross_map_guest_handoff =
        std::span<const u8>{reinterpret_cast<const u8*>(base + CrossMapGuestHandoffOffset),
                            cross_map_guest_handoff_expected.size()};
    if (!std::ranges::equal(actual_cross_map_guest_handoff, cross_map_guest_handoff_expected)) {
        LOG_ERROR(Debug,
                  "Bloodborne cross-map guest-handoff signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  CrossMapGuestHandoffOffset, BytesToHex(cross_map_guest_handoff_expected),
                  BytesToHex(actual_cross_map_guest_handoff));
        return;
    }
    const auto actual_deferred_summon_reload =
        std::span<const u8>{reinterpret_cast<const u8*>(base + SosStatusUpdateOffset),
                            deferred_summon_reload_expected.size()};
    if (!std::ranges::equal(actual_deferred_summon_reload, deferred_summon_reload_expected)) {
        LOG_ERROR(Debug,
                  "Bloodborne deferred summon-reload signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  SosStatusUpdateOffset, BytesToHex(deferred_summon_reload_expected),
                  BytesToHex(actual_deferred_summon_reload));
        return;
    }
    const auto actual_summon_reload_state =
        std::span<const u8>{reinterpret_cast<const u8*>(base + StageWarpDescriptorFinalizeOffset),
                            summon_reload_state_expected.size()};
    if (!std::ranges::equal(actual_summon_reload_state, summon_reload_state_expected)) {
        LOG_ERROR(Debug,
                  "Bloodborne summon-reload state signature mismatch at {:#x}: expected={} "
                  "actual={}",
                  StageWarpDescriptorFinalizeOffset, BytesToHex(summon_reload_state_expected),
                  BytesToHex(actual_summon_reload_state));
        return;
    }
    const auto actual_healing_fountain_host_availability =
        std::span<const u8>{reinterpret_cast<const u8*>(base + HealingFountainAvailabilityOffset),
                            healing_fountain_host_availability_expected.size()};
    if (!std::ranges::equal(actual_healing_fountain_host_availability,
                            healing_fountain_host_availability_expected)) {
        LOG_ERROR(Debug,
                  "Bloodborne healing-fountain host-availability signature mismatch at {:#x}: "
                  "expected={} actual={}",
                  HealingFountainAvailabilityOffset,
                  BytesToHex(healing_fountain_host_availability_expected),
                  BytesToHex(actual_healing_fountain_host_availability));
        return;
    }
    for (const auto& site : CrossMapNativeCalls) {
        const auto expected = std::span<const u8>{site.prologue.data(), site.prologue_size};
        const auto actual =
            std::span<const u8>{reinterpret_cast<const u8*>(base + site.offset), expected.size()};
        if (!std::ranges::equal(actual, expected)) {
            LOG_ERROR(Debug,
                      "Bloodborne cross-map native call {} signature mismatch at {:#x}: "
                      "expected={} actual={}",
                      site.name, site.offset, BytesToHex(expected), BytesToHex(actual));
            return;
        }
    }
    image_base = base;
    const size_t summon_build_entry_index =
        static_cast<size_t>(std::distance(Sites.begin(), summon_build_entry));
    if (!InstallGuestCodeHook(reinterpret_cast<void*>(base + SummonBuildEntryOffset),
                              summon_build_entry_expected, summon_build_entry_index, TraceEntry)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne cross-map host-placement hook at {:#x}",
                  SummonBuildEntryOffset);
        return;
    }
    summon_build_host_placement_hook_installed = true;
    const size_t cross_map_guest_handoff_index =
        static_cast<size_t>(std::distance(Sites.begin(), cross_map_guest_handoff));
    if (!InstallGuestCodeHook(reinterpret_cast<void*>(base + CrossMapGuestHandoffOffset),
                              cross_map_guest_handoff_expected, cross_map_guest_handoff_index,
                              TraceEntry)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne cross-map guest-handoff hook at {:#x}",
                  CrossMapGuestHandoffOffset);
        return;
    }
    cross_map_guest_handoff_hook_installed = true;
    const size_t summon_reload_state_index =
        static_cast<size_t>(std::distance(Sites.begin(), summon_reload_state));
    if (!InstallGuestCodeHook(reinterpret_cast<void*>(base + StageWarpDescriptorFinalizeOffset),
                              summon_reload_state_expected, summon_reload_state_index,
                              TraceEntry)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne summon-reload state hook at {:#x}",
                  StageWarpDescriptorFinalizeOffset);
        return;
    }
    summon_reload_state_hook_installed = true;
    const size_t deferred_summon_reload_index =
        static_cast<size_t>(std::distance(Sites.begin(), deferred_summon_reload));
    if (!InstallGuestCodeHook(reinterpret_cast<void*>(base + SosStatusUpdateOffset),
                              deferred_summon_reload_expected, deferred_summon_reload_index,
                              TraceEntry)) {
        LOG_ERROR(Debug, "Failed to install Bloodborne deferred summon-reload hook at {:#x}",
                  SosStatusUpdateOffset);
        return;
    }
    deferred_summon_reload_hook_installed = true;
    InitPartyController();
    const size_t healing_fountain_host_availability_index =
        static_cast<size_t>(std::distance(Sites.begin(), healing_fountain_host_availability));
    if (!InstallGuestCodeHook(reinterpret_cast<void*>(base + HealingFountainAvailabilityOffset),
                              healing_fountain_host_availability_expected,
                              healing_fountain_host_availability_index, TraceEntry)) {
        LOG_ERROR(Debug,
                  "Failed to install Bloodborne healing-fountain host-availability hook at "
                  "{:#x}",
                  HealingFountainAvailabilityOffset);
        return;
    }
    healing_fountain_host_availability_hook_installed = true;
    // Optional: without it a guest quitting to the title stays in the room, but every other
    // patch still applies.
    const auto world_teardown_expected =
        std::span<const u8>{world_teardown->prologue.data(), world_teardown->prologue_size};
    world_teardown_hook_installed = InstallGuestCodeHook(
        reinterpret_cast<void*>(base + WorldTeardownMatchingOffset), world_teardown_expected,
        static_cast<size_t>(std::distance(Sites.begin(), world_teardown)), TraceEntry);
    if (!world_teardown_hook_installed) {
        LOG_ERROR(Debug, "Failed to install Bloodborne world-teardown hook at {:#x}",
                  WorldTeardownMatchingOffset);
    }

    InstallGhostDeathRevive(base, image_size);
    InstallHostDeathKeepsGuests(base, image_size);
    InstallBossClearKeepsSession(base, image_size);
    InstallGuestStaysWhenTheHostLeaves(base, image_size);
    InstallSeamlessTravel(base, image_size);
    InstallLampsAsCampfires(base, image_size);
    InstallNoLevelSyncInParty(base, image_size);
    InstallSolidNpcsInParty(base, image_size);
    InstallSharedLoot(base, image_size);
    InstallItemModuleDiagnostics(base, image_size);
    InstallItemButtonDiagnostics(base);
    StartRestBoardClient();
    PatchSignTimers(base, image_size);

    std::memcpy(area_comparison, BeckoningSignedAreaComparison.data(),
                BeckoningSignedAreaComparison.size());
    std::memcpy(area_flag_result, BeckoningAllowedAreaFlagResult.data(),
                BeckoningAllowedAreaFlagResult.size());
    std::memcpy(responder_bell_area_result, ResponderBellAllowedAreaResult.data(),
                ResponderBellAllowedAreaResult.size());
    std::memcpy(responder_bell_common_result, ResponderBellAllowedCommonResult.data(),
                ResponderBellAllowedCommonResult.size());
    std::memcpy(active_bell_area_comparison, ActiveBellSignedAreaComparison.data(),
                ActiveBellSignedAreaComparison.size());
    std::memcpy(active_bell_area_flag_result, ActiveBellAllowedAreaFlagResult.data(),
                ActiveBellAllowedAreaFlagResult.size());
    std::memcpy(responder_search_area_range_result, ResponderSearchSignedAreaRangeResult.data(),
                ResponderSearchSignedAreaRangeResult.size());
    std::memcpy(sos_status_area_restriction, SosStatusAllowedArea.data(),
                SosStatusAllowedArea.size());
    std::memcpy(summon_candidate_area_restriction, SummonCandidateAllowedCrossMap.data(),
                SummonCandidateAllowedCrossMap.size());
    std::memcpy(summon_build_area_restriction, SummonBuildAllowedCrossMap.data(),
                SummonBuildAllowedCrossMap.size());
    std::memcpy(summon_build_world_state_restriction, SummonBuildAllowedWorldState.data(),
                SummonBuildAllowedWorldState.size());
    std::memcpy(summon_build_negative_event_restriction, SummonBuildAllowedNegativeEvent.data(),
                SummonBuildAllowedNegativeEvent.size());
    std::memcpy(stage_transition_stop_matching_call, StageTransitionKeepMatchingCall.data(),
                StageTransitionKeepMatchingCall.size());
    negative_area_patch_installed = true;
    area_flag_patch_installed = true;
    responder_bell_area_patch_installed = true;
    responder_bell_common_patch_installed = true;
    active_bell_negative_area_patch_installed = true;
    active_bell_area_flag_patch_installed = true;
    responder_search_negative_area_patch_installed = true;
    sos_status_area_patch_installed = true;
    summon_candidate_area_patch_installed = true;
    summon_build_area_patch_installed = true;
    summon_build_world_state_patch_installed = true;
    summon_build_negative_event_patch_installed = true;
    stage_transition_keep_matching_patch_installed = true;
    LOG_INFO(Debug,
             "Bloodborne seamless patches enabled Beckoning, Small Resonant, and Sinister Bell "
             "use, active search, cross-map candidate selection, and native summon construction "
             "with host-placement transport, guest stage handoff, retained matching during "
             "stage transitions, deferred native reload, and host lantern actions in negative "
             "and area-complete SOS regions at "
             "{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/{:#x}/"
             "{:#x}/{:#x}/{:#x}/{:#x}/{:#x}",
             BeckoningAreaComparisonOffset, BeckoningAreaFlagResultOffset,
             ResponderBellAreaResultOffset, ResponderBellCommonResultOffset,
             ActiveBellAreaComparisonOffset, ActiveBellAreaFlagResultOffset,
             ResponderSearchAreaRangeResultOffset, SosStatusAreaRestrictionOffset,
             SummonCandidateAreaRestrictionOffset, SummonBuildAreaRestrictionOffset,
             SummonBuildWorldStateRestrictionOffset, SummonBuildNegativeEventRestrictionOffset,
             StageTransitionStopMatchingCallOffset, SummonBuildEntryOffset,
             CrossMapGuestHandoffOffset, SosStatusUpdateOffset, SetSummonReloadStateOffset,
             HealingFountainAvailabilityOffset);
}

void InstallReverseEngineeringTrace() {
    if (installed || !EnvFlagEnabled("SHADPS4_BLOODBORNE_RE_TRACE")) {
        return;
    }
    if (MemoryPatcher::g_game_serial != "CUSA03173") {
        LOG_WARNING(Debug, "Bloodborne RE trace ignored for title {}",
                    MemoryPatcher::g_game_serial);
        return;
    }

    const auto* param_sfo = Common::Singleton<PSF>::Instance();
    const std::string_view app_version = param_sfo->GetString("APP_VER").value_or("Unknown");
    if (app_version != "01.09") {
        LOG_ERROR(Debug, "Bloodborne RE trace requires CUSA03173 01.09; loaded version is {}",
                  app_version);
        return;
    }

    image_base = MemoryPatcher::g_eboot_address;
    if (image_base == 0 || !VerifySites()) {
        LOG_ERROR(Debug, "Bloodborne RE trace was not installed");
        return;
    }

    const auto capture_dir =
        Common::FS::GetUserPath(Common::FS::PathType::CapturesDir) / "bloodborne-re";
    std::error_code error;
    std::filesystem::create_directories(capture_dir, error);
    if (error) {
        LOG_ERROR(Debug, "Could not create Bloodborne RE capture directory {}: {}",
                  Common::FS::PathToUTF8String(capture_dir), error.message());
        return;
    }

    const auto timestamp = std::chrono::duration_cast<std::chrono::milliseconds>(
                               std::chrono::system_clock::now().time_since_epoch())
                               .count();
    const int process_id = Debugger::GetCurrentPid();
    capture_path = capture_dir / ("cusa03173-109-p" + std::to_string(process_id) + '-' +
                                  std::to_string(timestamp) + ".jsonl");
    capture_file.open(capture_path, std::ios::out | std::ios::trunc);
    if (!capture_file) {
        LOG_ERROR(Debug, "Could not open Bloodborne RE capture {}",
                  Common::FS::PathToUTF8String(capture_path));
        return;
    }
    capture_file << "{\"type\":\"header\",\"title\":\"CUSA03173\","
                    "\"app_version\":\"01.09\",\"image_base\":\"0x"
                 << std::hex << image_base << std::dec << "\",\"process_id\":" << process_id
                 << ",\"site_count\":" << Sites.size() << ",\"negative_area_patch\":"
                 << (negative_area_patch_installed ? "true" : "false")
                 << ",\"area_flag_patch\":" << (area_flag_patch_installed ? "true" : "false")
                 << ",\"responder_bell_area_patch\":"
                 << (responder_bell_area_patch_installed ? "true" : "false")
                 << ",\"responder_bell_common_patch\":"
                 << (responder_bell_common_patch_installed ? "true" : "false")
                 << ",\"active_bell_negative_area_patch\":"
                 << (active_bell_negative_area_patch_installed ? "true" : "false")
                 << ",\"active_bell_area_flag_patch\":"
                 << (active_bell_area_flag_patch_installed ? "true" : "false")
                 << ",\"responder_search_negative_area_patch\":"
                 << (responder_search_negative_area_patch_installed ? "true" : "false")
                 << ",\"sos_status_area_patch\":"
                 << (sos_status_area_patch_installed ? "true" : "false")
                 << ",\"summon_candidate_area_patch\":"
                 << (summon_candidate_area_patch_installed ? "true" : "false")
                 << ",\"summon_build_area_patch\":"
                 << (summon_build_area_patch_installed ? "true" : "false")
                 << ",\"summon_build_world_state_patch\":"
                 << (summon_build_world_state_patch_installed ? "true" : "false")
                 << ",\"summon_build_negative_event_patch\":"
                 << (summon_build_negative_event_patch_installed ? "true" : "false")
                 << ",\"stage_transition_keep_matching_patch\":"
                 << (stage_transition_keep_matching_patch_installed ? "true" : "false")
                 << ",\"summon_build_host_placement_hook\":"
                 << (summon_build_host_placement_hook_installed ? "true" : "false")
                 << ",\"cross_map_guest_handoff_hook\":"
                 << (cross_map_guest_handoff_hook_installed ? "true" : "false")
                 << ",\"summon_reload_state_hook\":"
                 << (summon_reload_state_hook_installed ? "true" : "false")
                 << ",\"deferred_summon_reload_hook\":"
                 << (deferred_summon_reload_hook_installed ? "true" : "false")
                 << ",\"healing_fountain_host_availability_hook\":"
                 << (healing_fountain_host_availability_hook_installed ? "true" : "false") << "}\n";
    capture_file.flush();

    size_t hook_count = 0;
    for (size_t index = 0; index < Sites.size(); ++index) {
        const auto& site = Sites[index];
        if ((summon_build_host_placement_hook_installed && site.offset == SummonBuildEntryOffset) ||
            (cross_map_guest_handoff_hook_installed && site.offset == CrossMapGuestHandoffOffset) ||
            (summon_reload_state_hook_installed &&
             site.offset == StageWarpDescriptorFinalizeOffset) ||
            (deferred_summon_reload_hook_installed && site.offset == SosStatusUpdateOffset) ||
            (healing_fountain_host_availability_hook_installed &&
             site.offset == HealingFountainAvailabilityOffset) ||
            (world_teardown_hook_installed && site.offset == WorldTeardownMatchingOffset) ||
            (ghost_death_revive_hook_installed && site.offset == GhostDeathReviveOffset) ||
            (host_death_respawn_hook_installed && site.offset == SoloPlayDeathRespawnOffset) ||
            (seamless_travel_hooks_installed && site.offset == StageTransitionEntryOffset) ||
            (stop_request_hook_installed && site.offset == MultiPlayStopRequestOffset)) {
            ++hook_count;
            continue;
        }
        const auto expected = std::span<const u8>{site.prologue.data(), site.prologue_size};
        if (!InstallGuestCodeHook(reinterpret_cast<void*>(image_base + site.offset), expected,
                                  index, TraceEntry)) {
            LOG_ERROR(Debug, "Failed to install Bloodborne RE hook {} at {:#x}", site.name,
                      site.offset);
            continue;
        }
        ++hook_count;
    }

    installed = hook_count == Sites.size();
    LOG_INFO(Debug, "Bloodborne RE trace installed {}/{} hooks; capture={}", hook_count,
             Sites.size(), Common::FS::PathToUTF8String(capture_path));
}

} // namespace Core::Bloodborne
