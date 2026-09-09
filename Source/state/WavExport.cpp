#include "WavExport.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace wavexport
{
    bool writeStereoFloat (const juce::File& dest, const juce::AudioBuffer<float>& buffer,
                           double sampleRate)
    {
        if (buffer.getNumSamples() <= 0 || sampleRate <= 0.0)
            return false;

        if (! dest.getParentDirectory().createDirectory())
            return false;

        // A half-written file from an earlier failure must not survive under
        // the name the caller was promised.
        dest.deleteFile();

        std::unique_ptr<juce::OutputStream> stream = dest.createOutputStream();
        if (stream == nullptr)
            return false;

        // The source may be mono (a pattern rendered before the engine grew a
        // stereo voice); the file is always stereo so a DAW drops it straight
        // onto the track the plugin sits on.
        juce::AudioBuffer<float> stereo (2, buffer.getNumSamples());
        for (int ch = 0; ch < 2; ++ch)
            stereo.copyFrom (ch, 0, buffer, juce::jmin (ch, buffer.getNumChannels() - 1), 0,
                             buffer.getNumSamples());

        juce::WavAudioFormat wav;
        const auto options = juce::AudioFormatWriterOptions()
                                 .withSampleRate (sampleRate)
                                 .withNumChannels (2)
                                 .withBitsPerSample (32)
                                 .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
        auto writer = wav.createWriterFor (stream, options);
        if (writer == nullptr)
            return false;

        const bool ok = writer->writeFromAudioSampleBuffer (stereo, 0, stereo.getNumSamples());
        writer.reset(); // flushes the header before the caller reads the file back
        return ok;
    }
} // namespace wavexport
