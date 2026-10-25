// BlockPool.h: fixed-size stereo audio blocks handed between the audio thread and a worker thread. No Rack dependency.
// take() and give() are lock-free and never allocate or free. They run on the audio thread, or on another thread while
// Rack's engine lock keeps process() out (onAdd / onReset): never from two threads at once. The worker refill()s spares
// and deletes returned blocks. One producer and one consumer per queue.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include <atomic>
#include <cstring>
#include <mutex>
#include <new>

namespace loop
{
constexpr int BLOCK = 65536;   // frames per block: 512 KB stereo, ~1.4 s at 48 kHz

struct Block
{
    float l[BLOCK];
    float r[BLOCK];
};

// Single-producer single-consumer ring of block pointers. CAP must be a power of two.
template <unsigned CAP>
struct Spsc
{
    static_assert (CAP > 0 && (CAP & (CAP - 1)) == 0, "Spsc: CAP must be a power of two (the unsigned indices wrap)");
    Block* slot[CAP];
    std::atomic<unsigned> head { 0 }, tail { 0 };

    bool push (Block* b)
    {
        const unsigned h = head.load (std::memory_order_relaxed);
        if (h - tail.load (std::memory_order_acquire) == CAP) return false;
        slot[h % CAP] = b;
        head.store (h + 1, std::memory_order_release);
        return true;
    }
    Block* pop()
    {
        const unsigned t = tail.load (std::memory_order_relaxed);
        if (t == head.load (std::memory_order_acquire)) return nullptr;
        Block* b = slot[t % CAP];
        tail.store (t + 1, std::memory_order_release);
        return b;
    }
    unsigned size() const { return head.load (std::memory_order_acquire) - tail.load (std::memory_order_acquire); }
};

struct BlockPool
{
    static constexpr unsigned SPARE = 16;   // spares kept ready: ~22 s of recording at 48 kHz between refills
    Spsc<64> spare;                         // worker -> audio, zeroed blocks
    Spsc<32768> freed;                      // audio -> worker; 5 tracks x 2 lists x 2048 blocks fit
    std::mutex freeLock;                    // held by the UI thread while it reads track blocks (save): freed blocks wait
    std::atomic<long> allocated { 0 };      // blocks alive, for the tests and the menu

    // audio thread, or another thread while the engine lock excludes process(): never two threads at once
    Block* take() { return spare.pop(); }   // a zeroed block, or null when starved
    void give (Block* b) { if (b && ! freed.push (b)) leaked++; }   // ponytail: a full queue leaks the block (needs > 32768 frees between refills)

    // worker thread (also the UI thread, before the worker starts or after it stops)
    void refill()
    {
        {
            std::unique_lock<std::mutex> lock (freeLock, std::try_to_lock);   // a save in progress: free later, still refill
            if (lock)
                while (Block* b = freed.pop())
                {
                    if (spare.size() < SPARE) { std::memset (b, 0, sizeof (Block)); spare.push (b); }
                    else { delete b; allocated--; }
                }
        }
        while (spare.size() < SPARE)
        {
            Block* b = new (std::nothrow) Block();   // value-initialised: zeroed
            if (! b) return;                         // out of RAM: the tracks see a starved pool
            allocated++;
            spare.push (b);
        }
    }
    Block* make()   // UI thread, loading a saved loop: a zeroed block the caller hands to a track
    {
        Block* b = new (std::nothrow) Block();
        if (b) allocated++;
        return b;
    }
    void drain()   // only when no other thread touches the pool
    {
        while (Block* b = freed.pop()) { delete b; allocated--; }
        while (Block* b = spare.pop()) { delete b; allocated--; }
    }
    ~BlockPool() { drain(); }

    long leaked = 0;
};
}
