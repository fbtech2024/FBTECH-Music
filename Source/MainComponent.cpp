#include "MainComponent.h"

namespace fbd
{

MainComponent::MainComponent()
{
    setLookAndFeel (&laf);

    engine.addTrack ("Audio 1", false);
    engine.addTrack ("MIDI 1", true);

    btnRec.setColour (juce::TextButton::textColourOffId, Neon::pink);
    btnPlay.setColour (juce::TextButton::textColourOffId, Neon::green);
    btnStop.setColour (juce::TextButton::textColourOffId, Neon::yellow);
    btnRew.setColour (juce::TextButton::textColourOffId, Neon::cyan);

    for (auto* bt : { &btnRec, &btnPlay, &btnStop, &btnRew, &btnAddAudio, &btnAddMidi, &btnExport })
    {
        addAndMakeVisible (*bt);
        bt->addListener (this);
    }

    addAndMakeVisible (lblPos);
    lblPos.setFont (juce::Font (juce::Font::getDefaultMonospacedFontName(), 28.0f, juce::Font::plain));
    lblPos.setColour (juce::Label::textColourId, Neon::cyan);
    addAndMakeVisible (lblStatus);

    addAndMakeVisible (midiInputList);
    midiInputList.addListener (this);
    auto inputs = juce::MidiInput::getAvailableDevices();
    int id = 1;
    for (auto& d : inputs)
        midiInputList.addItem (d.name, id++);
    if (inputs.size() > 0)
        midiInputList.setSelectedItemIndex (0);

    addAndMakeVisible (arrangeView);
    arrangeView.setViewedComponent (&arrangeContent, false);
    addAndMakeVisible (mixerView);
    mixerView.setViewedComponent (&mixerContent, false);

    rebuildViews();
    startTimerHz (30);
    if (inputs.size() > 0)
        comboBoxChanged (&midiInputList);
    setSize (1280, 800);
}

MainComponent::~MainComponent()
{
    stopTimer();
    setLookAndFeel (nullptr);
    int idx = midiInputList.getSelectedItemIndex();
    auto inputs = juce::MidiInput::getAvailableDevices();
    if (idx >= 0 && idx < inputs.size())
        engine.deviceManager.removeMidiInputDeviceCallback (inputs[idx].identifier, &engine);
}

void MainComponent::comboBoxChanged (juce::ComboBox*)
{
    int idx = midiInputList.getSelectedItemIndex();
    auto inputs = juce::MidiInput::getAvailableDevices();
    if (idx < 0 || idx >= inputs.size()) return;
    engine.deviceManager.removeMidiInputDeviceCallback (inputs[idx].identifier, &engine);
    engine.deviceManager.setMidiInputEnabled (inputs[idx].identifier, true);
    engine.deviceManager.addMidiInputDeviceCallback (inputs[idx].identifier, &engine);
    lblStatus.setText ("Input MIDI: " + inputs[idx].name, juce::dontSendNotification);
}

void MainComponent::showPluginMenu (Track& t, bool forInstrument)
{
    juce::PopupMenu m;
    auto& list = engine.getPluginList();
    int i = 1;
    for (auto& desc : list.getTypes())
    {
        juce::PopupMenu::Item item;
        item.itemID = i++;
        item.text = desc.name + "  (" + desc.pluginFormatName + ")";
        item.isEnabled = true;
        item.isTicked = false;
        item.action = [this, &t, desc, forInstrument]
        {
            auto* inst = forInstrument
                ? engine.loadInstrumentOnTrack (t, desc)
                : engine.addFxToTrack (t, desc);
            if (inst != nullptr)
                engine.windowForPlugin (*inst, desc.name);
            else
                lblStatus.setText ("Errore caricamento plugin", juce::dontSendNotification);
        };
        m.addItem (item);
    }
    if (list.getTypes().isEmpty())
    {
        juce::PopupMenu::Item none;
        none.itemID = 1;
        none.text = "Nessun VST3 trovato in C:/Program Files/Common Files/VST3";
        none.isEnabled = false;
        m.addItem (none);
    }
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this));
}

void MainComponent::rebuildViews()
{
    arrangeContent.removeAllChildren();
    lanes.clear();
    int y = 0;
    for (auto* t : engine.getTracks())
    {
        auto* lane = lanes.add (new TrackLane (*this, t));
        lane->setBounds (0, y, 8000, 90);
        arrangeContent.addAndMakeVisible (lane);
        y += 90;
    }
    arrangeContent.setSize (8000, y);

    strips.clear();
    mixerContent.removeAllChildren();
    int x = 0;
    for (auto* t : engine.getTracks())
    {
        auto strip = std::make_unique<MixerStrip> (*this, t);
        mixerContent.addAndMakeVisible (strip.get());
        strip->setBounds (x, 0, 90, 330);
        strips.push_back (std::move (strip));
        x += 95;
    }
    mixerContent.setSize (x, 330);
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (Neon::bg0);
    double pos = engine.getPosition();
    float x = (float) (pos * pxPerSec) - (float) arrangeView.getViewPositionX();
    g.setColour (Neon::pink);
    g.drawLine (x, 40.0f, x, (float) arrangeView.getBottom(), 2.0f);
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (6);
    auto top = r.removeFromTop (40);
    for (auto* bt : { &btnRec, &btnPlay, &btnStop, &btnRew, &btnAddAudio, &btnAddMidi })
        bt->setBounds (top.removeFromLeft (88).reduced (2));
    btnExport.setBounds (top.removeFromLeft (100).reduced (2));
    midiInputList.setBounds (top.removeFromLeft (230).reduced (2));
    lblPos.setBounds (top.removeFromLeft (170));
    auto bottom = r.removeFromBottom (22);
    lblStatus.setBounds (bottom);

    auto mixH = juce::jmin (350, r.getHeight() / 2);
    mixerView.setBounds (r.removeFromBottom (mixH));
    arrangeView.setBounds (r);
}

void MainComponent::timerCallback()
{
    double pos = engine.getPosition();
    int minutes = (int) (pos / 60.0);
    double seconds = pos - minutes * 60;
    juce::String txt;
    txt << juce::String (minutes).paddedLeft ('0', 2)
        << ":"
        << juce::String (seconds, 2).paddedLeft ('0', 5);
    lblPos.setText (txt, juce::dontSendNotification);
    arrangeView.setViewPosition (juce::jmax (0, (int) (pos * pxPerSec) - arrangeView.getMaximumVisibleWidth() / 3),
                                 arrangeView.getViewPositionY());
    repaint();
}

void MainComponent::buttonClicked (juce::Button* bt)
{
    if (bt == &btnPlay)
    {
        engine.play();
    }
    else if (bt == &btnStop)
    {
        bool wasRec = engine.isRecording();
        engine.stop();
        if (wasRec)
        {
            for (auto* t : engine.getTracks())
                if (t->armed && ! t->isMidi)
                    engine.stopAudioRecordingAndAttach (*t);
            engine.stopMidiRecording();
            rebuildViews();
            lblStatus.setText ("Registrazione salvata.", juce::dontSendNotification);
        }
    }
    else if (bt == &btnRec)
    {
        if (engine.isRecording())
        {
            engine.toggleRecord();
            engine.stop();
            for (auto* t : engine.getTracks())
                if (t->armed && ! t->isMidi)
                    engine.stopAudioRecordingAndAttach (*t);
            engine.stopMidiRecording();
            rebuildViews();
            lblStatus.setText ("Registrazione salvata.", juce::dontSendNotification);
            return;
        }
        bool anyArmed = false, armedMidi = false;
        for (auto* t : engine.getTracks())
            if (t->armed) { anyArmed = true; if (t->isMidi) armedMidi = true; }
        if (! anyArmed)
        {
            lblStatus.setText ("Arma una traccia (pulsante O) prima di registrare.", juce::dontSendNotification);
            return;
        }
        for (auto* t : engine.getTracks())
            if (t->armed && ! t->isMidi) engine.startAudioRecording();
        if (armedMidi) engine.startMidiRecording();
        engine.toggleRecord();
        lblStatus.setText ("Registrazione...", juce::dontSendNotification);
    }
    else if (bt == &btnRew)
    {
        engine.stop();
        engine.setPosition (0.0);
    }
    else if (bt == &btnAddAudio)
    {
        auto* t = engine.addTrack ("Audio " + juce::String (engine.getTracks().size() + 1), false);
        rebuildViews();
        auto fc = std::make_shared<juce::FileChooser> (
            "Apri file audio", lastAudioDir, "*.wav;*.aiff;*.mp3;*.flac");
        fc->launchAsync (juce::FileBrowserComponent::openMode
                             | juce::FileBrowserComponent::canSelectFiles,
                         [this, t, fc] (const juce::FileChooser& fch)
                         {
                             auto f = fch.getResult();
                             if (! f.existsAsFile()) return;
                             lastAudioDir = f.getParentDirectory();
                             engine.loadFileOnTrack (*t, f);
                             arrangeContent.repaint();
                         });
    }
    else if (bt == &btnAddMidi)
    {
        engine.addTrack ("MIDI " + juce::String (engine.getTracks().size() + 1), true);
        rebuildViews();
    }
    else if (bt == &btnExport)
    {
        engine.stop();
        auto fc = std::make_shared<juce::FileChooser> (
            "Esporta mix WAV", juce::File::getSpecialLocation (juce::File::userDesktopDirectory), "*.wav");
        fc->launchAsync (juce::FileBrowserComponent::saveMode
                             | juce::FileBrowserComponent::warnAboutOverwriting,
                         [this, fc] (const juce::FileChooser& fch)
                         {
                             auto out = fch.getResult();
                             if (out == juce::File()) return;
                             out = out.withFileExtension ("wav");
                             if (engine.exportToWav (out, [] (double) {}))
                                 lblStatus.setText ("Mix esportato: " + out.getFullPathName(), juce::dontSendNotification);
                             else
                                 lblStatus.setText ("Export fallito: progetto vuoto?", juce::dontSendNotification);
                         });
    }
}

} // namespace fbd
