#include "Agitation.h"

#include <juce_core/juce_core.h>

void Agitation::prepare (double sampleRate)
{
    sr = sampleRate;
    recomputeShape();
    reset();
}

void Agitation::reset() noexcept
{
    phase = 0.0;
    lastOut = 0.0f;
    running = mode == Mode::loop;
}

void Agitation::setSpeedHz (float hz)
{
    const float clamped = juce::jlimit (modk::kAgitSpeedMin, modk::kAgitSpeedMax, hz);
    if (std::abs (clamped - speedHz) > 1.0e-6f)
    {
        speedHz = clamped;
        recomputeShape();
    }
}

void Agitation::setAngle (float angle01)
{
    const float clamped = juce::jlimit (0.0f, 1.0f, angle01);
    if (std::abs (clamped - angle) > 1.0e-6f)
    {
        angle = clamped;
        recomputeShape();
    }
}

void Agitation::setMode (Mode m) noexcept
{
    if (m == mode)
        return;
    mode = m;
    if (mode == Mode::loop)
        running = true;
}

void Agitation::recomputeShape()
{
    const double periodSamples = sr / (double) speedHz;

    // The rise/fall split, floored so the shortest segment stays a ramp. A
    // one-sample cliff into Time is a pitch snap, which is a click.
    const float raw = modk::kAngleMin + angle * (1.0f - 2.0f * modk::kAngleMin);
    const float minFrac = (float) (modk::kAgitMinSegmentMs * 0.001 * sr / juce::jmax (1.0, periodSamples));

    attackFraction = minFrac >= 0.5f ? 0.5f : juce::jlimit (minFrac, 1.0f - minFrac, raw);
    invAttack = 1.0f / juce::jmax (attackFraction, 1.0e-6f);
    invDecay = 1.0f / juce::jmax (1.0f - attackFraction, 1.0e-6f);

    phaseInc = (double) speedHz / sr;
}
