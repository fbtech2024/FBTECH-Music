#include "Engine.h"

namespace fbd
{

AudioClip* Track::addClip (const juce::File& f, double startTime,
                           juce::AudioFormatManager& fm,
                           double trim, double len)
{
    auto* reader = fm.createReaderFor (f);
    if (reader == nullptr) return nullptr;

    auto* c = new AudioClip();
    c->file = f;
    c->reader.reset (reader);
    c->startTime = startTime;
    c->trimStart = trim >= 0.0 ? trim : 0.0;
    double fileLen = reader->sampleRate > 0.0
                         ? (double) reader->lengthInSamples / reader->sampleRate
                         : 0.0;
    c->length = len > 0.0 ? len : juce::jmax (0.01, fileLen - c->trimStart);
    fmPtr = &fm;
    return clips.add (c);
}

void Track::removeClip (AudioClip* c)
{
    if (c != nullptr) clips.removeObject (c);
}

void Track::splitClip (AudioClip* c, double atTime)
{
    if (c == nullptr || c->reader == nullptr) return;
    if (atTime <= c->startTime || atTime >= c->startTime + c->length) return;

    double localOffset = atTime - c->startTime;
    double secondTrim  = c->trimStart + localOffset;
    double secondLen   = c->length - localOffset;
    c->length = localOffset;

    auto* second = new AudioClip();
    second->file = c->file;
    second->reader.reset (fmPtr != nullptr ? fmPtr->createReaderFor (second->file) : nullptr);
    second->startTime = atTime;
    second->trimStart = secondTrim;
    second->length = secondLen;
    clips.add (second);
}

void Track::prepare (double sr, int bs)
{
    srStored = sr;
    auto setupProc = [&] (juce::AudioPluginInstance& p)
    {
        p.enableAllBuses();
        p.setPlayConfigDetails (2, 2, sr, bs);
        p.prepareToPlay (sr, bs);
    };
    if (instrument != nullptr) setupProc (*instrument);
    for (auto& f : fx) setupProc (*f);
}

void Track::render (juce::AudioBuffer<float>& out, juce::MidiBuffer& mb, double posSec)
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
        double blockDur = out.getNumSamples() / srStored;
        double blockEnd = posSec + blockDur;
        for (auto* c : clips)
        {
            if (c->reader == nullptr) continue;
            double cEnd = c->startTime + c->length;
            if (blockEnd <= c->startTime || posSec >= cEnd) continue;

            double visStart = juce::jmax (posSec, c->startTime);
            double visEnd   = juce::jmin (blockEnd, cEnd);
            int destStart = juce::roundToInt ((visStart - posSec) * srStored);
            int nSamples  = juce::roundToInt ((visEnd - visStart) * srStored);
            if (nSamples <= 0) continue;

            double fileTime = c->trimStart + (visStart - c->startTime);
            juce::int64 readerPos = juce::roundToInt (fileTime * c->reader->sampleRate);
            nSamples = (int) juce::jmin ((juce::int64) nSamples, c->reader->lengthInSamples - readerPos);
            if (nSamples <= 0) continue;

            c->reader->read (&out, destStart, nSamples, readerPos, true, true);
        }
    }

    juce::MidiBuffer empty;
    for (auto& f : fx)
        if (f != nullptr) f->processBlock (out, empty);
}

double Track::endTime() const
{
    double e = 0.0;
    for (auto* c : clips) e = juce::jmax (e, c->startTime + c->length);
    if (isMidi) e = juce::jmax (e, midi.getEndTime());
    return e;
}

juce::AudioThumbnail* Track::thumbFor (AudioClip* c, juce::AudioFormatManager& fm)
{
    if (c == nullptr || ! c->file.existsAsFile()) return nullptr;
    if (thumbCache == nullptr)
        thumbCache = std::make_unique<juce::AudioThumbnailCache> (16);
    auto key = c->file.getFullPathName();
    if (thumbs[key] == nullptr)
    {
        thumbs[key] = std::make_unique<juce::AudioThumbnail> (256, fm, *thumbCache);
        thumbs[key]->setSource (new juce::FileInputSource (c->file));
    }
    return thumbs[key].get();
}

Engine::Engine()
{
    audioFormatManager.registerBasicFormats();
    formatManager.addDefaultFormats();
    deviceManager.initialise (2, 2, nullptr, true);
    deviceManager.addAudioCallback (this);
}

Engine::~Engine()
{
    if (vstScanThread.joinable()) vstScanThread.join();
    deviceManager.removeAudioCallback (this);
}

AudioClip* Engine::addAudioFileToTrack (Track& t, const juce::File& f,
                                        double startTime, double trim, double len)
{
    if (! t.isMidi)
        return t.addClip (f, startTime, audioFormatManager, trim, len);
    return nullptr;
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

void Engine::releaseResources() {}

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
        t->render (trackBuffer, midiScratch, position);

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
    playing = true;
}

void Engine::stop()
{
    playing = false;
    recording = false;
    juce::ScopedLock sl (recLock);
    writer.reset();
}

void Engine::toggleRecord()
{
    if (! playing) { playing = true; recording = true; }
    else recording = ! recording;
}

double Engine::getLength() const
{
    double len = position;
    for (auto* t : tracks) len = juce::jmax (len, t->endTime());
    return len;
}

Track* Engine::addTrack (const juce::String& name, bool midi)
{
    auto* t = new Track (name, midi);
    t->prepare (sampleRate, blockSize);
    tracks.add (t);
    return t;
}

void Engine::removeTrack (Track* t) { if (t != nullptr) tracks.removeObject (t); }

bool Engine::saveProject (const juce::File& f)
{
    juce::XmlElement root ("FBTECHMUSIC");
    root.setAttribute ("version", "0.5");
    for (auto* t : tracks)
    {
        auto* tx = root.createNewChildElement ("TRACK");
        tx->setAttribute ("name", t->name);
        tx->setAttribute ("midi", t->isMidi);
        tx->setAttribute ("gain", (double) t->gain);
        tx->setAttribute ("pan", (double) t->pan);
        tx->setAttribute ("mute", t->mute);
        tx->setAttribute ("solo", t->solo);
        for (auto* c : t->clips)
        {
            auto* cx = tx->createNewChildElement ("CLIP");
            cx->setAttribute ("file", c->file.getFullPathName());
            cx->setAttribute ("start", c->startTime);
            cx->setAttribute ("trim", c->trimStart);
            cx->setAttribute ("length", c->length);
        }
        for (int i = 0; i < t->midi.getNumEvents(); ++i)
        {
            auto& m = t->midi.getEventPointer (i)->message;
            auto* mx = tx->createNewChildElement ("MIDI");
            mx->setAttribute ("time", m.getTimeStamp());
            mx->setAttribute ("data", juce::Base64::toBase64 (m.getRawData(), (size_t) m.getRawDataSize()));
        }
    }
    return root.writeToFile (f, {});
}

bool Engine::loadProject (const juce::File& f)
{
    auto xml = juce::XmlDocument::parse (f);
    if (xml == nullptr || ! xml->hasTagName ("FBTECHMUSIC")) return false;
    tracks.clear();
    position = 0.0;
    for (int i = 0; i < xml->getNumChildElements(); ++i)
    {
        auto* tx = xml->getChildElement (i);
        if (! tx->hasTagName ("TRACK")) continue;
        auto* t = addTrack (tx->getStringAttribute ("name"),
                            tx->getBoolAttribute ("midi", false));
        t->gain = (float) tx->getDoubleAttribute ("gain", 1.0);
        t->pan  = (float) tx->getDoubleAttribute ("pan", 0.0);
        t->mute = tx->getBoolAttribute ("mute", false);
        t->solo = tx->getBoolAttribute ("solo", false);
        for (int j = 0; j < tx->getNumChildElements(); ++j)
        {
            auto* cx = tx->getChildElement (j);
            if (cx->hasTagName ("CLIP"))
                addAudioFileToTrack (*t, juce::File (cx->getStringAttribute ("file")),
                                     cx->getDoubleAttribute ("start", 0.0),
                                     cx->getDoubleAttribute ("trim", -1.0),
                                     cx->getDoubleAttribute ("length", -1.0));
            else if (cx->hasTagName ("MIDI"))
            {
                juce::MemoryBlock mb;
                juce::MemoryOutputStream mos (mb, false);
                if (juce::Base64::convertFromBase64 (mos, cx->getStringAttribute ("data"))
                        && mb.getSize() > 0)
                {
                    juce::MidiMessage m (mb.getData(), (int) mb.getSize());
                    m.setTimeStamp (cx->getDoubleAttribute ("time", 0.0));
                    t->midi.addEvent (m);
                }
            }
        }
    }
    return true;
}

void Engine::scanVst3Async (std::function<void(int)> onDone)
{
    if (scanning) return;
    scanning = true;
    if (vstScanThread.joinable()) vstScanThread.join();
    vstScanThread = std::thread ([this, onDone]
    {
        juce::KnownPluginList local;
        juce::VST3PluginFormat fmt;
        juce::PluginDirectoryScanner scanner (local, fmt, fmt.getDefaultLocationsToSearch(), true, juce::File());
        juce::String nm;
        while (scanner.scanNextFile (true, nm)) {}

        juce::Array<juce::PluginDescription> found;
        for (auto* d : local.getTypes())
            found.add (*d);

        juce::MessageManager::callAsync ([this, found, onDone]
        {
            pluginList.clear();
            for (auto& d : found)
                pluginList.addType (d);
            scanning = false;
            if (onDone) onDone (found.size());
        });
    });
}

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
    recStart = position;
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
        addAudioFileToTrack (target, lastRecording, recStart);
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
    double now = position;
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

    playing = true;
    juce::AudioBuffer<float> buf (2, blockSize);
    double totalBlocks = std::ceil (len * sampleRate / blockSize);
    for (long i = 0; i < (long) totalBlocks; ++i)
    {
        processBlockInternal (buf, blockSize);
        writerPtr->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
        if (progress) progress ((double) i / totalBlocks);
        juce::Thread::sleep (1);
    }
    playing = false;
    setPosition (0.0);
    return true;
}

} // namespace fbd
