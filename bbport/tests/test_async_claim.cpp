// The GPU thread's step for a draw whose pipeline is not built yet (gpu/shim/bbport_async_claim.h):
// a skippable draw never goes without its pipeline on a draw-preparation worker's claim alone.
// No game, Vulkan device or window.
//   ninja -C out/gpu async-claim-test && out/gpu/async-claim-test.exe
#include <cassert>
#include <cstdio>
#include "bbport_async_claim.h"

using BbAsyncClaim::Next;
using BbAsyncClaim::Step;

int main() {
    // Nobody builds it: claimed by this draw, skippable or not.
    assert(Next(false, false, true) == Step::Claim);
    assert(Next(false, false, false) == Step::Claim);
    // The background compiler builds it: a skippable draw goes without for now.
    assert(Next(true, true, true) == Step::Skip);
    // A worker (idle priority) builds it: the background compiler builds it too, the draw goes
    // without only from then on.
    if (Next(true, false, true) != Step::TakeOver) {
        std::printf("async-claim-test: a skippable draw skips on a worker's claim\n");
        return 1;
    }
    // A draw that may not go without waits, whoever builds it.
    assert(Next(true, false, false) == Step::Wait);
    assert(Next(true, true, false) == Step::Wait);
    std::printf("async-claim-test: ok\n");
    return 0;
}
