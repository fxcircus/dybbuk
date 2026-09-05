#pragma once

// xorshift32. The plan's tell 4 is that the same settings never land in the
// same place twice, so the loop needs continuous low-level noise; this is
// cheap enough to call several times per sample. Seeded per stage so tests
// can be made deterministic (seedForTests) or deliberately not (drift).
struct Rng
{
    unsigned int state = 2463534242u;

    void seed (unsigned int s) noexcept { state = (s == 0u ? 1u : s); }

    inline float white() noexcept
    {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) (int) state * (1.0f / 2147483648.0f); // [-1, 1)
    }
};
