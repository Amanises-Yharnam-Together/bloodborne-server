// bbport: what the GPU thread does with a draw whose graphics pipeline is not built yet
// (PipelineCache::GetGraphicsPipeline, BB_ASYNC_PIPELINES; tests/test_async_claim.cpp). Pure: no
// Vulkan, no game.
#pragma once

namespace BbAsyncClaim {

enum class Step {
    Claim,    ///< nobody builds it: claim it and build it (in the background when skippable)
    Skip,     ///< the background compiler builds it: the draw goes without it this frame
              ///< (only for a moment: past it, Wait)
    TakeOver, ///< a draw-preparation worker builds it: the background compiler builds it too
    Wait,     ///< someone builds it: wait for that (bounded), then build a copy
};

/// claimed: the pipeline is being built (pipelines_compiling holds its key).
/// background: the background compiler has a job for it (normal priority, done in moments).
/// skippable: the draw may go without its pipeline for a while (AsyncSkippable).
/// overdue: the background job has run longer than a draw may go without (BB_ASYNC_SKIP_MS).
///
/// A draw-preparation worker runs at idle priority: on a busy CPU (a laptop at 40 FPS) it waits
/// for seconds, and the draws skipped on its claim alone left objects missing that long (401826
/// draws in one play test without a single background compile).
///
/// A background job can take seconds too (v0.3.14 on NVIDIA laptops: 21194 draws went without
/// one pipeline; a teammate's face, a weapon or an enemy gone that long). Once overdue the draw
/// waits for it like any other (bounded, then the GPU thread builds a copy): a hitch, not a
/// missing object.
constexpr Step Next(bool claimed, bool background, bool skippable, bool overdue = false) {
    if (!claimed) {
        return Step::Claim;
    }
    if (!skippable) {
        return Step::Wait;
    }
    if (!background) {
        return Step::TakeOver;
    }
    return overdue ? Step::Wait : Step::Skip;
}

} // namespace BbAsyncClaim
