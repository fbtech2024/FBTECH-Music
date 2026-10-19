#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>

namespace fbd
{

class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor& p, const juce::String& title)
        : DocumentWindow (title, juce::Colours::darkgrey, closeButton),
          proc (p)
    {
        setUsingNativeTitleBar (true);
        auto* ed = p.createEditorIfNeeded();
        setContentOwned (ed != nullptr ? static_cast<juce::Component*> (ed)
                                       : static_cast<juce::Component*> (new juce::GenericAudioProcessorEditor (p)),
                         true);
        setResizable (true, true);
        centreWithSize (420, 320);
        setVisible (true);
    }
    void closeButtonPressed() override { setVisible (false); }
    juce::AudioProcessor& getProcessor() const { return proc; }
    JUCE_DECLARE_NON_COPYABLE (PluginWindow)

private:
    juce::AudioProcessor& proc;
};

class Track
{
public:
    Track (juce::String n, bool midi) : name (n), isMidi (midi) {}

    juce::String name;
    bool isMidi;
    float gain = 1.0f, pan = 0.0f, lastLevel = 0.0f;
    bool mute = false, solo = false, armed = false;

    juce::AudioTransportSource player;
    juce::AudioFormatReaderSource* readerSource = nullptr;
    juce::TimeSliceThread readerThread { "readahead" };

    juce::MidiMessageSequence midi;
    double nextEventTime = 0.0;

    std::unique_ptr<juce::AudioPluginInstance> instrument;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> fx;

    void loadFile (const juce::File& f, juce::AudioFormatManager& fm);
    void render (juce::AudioBuffer<float>& out, juce::MidiBuffer& mb,
                 double posSec, double sr, int blockSize);
    void prepare (double sr, int blockSize);
    double fileLength() const { return readerSource ? readerSource->getTotalLength() / srStored : 0.0; }
    juce::AudioThumbnail& thumb();
    double srStored = 44100.0;

private:
    juce::AudioFormatManager* fmPtr = nullptr;
    std::unique_ptr<juce::AudioThumbnailCache> thumbCache;
    std::unique_ptr<juce::AudioThumbnail> thumbnail;
    void ensureThumb();
};

class Engine : public juce::AudioIODeviceCallback,
               public juce::MidiInputCallback
{
public:
    Engine();
    ~Engine() override;

    void audioDeviceAboutToStart (juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;
    void audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                           int numInputChannels,
                                           float* const* outputChannelData,
                                           int numOutputChannels,
                                           int numSamples,
                                           const juce::AudioIODeviceCallbackContext&) override;

    void prepareToPlay (int samplesPerBlockExpected, double newSampleRate);
    void releaseResources();

    void handleIncomingMidiMessage (juce::MidiInput* source, const juce::MidiMessage& msg) override;

    void play();
    void stop();
    bool isPlaying() const { return playing; }
    bool isRecording() const { return recording; }
    void toggleRecord();
    double getPosition() const;
    void setPosition (double sec);
    double getLength() const;

    Track* addTrack (const juce::String& name, bool midi);
    void removeTrack (Track* t);
    juce::OwnedArray<Track>& getTracks() { return tracks; }
    void loadFileOnTrack (Track& t, const juce::File& f) { t.loadFile (f, audioFormatManager); }

    juce::KnownPluginList& getPluginList() { return pluginList; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    void scanDefaultVst3Folders();
    juce::AudioPluginInstance* loadInstrumentOnTrack (Track&, const juce::PluginDescription&);
    juce::AudioPluginInstance* addFxToTrack (Track&, const juce::PluginDescription&);
    PluginWindow* windowForPlugin (juce::AudioProcessor&, juce::String title);

    void startAudioRecording();
    void stopAudioRecordingAndAttach (Track& target);

    void startMidiRecording();
    void stopMidiRecording();

    bool exportToWav (const juce::File& out, std::function<void(double)> progress);

    juce::AudioDeviceManager deviceManager;

private:
    juce::OwnedArray<Track> tracks;
    juce::KnownPluginList pluginList;
    juce::AudioPluginFormatManager formatManager;
    juce::VST3PluginFormat vst3Format;
    juce::AudioFormatManager audioFormatManager;
    juce::TimeSliceThread scanThread { "scan" };

    bool playing = false, recording = false;
    double position = 0.0, sampleRate = 44100.0;
    int blockSize = 512;

    juce::AudioBuffer<float> trackBuffer;
    juce::MidiBuffer midiScratch;

    juce::File lastRecording;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::unique_ptr<juce::FileOutputStream> recStream;
    juce::WavAudioFormat wavFormat;
    juce::CriticalSection recLock;

    juce::OwnedArray<PluginWindow> pluginWindows;

    void processBlockInternal (juce::AudioBuffer<float>& out, int numSamples);
};

} // namespace fbd
