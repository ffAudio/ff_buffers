/*
  ==============================================================================

    ElasticDelayBuffer.h
    Created: 30 Jul 2017 12:56:50am
    Author:  Dobby

  ==============================================================================
*/

#pragma once

template <typename SampleType>
class ElasticDelayBuffer {
    juce::AudioBuffer<SampleType>           buffer;

    juce::OwnedArray<CircularLagrangeInterpolator> resamplers;

    int writePosition = 0;
    int readPosition  = 0;
    
    std::atomic<int> numDelaySamples;
    
    double maxResamplingFactor = 8.0;

public:
    ElasticDelayBuffer ()
    {
    }
    
    void setSize (const int numChannels, const int numSamples, const double sampleRate)
    {
        buffer.setSize (numChannels, numSamples, false, true);
        while (resamplers.size() < numChannels) {
            resamplers.add (new CircularLagrangeInterpolator());
        }
        while (resamplers.size() > numChannels) {
            resamplers.removeLast();
        }
        if (writePosition >= numSamples) {
            writePosition = 0;
            buffer.clear();
            for (auto r : resamplers) {
                r->reset();
            }
        }
        updateActualSampleDelay();
    }

    void setNumSamplesDelay (const int delay) {
        jassert (delay < buffer.getNumSamples());
        const int assumedRead = writePosition - delay;
        readPosition = (assumedRead < 0) ? assumedRead + buffer.getNumSamples() : assumedRead;
        reset();

        updateActualSampleDelay();
    }

    void setMaxResamplingFactor (const double factor) {
        maxResamplingFactor = factor;
    }

    int getActualSampleDelay () const
    {
        return numDelaySamples;
    }

    void updateActualSampleDelay ()
    {
        numDelaySamples = (writePosition < readPosition) ? writePosition + buffer.getNumSamples() - readPosition : writePosition - readPosition;
    }

    void reset ()
    {
        for (auto* r : resamplers)
            r->reset();
    }

    void pushBlock (const juce::AudioBuffer<SampleType>& inputBuffer, const int numSamples, const SampleType gain = 1.0f)
    {
        jassert (buffer.getNumSamples()  >  numSamples);
        jassert (buffer.getNumChannels() == inputBuffer.getNumChannels());

        const int available = buffer.getNumSamples() - (writePosition + numSamples);
        if (available >= 0) {
            for (int i=0; i < buffer.getNumChannels(); ++i)
                buffer.copyFrom (i, writePosition, inputBuffer.getReadPointer (i), numSamples, gain);

            writePosition += numSamples;
        }
        else {
            for (int i=0; i < buffer.getNumChannels(); ++i) {
                buffer.copyFrom (i, writePosition, inputBuffer.getReadPointer (i), numSamples + available, gain);
                buffer.copyFrom (i, 0, inputBuffer.getReadPointer (i, numSamples + available), -available, gain);
            }
            writePosition = -available;
        }

        updateActualSampleDelay();
    }

    void addToPushedBlock (const juce::AudioBuffer<SampleType>& inputBuffer, const int numSamples, const SampleType gain = 1.0f)
    {
        jassert (buffer.getNumSamples()  >  numSamples);
        jassert (buffer.getNumChannels() == inputBuffer.getNumChannels());

        int previousWrite = writePosition - numSamples;
        if (previousWrite < 0)
            previousWrite += buffer.getNumSamples();
        const int available = buffer.getNumSamples() - (previousWrite + numSamples);
        if (available >= 0) {
            for (int i=0; i < buffer.getNumChannels(); ++i)
                buffer.addFrom (i, previousWrite, inputBuffer.getReadPointer (i), numSamples, gain);
        }
        else {
            for (int i=0; i < buffer.getNumChannels(); ++i) {
                buffer.addFrom (i, previousWrite, inputBuffer.getReadPointer (i), numSamples + available, gain);
                buffer.addFrom (i, 0, inputBuffer.getReadPointer (i, numSamples + available), -available, gain);
            }
        }
    }

    void pullBlock (juce::AudioBuffer<SampleType>& outputBuffer, const int numSamples, const int delayTime)
    {
        jassert (buffer.getNumSamples()  >  outputBuffer.getNumSamples());
        jassert (buffer.getNumChannels() == outputBuffer.getNumChannels());
        jassert (buffer.getNumChannels() == resamplers.size());

        int currentDelay = writePosition - readPosition;
        if (currentDelay < 0) currentDelay += buffer.getNumSamples();

        double difference = (currentDelay - numSamples) - delayTime;
        double factor = 1.0 + (difference / (numSamples * 8.0));
        if (factor < 0.0001)
            factor = 0.0001;
        if (factor > maxResamplingFactor)
            factor = maxResamplingFactor;

        int used = 0;
        for (int channel=0; channel < outputBuffer.getNumChannels(); ++channel) {
            used = resamplers.getUnchecked (channel)->process (factor, buffer.getReadPointer (channel, readPosition),
                                                               outputBuffer.getWritePointer (channel),
                                                               numSamples,
                                                               buffer.getNumSamples() - readPosition,
                                                               buffer.getNumSamples());
        }
        readPosition = (readPosition + used) % buffer.getNumSamples();
        
        updateActualSampleDelay();
    }
};

