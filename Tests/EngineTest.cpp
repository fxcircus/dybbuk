// Offline DSP harness: drives the engine like a host and prints measurements,
// so behaviour can be verified without a DAW.
//
// THIS IS THE MOST IMPORTANT FILE IN THE REPO. Add a named scenario for every
// behaviour worth trusting, and make each one print a number that would change
// if the behaviour broke — RMS, a frequency estimate, a channel ratio. "It
// still builds" is not verification.
//
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest            (default)
//   build/EngineTest_artefacts/RelWithDebInfo/EngineTest sweep
#include "../Source/dsp/ExampleEngine.h"

#include <cstdio>
#include <cstring>

int main (int argc, char* argv[])
{
    const bool sweep = argc > 1 && std::strcmp (argv[1], "sweep") == 0;

    constexpr double sr = 48000.0;
    constexpr int block = 128;

    ExampleEngine engine;
    engine.prepare (sr, block);

    ExampleEngine::Params p;
    p.driveDb = 12.0f;

    juce::AudioBuffer<float> buffer (2, block);

    const double totalSeconds = 2.0;
    const int totalBlocks = (int) (totalSeconds * sr / block);
    double phase = 0.0;

    double windowSum = 0.0;
    int windowCount = 0;
    int zeroCrossings = 0;
    float prevSample = 0.0f;

    for (int b = 0; b < totalBlocks; ++b)
    {
        const double t = b * block / sr;

        if (sweep) // drive rises across the run; RMS should climb then compress
            p.driveDb = 24.0f * (float) (t / totalSeconds);

        for (int i = 0; i < block; ++i)
        {
            const float v = 0.25f * (float) std::sin (phase);
            phase += 220.0 / sr * juce::MathConstants<double>::twoPi;
            buffer.setSample (0, i, v);
            buffer.setSample (1, i, v);
        }

        engine.process (buffer, p);

        for (int i = 0; i < block; ++i)
        {
            const float s = buffer.getSample (0, i);
            windowSum += (double) s * s;
            if ((s > 0.0f) != (prevSample > 0.0f))
                ++zeroCrossings;
            prevSample = s;
        }
        windowCount += block;

        if (windowCount >= (int) (0.25 * sr))
        {
            const double freqEstimate = zeroCrossings / 2.0 / (windowCount / sr);
            std::printf ("t=%5.2fs  drive=%5.1f dB  rms=%.6f  ~freq=%6.1f Hz\n",
                         t, p.driveDb, std::sqrt (windowSum / windowCount), freqEstimate);
            windowSum = 0.0;
            windowCount = 0;
            zeroCrossings = 0;
        }
    }

    return 0;
}
