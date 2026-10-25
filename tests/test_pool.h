// test_pool.h: the block pool.
// Author: Ari Klopfer / A_Development. MIT
#pragma once
#include "check.h"
#include "BlockPool.h"

using namespace loop;

void testPool()
{
    BlockPool* p = new BlockPool();
    p->refill();
    check (p->allocated == (long) BlockPool::SPARE, "pool: refill makes SPARE blocks", (double) p->allocated);
    Block* b = p->take();
    bool zero = true;
    for (int i = 0; i < BLOCK; i += 997) zero = zero && b->l[i] == 0.f && b->r[i] == 0.f;
    check (zero, "pool: a taken block is zeroed");
    b->l[5] = 3.f;
    p->give (b);
    p->refill();
    check (p->allocated == (long) BlockPool::SPARE, "pool: a given block is reused, not reallocated", (double) p->allocated);
    int got = 0;
    bool reusedZero = true;
    std::vector<Block*> taken;
    while (Block* x = p->take()) { ++got; reusedZero = reusedZero && x->l[5] == 0.f; taken.push_back (x); }
    check (reusedZero, "pool: a reused block comes back zeroed");
    for (Block* x : taken) p->give (x);
    check (got == (int) BlockPool::SPARE, "pool: starved after SPARE takes without refill", got);
    p->freeLock.lock();   // a save in progress
    p->refill();
    check (p->freed.size() == BlockPool::SPARE && p->spare.size() == BlockPool::SPARE, "pool: while saving, given blocks stay alive and spares still refill", p->freed.size());
    p->freeLock.unlock();
    p->refill();
    check (p->freed.size() == 0 && p->allocated == (long) BlockPool::SPARE, "pool: frees resume after the hold", (double) p->allocated);
    delete p;
}
