// The flip IRQ's wait for its buffer label (gpu/shim/bbport_flip_label.h): a label set a moment
// after the IRQ is waited for, one never set gives up after the timeout, and either way the caller
// flips. No game, Vulkan device or window.
//   ninja -C out/gpu flip-label-test && out/gpu/flip-label-test.exe
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include "bbport_flip_label.h"

using namespace std::chrono;

int main() {
    // Already set: no wait at all.
    std::uint64_t label = 1;
    auto start = steady_clock::now();
    assert(BbFlipLabel::WaitForLabel(label, milliseconds(20)));
    assert(steady_clock::now() - start < milliseconds(5));

    // Set 3 ms after the IRQ by another thread (the draw recording thread's label write).
    label = 0;
    std::thread writer([&] {
        std::this_thread::sleep_for(milliseconds(3));
        reinterpret_cast<volatile std::uint64_t&>(label) = 1;
    });
    assert(BbFlipLabel::WaitForLabel(label, milliseconds(500)));
    writer.join();

    // Never set: false once the timeout has passed, not before, and not much later.
    label = 0;
    start = steady_clock::now();
    assert(!BbFlipLabel::WaitForLabel(label, milliseconds(20)));
    const auto waited = steady_clock::now() - start;
    assert(waited >= milliseconds(20));
    assert(waited < milliseconds(400));

    // A label of another value (the flip before already reset it) counts as not set.
    label = 2;
    assert(!BbFlipLabel::WaitForLabel(label, milliseconds(1)));

    std::puts("PASS: flip label: set at once, set late, never set (timeout), other value");
}
