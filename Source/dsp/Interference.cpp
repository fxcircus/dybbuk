#include "Interference.h"

#include <juce_core/juce_core.h>

#include "ChipConstants.h"

void Interference::prepare (double sampleRate)
{
    sr = sampleRate;
    controlRate = (float) sampleRate / (float) modk::kControlBlock;

    auto coeff = [] (float ms, float rate)
    { return 1.0f - std::exp (-1.0f / juce::jmax (1.0f, ms * 0.001f * rate)); };

    aEnergyUp = coeff (modk::kEnergyUpMs, controlRate);
    aEnergyDown = coeff (modk::kEnergyDownMs, controlRate);
    aHeat = 1.0f - std::exp (-1.0f / (modk::kIntfHeatSeconds * controlRate));
    aSlew = 1.0f - std::exp (-pt::kTwoPi * modk::kIntfSlewHz / controlRate);

    aCrackleAttack = 1.0f - std::exp (-1.0f / (modk::kCrackleAttackMs * 0.001f * (float) sampleRate));
    crackleDecay = std::exp (-1.0f / (modk::kCrackleDecayMs * 0.001f * (float) sampleRate));

    reset();
}

void Interference::seed (unsigned int s) noexcept { rng.seed (s); }

void Interference::reset() noexcept
{
    x = 1.0f;
    y = 1.0f;
    z = 20.0f;
    energy = 0.0f;
    heat = 0.0f;
    wanderSlew = wanderCur = wanderInc = 0.0f;
    crackleEnv = crackleTarget = 0.0f;
    crackleThreshold = 2.0f; // above the RNG's range: no ticks until energy arrives
}

void Interference::tick (float loopEnv, float loopSample)
{
    // The one modulation feedback input, sanitised at the boundary.
    if (! (loopEnv >= 0.0f) || loopEnv > modk::kLoopEnvCeiling)
        loopEnv = 0.0f;
    if (! std::isfinite (loopSample))
        loopSample = 0.0f;

    const float db = 20.0f * std::log10 (juce::jmax (loopEnv, 1.0e-9f));
    const float raw = juce::jlimit (0.0f, 1.0f,
                                    (db - modk::kEnergyFloorDb)
                                        / (modk::kEnergyCeilDb - modk::kEnergyFloorDb));
    energy += (raw - energy) * (raw > energy ? aEnergyUp : aEnergyDown);
    heat += (energy - heat) * aHeat;

    // rho sweeps across the chaos threshold (24.74) with energy, so a quiet
    // loop settles and a hot one never repeats. Heat is the only long-timescale
    // memory in the system: a loop that has been hot for a while gets wilder.
    const float rho = modk::kIntfRhoMin
                      + energy * (modk::kIntfRhoMax - modk::kIntfRhoMin)
                      + modk::kIntfHeatRho * heat;
    const float dt = (modk::kIntfSpeedMin
                      + energy * (modk::kIntfSpeedMax - modk::kIntfSpeedMin)) / controlRate;

    // The audio itself nudges the state, which is what makes the source
    // correlated with what you played rather than merely random.
    x += modk::kIntfInject * energy * loopSample;

    auto derivative = [rho] (float px, float py, float pz, float& dx, float& dy, float& dz)
    {
        dx = modk::kLorenzSigma * (py - px);
        dy = px * (rho - pz) - py;
        dz = px * py - modk::kLorenzBeta * pz;
    };

    float k1x, k1y, k1z, k2x, k2y, k2z;
    derivative (x, y, z, k1x, k1y, k1z);
    const float half = 0.5f * dt;
    derivative (x + half * k1x, y + half * k1y, z + half * k1z, k2x, k2y, k2z);
    x += dt * k2x;
    y += dt * k2y;
    z += dt * k2z;

    x = juce::jlimit (-modk::kLorenzBound, modk::kLorenzBound, x);
    y = juce::jlimit (-modk::kLorenzBound, modk::kLorenzBound, y);
    z = juce::jlimit (0.0f, 2.0f * modk::kLorenzBound, z);

    if (! std::isfinite (x + y + z))
    {
        x = 1.0f;
        y = 1.0f;
        z = 20.0f;
    }

    // y - x is dx/dt over sigma: exactly zero at any fixed point, so a quiet
    // loop produces no modulation at all. Using x directly would park on one
    // lobe and detune the delay by a constant.
    const float wanderRaw = pt::fastTanh ((y - x) * modk::kIntfWanderScale);
    wanderSlew += (wanderRaw - wanderSlew) * aSlew;
    wanderInc = (wanderSlew - wanderCur) * modk::kInvControlBlock;

    // Crackle density grows with the square of energy. Rng::white is uniform
    // in [-1, 1), so the threshold is expressed in that space.
    const float density = modk::kCrackleMaxRate * energy * energy / (float) sr;
    crackleThreshold = density > 0.0f ? 1.0f - 2.0f * density : 2.0f;
}
