#include "Engine.h"

namespace fbd
{

void Track::loadFile (const juce::File& f, juce::AudioFormatManager& fm)
{
    if (auto* reader = fm.createReaderFor (f))
    {
        if (readerSource != nullptr) readerSource->releaseResources();
        readerSource = new juce::AudioFormatReaderSource (reader, true);
        player.setSource (readerSource, 4096, &readerThread);
        fmPtr = &fm;
        ensureThumb();
        thumbnail->setSource (new juce::FileInputSource (f));
    }
}

void Track::ensureThumb()
{
    if (thumbnail == nullptr)
    {
        thumbCache  = std::make_unique<juce::AudioThumbnailCache> (8);
        thumbnail   = std::make_unique<juce::AudioThumbnail> (256, *fmPtr, *thumbCache);
    }
}

juce::AudioThumbnail& Track::thumb()
{
    if (thumbnail == nullptr)
    {
        thumbCache = std::make_unique<juce::AudioThumbnailCache> (8);
        thumbnail = std::make_unique<juce::AudioThumbnail> (256, *fmPtr, *thumbCache);
    }
    return *thumbnail;
}

void Track::prepare (double sr, int bs)
{
    srStored = sr;
    player.prepareToPlay (bs, sr);
    auto setupProc = [&] (juce::AudioPluginInstance& p)
    {
        p.enableAllBuses();
        p.setPlayConfigDetails (2, 2, sr, bs);
        p.prepareToPlay (sr, bs);
    };
    if (instrument != nullptr) setupProc (*instrument);
    for (auto& f : fx) setupProc (*f);
    nextEventTime = 0.0;
}

void Track::render (juce::AudioBuffer<float>& out, juce::MidiBuffer& mb,
                    double posSec, double, int)
{
    out.clear();
    mb.clear();

    if (isMidi)
    {
        double blockDur = out.getNumSamples() / srStored;
        for (int i = 0; i < midi.getNumEvents(); ++i)
        {
            auto* ev = midi.getEventPointer (i);
            double t = ev->message.getTimeStamp();
            if (t >= posSec && t < posSec + blockDur)
            {
                auto m = ev->message;
                m.setTimeStamp (0);
                mb.addEvent (m, juce::roundToInt ((t - posSec) * srStored));
            }
        }
        if (instrument != nullptr)
            instrument->processBlock (out, mb);
    }
    else
    {
        juce::AudioSourceChannelInfo info (&out, 0, out.getNumSamples());
        player.getNextAudioBlock (info);
    }

    juce::MidiBuffer empty;
    for (auto& f : fx)
        if (f != nullptr) f->processBlock (out, empty);
}

Engine::Engine()
{
    audioFormatManager.registerBasicFormats();
    formatManager.addDefaultFormats();
    deviceManager.initialise (2, 2, nullptr, true);
    deviceManager.addAudioCallback (this);
    scanDefaultVst3Folders();
}

Engine::~Engine()
{
    deviceManager.removeAudioCallback (this);
}

void Engine::scanDefaultVst3Folders()
{
    auto folders = vst3Format.getDefaultLocationsToSearch();
    juce::PluginDirectoryScanner scanner (pluginList, vst3Format, folders, true, juce::File());
    juce::String name;
    while (scanner.scanNextFile (true, name))
        juce::Logger::writeToLog ("Scansione: " + name);
}

void Engine::audioDeviceAboutToStart (juce::AudioIODevice* device)
{
    prepareToPlay ((int) device->getCurrentBufferSizeSamples(), device->getCurrentSampleRate());
}

void Engine::audioDeviceStopped()
{
    releaseResources();
}

void Engine::audioDeviceIOCallbackWithContext (const float* const* inputChannelData,
                                               int numInputChannels,
                                               float* const* outputChannelData,
                                               int numOutputChannels,
                                               int numSamples,
                                               const juce::AudioIODeviceCallbackContext&)
{
    if (numOutputChannels == 0 || outputChannelData == nullptr) return;
    juce::AudioBuffer<float> out (outputChannelData, numOutputChannels, 0, numSamples);
    processBlockInternal (out, numSamples);

    juce::ScopedLock sl (recLock);
    if (writer != nullptr && numInputChannels > 0 && inputChannelData != nullptr)
    {
        juce::AudioBuffer<float> in (const_cast<float**> (inputChannelData), numInputChannels, 0, numSamples);
        writer->write (in.getArrayOfReadPointers(), numSamples);
    }
}

void Engine::prepareToPlay (int samplesPerBlockExpected, double newSampleRate)
{
    sampleRate = newSampleRate;
    blockSize  = samplesPerBlockExpected;
    trackBuffer.setSize (2, samplesPerBlockExpected);
    for (auto* t : tracks) t->prepare (sampleRate, samplesPerBlockExpected);
}

void Engine::releaseResources()
{
    for (auto* t : tracks) t->player.releaseResources();
}

void Engine::processBlockInternal (juce::AudioBuffer<float>& out, int numSamples)
{
    out.clear();
    bool anySolo = false;
    for (auto* t : tracks) if (t->solo) { anySolo = true; break; }

    const double blockDur = numSamples / sampleRate;

    for (auto* t : tracks)
    {
        bool audible = (! t->mute) && ((! anySolo) || t->solo);
        trackBuffer.setSize (2, numSamples, false, true, true);
        t->render (trackBuffer, midiScratch, position, sampleRate, numSamples);

        float level = trackBuffer.getRMSLevel (0, 0, numSamples);
        t->lastLevel = audible ? level * t->gain : 0.0f;

        if (! audible || t->gain == 0.0f) continue;

        float lg = t->gain * juce::jmin (1.0f, 1.0f - t->pan);
        float rg = t->gain * juce::jmin (1.0f, 1.0f + t->pan);

        for (int ch = 0; ch < out.getNumChannels(); ++ch)
        {
            float g = (ch == 0 ? lg : rg);
            out.addFrom (ch, 0, trackBuffer, juce::jmin (ch, trackBuffer.getNumChannels() - 1), 0, numSamples, g);
        }
    }

    if (playing)
    {
        position += blockDur;
        if (position >= getLength() && getLength() > 0.0) position = 0.0;
    }
}

void Engine::play()
{
    if (playing) return;
    playing = true;
    for (auto* t : tracks) if (! t->isMidi) t->player.start();
}

void Engine::stop()
{
    playing = false;
    recording = false;
    for (auto* t : tracks) t->player.stop();
    juce::ScopedLock sl (recLock);
    writer.reset();
}

void Engine::toggleRecord()
{
    if (! playing) { play(); recording = true; }
    else recording = ! recording;
}

double Engine::getPosition() const
{
    for (auto* t : tracks)
        if (! t->isMidi && t->readerSource != nullptr)
            return t->player.getCurrentPosition();
    return position;
}

void Engine::setPosition (double sec)
{
    position = sec;
    for (auto* t : tracks) if (! t->isMidi) t->player.setPosition (sec);
}

double Engine::getLength() const
{
    double len = 0.0;
    for (auto* t : tracks)
        if (! t->isMidi && t->readerSource != nullptr)
            len = juce::jmax (len, t->readerSource->getTotalLength() / sampleRate);
        else if (t->isMidi && t->midi.getEndTime() > 0.0)
            len = juce::jmax (len, t->midi.getEndTime());
    return juce::jmax (len, position);
}

Track* Engine::addTrack (const juce::String& name, bool midi)
{
    auto* t = new Track (name, midi);
    t->prepare (sampleRate, blockSize);
    tracks.add (t);
    return t;
}

void Engine::removeTrack (Track* t) { if (t != nullptr) tracks.removeObject (t); }

juce::AudioPluginInstance* Engine::loadInstrumentOnTrack (Track& t, const juce::PluginDescription& desc)
{
    juce::String err;
    auto inst = formatManager.createPluginInstance (desc, sampleRate, blockSize, err);
    if (inst == nullptr) { juce::Logger::writeToLog ("Plugin error: " + err); return nullptr; }
    t.instrument = std::move (inst);
    t.prepare (sampleRate, blockSize);
    return t.instrument.get();
}

juce::AudioPluginInstance* Engine::addFxToTrack (Track& t, const juce::PluginDescription& desc)
{
    juce::String err;
    auto inst = formatManager.createPluginInstance (desc, sampleRate, blockSize, err);
    if (inst == nullptr) { juce::Logger::writeToLog ("Plugin error: " + err); return nullptr; }
    auto* raw = inst.get();
    t.fx.push_back (std::move (inst));
    t.prepare (sampleRate, blockSize);
    return raw;
}

PluginWindow* Engine::windowForPlugin (juce::AudioProcessor& p, juce::String title)
{
    for (auto* w : pluginWindows)
        if (& w->getProcessor() == & p)
        {
            w->setVisible (true);
            w->toFront (true);
            return w;
        }
    auto* w = pluginWindows.add (new PluginWindow (p, title));
    w->toFront (true);
    return w;
}

void Engine::startAudioRecording()
{
    juce::ScopedLock sl (recLock);
    auto dir = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                   .getChildFile ("FBTECH_Recordings");
    dir.createDirectory();
    lastRecording = dir.getChildFile ("take_" + juce::Time::getCurrentTime().formatted ("%Y%m%d_%H%M%S") + ".wav");
    recStream = std::make_unique<juce::FileOutputStream> (lastRecording);
    if (auto* w = wavFormat.createWriterFor (recStream.get(), sampleRate, 2, 16, {}, 0))
    {
        recStream.release();
        writer = std::make_unique<juce::AudioFormatWriter::ThreadedWriter> (w, scanThread, 32768);
    }
}

void Engine::stopAudioRecordingAndAttach (Track& target)
{
    { juce::ScopedLock sl (recLock); writer.reset(); }
    if (lastRecording.existsAsFile())
        target.loadFile (lastRecording, audioFormatManager);
}

void Engine::startMidiRecording()
{
    for (auto* t : tracks) if (t->armed && t->isMidi) t->midi.clear();
}

void Engine::stopMidiRecording()
{
    for (auto* t : tracks)
        if (t->isMidi) t->midi.updateMatchedPairs();
}

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& msg)
{
    if (! recording) return;
    double now = getPosition();
    for (auto* t : tracks)
        if (t->armed && t->isMidi)
        {
            auto m = msg;
            m.setTimeStamp (now + msg.getTimeStamp());
            t->midi.addEvent (m);
        }
}

bool Engine::exportToWav (const juce::File& outFile, std::function<void(double)> progress)
{
    stop();
    setPosition (0.0);

    double len = getLength();
    if (len <= 0.0) return false;

    auto outStream = outFile.createOutputStream();
    if (outStream == nullptr) return false;

    std::unique_ptr<juce::AudioFormatWriter> writerPtr (
        wavFormat.createWriterFor (outStream.get(), sampleRate, 2, 16, {}, 0));
    if (writerPtr == nullptr) return false;
    outStream.release();

    play();
    juce::AudioBuffer<float> buf (2, blockSize);
    double totalBlocks = std::ceil (len * sampleRate / blockSize);
    for (long i = 0; i < (long) totalBlocks; ++i)
    {
        processBlockInternal (buf, blockSize);
        writerPtr->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        if (progress) progress ((double) i / totalBlocks);
        juce::Thread::sleep (1);
    }
    stop();
    setPosition (0.0);
    return true;
}

} // namespace fbd
