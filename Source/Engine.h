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

struct AudioClip
{
    juce::File file;
    double startTime = 0.0;
    double trimStart = 0.0;
    double length = 0.0;
    bool selected = false;
    juce::AudioBuffer<float> data;
};

class Track
{
public:
    Track (juce::String n, bool midi) : name (n), isMidi (midi) {}

    juce::String name;
    bool isMidi;
    float gain = 1.0f, pan = 0.0f, lastLevel = 0.0f;
    bool mute = false, solo = false, armed = false;

    juce::OwnedArray<AudioClip> clips;
    juce::MidiMessageSequence midi;

    std::unique_ptr<juce::AudioPluginInstance> instrument;
    std::vector<std::unique_ptr<juce::AudioPluginInstance>> fx;

    AudioClip* addClip (const juce::File& f, double startTime,
                        juce::AudioFormatManager& fm,
                        double targetSr, double trim = -1.0, double len = -1.0);
    void removeClip (AudioClip*);
    void splitClip (AudioClip*, double atTime);
    void prepare (double sr, int bs);
    void render (juce::AudioBuffer<float>&, juce::MidiBuffer&, double posSec);
    double endTime() const;
    juce::AudioThumbnail* thumbFor (AudioClip*, juce::AudioFormatManager&);

private:
    double srStored = 44100.0;
    juce::AudioFormatManager* fmPtr = nullptr;
    std::unique_ptr<juce::AudioThumbnailCache> thumbCache;
    std::map<juce::String, std::unique_ptr<juce::AudioThumbnail>> thumbs;
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
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage&) override;

    void play();
    void stop();
    bool isPlaying() const { return playing; }
    bool isRecording() const { return recording; }
    void toggleRecord();
    double getPosition() const { return position; }
    void setPosition (double sec) { position = juce::jmax (0.0, sec); }
    double getLength() const;

    Track* addTrack (const juce::String& name, bool midi);
    void removeTrack (Track* t);
    juce::OwnedArray<Track>& getTracks() { return tracks; }

    AudioClip* addAudioFileToTrack (Track&, const juce::File&, double startTime,
                                    double trim = -1.0, double len = -1.0);

    bool saveProject (const juce::File&);
    bool loadProject (const juce::File&);

    juce::KnownPluginList& getPluginList() { return pluginList; }
    juce::AudioPluginFormatManager& getFormatManager() { return formatManager; }
    void scanVst3Async (std::function<void(int)> onDone);
    bool isScanning() const { return scanning; }
    juce::AudioPluginInstance* loadInstrumentOnTrack (Track&, const juce::PluginDescription&);
    juce::AudioPluginInstance* addFxToTrack (Track&, const juce::PluginDescription&);
    PluginWindow* windowForPlugin (juce::AudioProcessor&, juce::String title);

    void startAudioRecording();
    void stopAudioRecordingAndAttach (Track& target);
    void startMidiRecording();
    void stopMidiRecording();
    bool exportToWav (const juce::File& out, std::function<void(double)> progress);

    juce::AudioDeviceManager deviceManager;
    juce::AudioFormatManager& getAudioFormatManager() { return audioFormatManager; }

private:
    juce::OwnedArray<Track> tracks;
    juce::KnownPluginList pluginList;
    juce::AudioPluginFormatManager formatManager;
    juce::VST3PluginFormat vst3Format;
    juce::AudioFormatManager audioFormatManager;
    juce::TimeSliceThread scanThread { "scan" };

    bool playing = false, recording = false, scanning = false;
    double position = 0.0, sampleRate = 44100.0;
    int blockSize = 512;

    juce::AudioBuffer<float> trackBuffer;
    juce::MidiBuffer midiScratch;

    juce::File lastRecording;
    double recStart = 0.0;
    std::unique_ptr<juce::AudioFormatWriter::ThreadedWriter> writer;
    std::unique_ptr<juce::FileOutputStream> recStream;
    juce::WavAudioFormat wavFormat;
    juce::CriticalSection recLock;

    juce::OwnedArray<PluginWindow> pluginWindows;
    std::thread vstScanThread;

    void processBlockInternal (juce::AudioBuffer<float>& out, int numSamples);
};

} // namespace fbd
