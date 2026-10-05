#include "motion_bytes.hpp"
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

using Cache = vii::MotionBytes;
using R = Cache::Result;
using M = Cache::Mode;
static unsigned checks = 0;
#define CHECK(condition) do { ++checks; if (!(condition)) { \
    std::fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #condition); std::exit(1); } } while (false)
static const char* path = "/MODEL/CHARA/061/MOTION/BATTLE.cl3";
struct Heap {
    size_t live = 0, peak = 0, allocations = 0;
    bool fail = false;
    static void* Allocate(void* context, size_t size) noexcept {
        auto& h = *static_cast<Heap*>(context);
        if (h.fail) return nullptr;
        auto p = static_cast<size_t*>(std::malloc(size + sizeof(size_t)));
        if (!p) return nullptr;
        *p = size; h.live += size; ++h.allocations;
        if (h.live > h.peak) h.peak = h.live;
        return p + 1;
    }
    static void Release(void* context, void* bytes) noexcept {
        auto p = static_cast<size_t*>(bytes) - 1;
        static_cast<Heap*>(context)->live -= *p; std::free(p);
    }
    Cache::Allocator Ops() { return {this, Allocate, Release}; }
};
static void OwnershipAndAudit() {
    Heap heap;
    {
        Cache c(M::Audit, 8, heap.Ops());
        char source[] = "abcd", destination[] = "zzzz";
        CHECK(c.Put(1, path, source, 4) == R::Stored);
        source[0] = 'x';
        CHECK(c.Get(1, path, destination, 4) == R::Candidate);
        CHECK(!std::memcmp(destination, "zzzz", 4));
        CHECK(c.Put(1, path, "abcd", 4) == R::Verified);
        CHECK(c.Put(1, path, source, 4) == R::Mismatch);
        CHECK(c.Get(1, path, destination, 4) == R::Disabled);
        CHECK(c.Put(1, path, "abcd", 4) == R::Disabled);
        auto s = c.Snapshot();
        CHECK(s.hits == 0 && s.comparisons == 2 && s.mismatches == 1 && s.disabled);
        CHECK(s.bytes == 0 && s.entries == 0 && heap.live == 0);
    }
    {
        Cache c(M::Replay, 8, heap.Ops());
        char source[] = "abcd", destination[4]{};
        CHECK(c.Put(1, path, source, 4) == R::Stored);
        source[0] = 'x';
        CHECK(c.Get(1, path + 1, destination, 4) == R::Hit);
        CHECK(!std::memcmp(destination, "abcd", 4));
        destination[0] = 'y';
        CHECK(c.Get(1, path, destination, 4) == R::Hit);
        CHECK(!std::memcmp(destination, "abcd", 4));
        CHECK(c.Get(1, path, destination, 3) == R::Miss);
        CHECK(c.Get(2, path, destination, 4) == R::Miss);
        CHECK(c.Get(1, "/model/chara/061/motion/battle.cl3", destination, 4) == R::Miss);
        CHECK(c.Put(2, path, "efgh", 4) == R::Stored);
        CHECK(c.Get(2, path, destination, 4) == R::Hit);
        CHECK(!std::memcmp(destination, "efgh", 4));
        CHECK(c.Snapshot().bytes == 8);
        c.Close(); c.Close();
        CHECK(heap.live == 0 && c.Snapshot().closed);
        CHECK(c.Get(1, path, destination, 4) == R::Disabled);
    }
    CHECK(heap.live == 0 && heap.peak <= 8);
}
static void BoundsAndFailures() {
    Heap heap;
    {
        Cache c(M::Replay, 8, heap.Ops()); char destination[4]{};
        CHECK(c.Put(1, path, "abcd", 4) == R::Stored);
        CHECK(c.Put(2, path, "efgh", 4) == R::Stored);
        CHECK(c.Get(1, path, destination, 4) == R::Hit); // Make namespace 2 oldest.
        CHECK(c.Put(3, path, "ijkl", 4) == R::Stored);
        CHECK(c.Get(2, path, destination, 4) == R::Miss);
        CHECK(c.Get(1, path, destination, 4) == R::Hit);
        CHECK(c.Snapshot().evictions == 1 && heap.live == 8 && heap.peak <= 8);
        heap.fail = true;
        CHECK(c.Put(4, path, "mnop", 4) == R::Rejected);
        CHECK(c.Snapshot().bytes == 4 && heap.live == 4);
        heap.fail = false;
        CHECK(c.Put(4, path, "mnop", 4) == R::Stored);
        CHECK(c.Put(4, path, "different", 3) == R::Mismatch);
        CHECK(heap.live == 0 && c.Snapshot().disabled);
    }
    {
        Cache c(M::Replay, 1024, heap.Ops());
        for (uint32_t i = 0; i < 129; ++i) CHECK(c.Put(i, path, "a", 1) == R::Stored);
        CHECK(c.Snapshot().entries == 128 && c.Snapshot().evictions == 1);
        char destination{};
        CHECK(c.Get(0, path, &destination, 1) == R::Miss);
        CHECK(c.Get(128, path, &destination, 1) == R::Hit && destination == 'a');
    }
    CHECK(heap.live == 0);
    {
        Cache c(M::Replay, 8, heap.Ops()); char destination[4]{};
        const char* invalid[] = {"", "/", "MODEL", "MODEL/CHARA//MOTION/BATTLE.cl3",
            "MODEL/CHARA/a/MOTION/BATTLE.cl3", "MODEL/CHARA/1/MOTION/BATTLE.cl3.extra",
            "//MODEL/CHARA/1/MOTION/BATTLE.cl3", "MODEL/CHARA/1/MOTION/IDLE.cl3"};
        for (auto p : invalid) CHECK(c.Put(1, p, "abcd", 4) == R::Rejected);
        const std::string longPath = "MODEL/CHARA/" + std::string(256, '1') + "/MOTION/BATTLE.cl3";
        CHECK(c.Put(1, longPath.c_str(), "abcd", 4) == R::Rejected);
        CHECK(c.Put(1, nullptr, "abcd", 4) == R::Rejected);
        CHECK(c.Put(1, path, nullptr, 4) == R::Rejected);
        CHECK(c.Put(1, path, "abcd", 0) == R::Rejected);
        CHECK(c.Put(1, path, "too large", 9) == R::Rejected);
        CHECK(c.Get(1, path, nullptr, 4) == R::Rejected);
        CHECK(c.Get(1, path, destination, 0) == R::Rejected);
        CHECK(c.Snapshot().entries == 0);
    }
    bool rejected = false;
    try { Cache c(M::Replay, 0); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
    rejected = false;
    try { Cache c(M::Replay, 64u * 1024u * 1024u + 1); } catch (const std::invalid_argument&) { rejected = true; }
    CHECK(rejected);
}
static void ConcurrentCalls() {
    Cache c(M::Replay, 1024);
    std::atomic<bool> failed{false}; std::vector<std::thread> threads;
    for (uint32_t t = 0; t < 4; ++t) threads.emplace_back([&, t] {
        char source[4] = {static_cast<char>('a' + t), 'b', 'c', 'd'}, destination[4]{};
        if (c.Put(t, path, source, 4) != R::Stored) failed = true;
        for (unsigned i = 0; i < 1000; ++i) {
            if (c.Get(t, path, destination, 4) != R::Hit || std::memcmp(source, destination, 4)) failed = true;
            if (c.Put(t, path, source, 4) != R::Verified) failed = true;
        }
    });
    for (auto& thread : threads) thread.join();
    CHECK(!failed);
    auto s = c.Snapshot();
    CHECK(s.hits == 4000 && s.comparisons == 4000 && s.bytes == 16 && s.mismatches == 0);
}
int main() {
    OwnershipAndAudit(); BoundsAndFailures(); ConcurrentCalls();
    std::printf("PASS: %u checks; audit/replay ownership, namespace and size isolation, LRU bounds, allocation failure, mismatch shutdown, cleanup, 4000 concurrent hits\n", checks);
}
