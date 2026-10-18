// bbport: what the GPU thread does with a draw whose graphics pipeline is not built yet
// (PipelineCache::GetGraphicsPipeline, BB_ASYNC_PIPELINES; tests/test_async_claim.cpp). Pure: no
// Vulkan, no game.
#pragma once

namespace BbAsyncClaim {

enum class Step {
    Claim,    ///< nobody builds it: claim it and build it (in the background when skippable)
    Skip,     ///< the background compiler builds it: the draw goes without it this frame
    TakeOver, ///< a draw-preparation worker builds it: the background compiler builds it too
    Wait,     ///< someone builds it: wait for that (bounded), then build a copy
};

/// claimed: the pipeline is being built (pipelines_compiling holds its key).
/// background: the background compiler has a job for it (normal priority, done in moments).
/// skippable: the draw may go without its pipeline for a while (AsyncSkippable).
///
/// A draw-preparation worker runs at idle priority: on a busy CPU (a laptop at 40 FPS) it waits
/// for seconds, and the draws skipped on its claim alone left objects missing that long (401826
/// draws in one play test without a single background compile).
constexpr Step Next(bool claimed, bool background, bool skippable) {
    if (!claimed) {
        return Step::Claim;
    }
    if (!skippable) {
        return Step::Wait;
    }
    return background ? Step::Skip : Step::TakeOver;
}

} // namespace BbAsyncClaim
