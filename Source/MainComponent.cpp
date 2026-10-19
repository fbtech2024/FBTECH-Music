#include "MainComponent.h"

namespace fbd
{

enum
{
    mImport = 10, mSave = 11, mOpen = 12, mExport = 13, mQuit = 14,
    mAddAudio = 20, mAddMidi = 21, mRename = 22, mDeleteTrack = 23, mSplit = 24,
    mScanVst = 30, mAbout = 40
};

MainComponent::MainComponent()
{
    setLookAndFeel (&laf);
    setWantsKeyboardFocus (true);

    engine.addTrack ("Audio 1", false);
    engine.addTrack ("MIDI 1", true);
    selectedTrack = engine.getTracks()[0];

    addAndMakeVisible (menuBar);
    menuBar.setModel (this);

    btnRec.setColour (juce::TextButton::textColourOffId, Neon::pink);
    btnPlay.setColour (juce::TextButton::textColourOffId, Neon::green);
    btnStop.setColour (juce::TextButton::textColourOffId, Neon::yellow);
    btnRew.setColour (juce::TextButton::textColourOffId, Neon::cyan);

    for (auto* bt : { &btnRec, &btnPlay, &btnStop, &btnRew, &btnAddAudio, &btnAddMidi, &btnExport })
    {
        addAndMakeVisible (*bt);
        bt->addListener (this);
        bt->setWantsKeyboardFocus (false);
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
    menuBar.setModel (nullptr);
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

juce::PopupMenu MainComponent::getMenuForIndex (int idx, const juce::String&)
{
    juce::PopupMenu m;
    if (idx == 0)
    {
        m.addItem (mImport, "Importa audio sulla traccia selezionata");
        m.addSeparator();
        m.addItem (mSave, "Salva progetto");
        m.addItem (mOpen, "Apri progetto");
        m.addSeparator();
        m.addItem (mExport, "Esporta mix WAV");
        m.addSeparator();
        m.addItem (mQuit, "Esci");
    }
    else if (idx == 1)
    {
        m.addItem (mAddAudio, "Aggiungi traccia audio");
        m.addItem (mAddMidi, "Aggiungi traccia MIDI");
        m.addSeparator();
        m.addItem (mRename, "Rinomina traccia selezionata", selectedTrack != nullptr);
        m.addItem (mDeleteTrack, "Elimina traccia selezionata", selectedTrack != nullptr);
        m.addSeparator();
        m.addItem (mSplit, "Dividi clip alla posizione (S)");
    }
    else if (idx == 2)
    {
        m.addItem (mScanVst, "Scansiona cartella VST3", ! engine.isScanning());
    }
    else
    {
        m.addItem (mAbout, "Informazioni su FBTECH Music");
    }
    return m;
}

void MainComponent::menuItemSelected (int id, int)
{
    switch (id)
    {
        case mImport:   importAudio(); break;
        case mSave:     saveProject(); break;
        case mOpen:     openProject(); break;
        case mExport:   btnExport.triggerClick(); break;
        case mQuit:     juce::JUCEApplication::quit(); break;
        case mAddAudio:
            engine.addTrack ("Audio " + juce::String (engine.getTracks().size() + 1), false);
            rebuildViews(); break;
        case mAddMidi:
            engine.addTrack ("MIDI " + juce::String (engine.getTracks().size() + 1), true);
            rebuildViews(); break;
        case mRename:
        {
            if (selectedTrack == nullptr) break;
            auto* t = selectedTrack;
            auto* raw = new juce::AlertWindow ("Rinomina traccia", "Nuovo nome:",
                                               juce::MessageBoxIconType::NoIcon);
            raw->addTextEditor ("name", t->name);
            raw->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
            raw->addButton ("Annulla", 0, juce::KeyPress (juce::KeyPress::escapeKey));
            raw->enterModalState (true, juce::ModalCallbackFunction::create ([this, t, raw] (int r)
                {
                    if (r == 1 && raw->getTextEditorContents ("name").isNotEmpty())
                    {
                        t->name = raw->getTextEditorContents ("name");
                        rebuildViews();
                    }
                }));
            break;
        }
        case mDeleteTrack:
            if (selectedTrack != nullptr)
            {
                engine.removeTrack (selectedTrack);
                selectedTrack = engine.getTracks().size() > 0 ? engine.getTracks().getLast() : nullptr;
                rebuildViews();
            }
            break;
        case mSplit: splitSelectedClip(); break;
        case mScanVst:
            lblStatus.setText ("Scansione VST3 in corso...", juce::dontSendNotification);
            engine.scanVst3Async ([this] (int n)
            {
                lblStatus.setText ("VST3 trovati: " + juce::String (n)
                                       + " — usa i tasti VSTi/FX sulle strisce del mixer",
                                   juce::dontSendNotification);
            });
            break;
        case mAbout:
            juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon,
                "FBTECH Music 0.5",
                "DAW per Windows in C++/JUCE\nTema grafico FBTECH neon\nVST3, MIDI, multitraccia, export WAV");
            break;
    }
}

void MainComponent::importAudio()
{
    auto fc = std::make_shared<juce::FileChooser> (
        "Importa audio", lastAudioDir, "*.wav;*.aiff;*.mp3;*.flac");
    fc->launchAsync (juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles,
                     [this, fc] (const juce::FileChooser& fch)
                     {
                         auto f = fch.getResult();
                         if (! f.existsAsFile()) return;
                         lastAudioDir = f.getParentDirectory();
                         Track* t = selectedTrack;
                         if (t == nullptr || t->isMidi)
                         {
                             t = engine.addTrack ("Audio " + juce::String (engine.getTracks().size() + 1), false);
                             rebuildViews();
                         }
                         engine.addAudioFileToTrack (*t, f, engine.getPosition());
                         selectedTrack = t;
                         arrangeContent.repaint();
                         lblStatus.setText ("Importato: " + f.getFileName(), juce::dontSendNotification);
                     });
}

void MainComponent::saveProject()
{
    auto fc = std::make_shared<juce::FileChooser> (
        "Salva progetto", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                            .getChildFile ("progetto.fbmusic"),
        "*.fbmusic");
    fc->launchAsync (juce::FileBrowserComponent::saveMode
                         | juce::FileBrowserComponent::warnAboutOverwriting,
                     [this, fc] (const juce::FileChooser& fch)
                     {
                         auto out = fch.getResult().withFileExtension ("fbmusic");
                         if (out == juce::File()) return;
                         bool ok = engine.saveProject (out);
                         lblStatus.setText (ok ? "Progetto salvato: " + out.getFullPathName()
                                               : "ERRORE nel salvataggio",
                                            juce::dontSendNotification);
                     });
}

void MainComponent::openProject()
{
    auto fc = std::make_shared<juce::FileChooser> (
        "Apri progetto", juce::File::getSpecialLocation (juce::File::userDocumentsDirectory), "*.fbmusic");
    fc->launchAsync (juce::FileBrowserComponent::openMode
                         | juce::FileBrowserComponent::canSelectFiles,
                     [this, fc] (const juce::FileChooser& fch)
                     {
                         auto f = fch.getResult();
                         if (! f.existsAsFile()) return;
                         if (engine.loadProject (f))
                         {
                             selectedTrack = engine.getTracks().size() > 0 ? engine.getTracks()[0] : nullptr;
                             rebuildViews();
                             lblStatus.setText ("Progetto caricato: " + f.getFileName(),
                                                juce::dontSendNotification);
                         }
                         else
                             lblStatus.setText ("ERRORE: file progetto non valido",
                                                juce::dontSendNotification);
                     });
}

void MainComponent::splitSelectedClip()
{
    for (auto* t : engine.getTracks())
        for (auto* c : t->clips)
            if (c->selected)
            {
                t->splitClip (c, engine.getPosition());
                arrangeContent.repaint();
                lblStatus.setText ("Clip divisa", juce::dontSendNotification);
                return;
            }
}

bool MainComponent::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::spaceKey)
    {
        if (engine.isPlaying()) engine.stop(); else engine.play();
        return true;
    }
    if (k.getTextCharacter() == 's')
    {
        splitSelectedClip();
        return true;
    }
    if (k == juce::KeyPress::deleteKey || k == juce::KeyPress::backspaceKey)
    {
        for (auto* t : engine.getTracks())
            for (int i = t->clips.size(); --i >= 0;)
                if (t->clips[i]->selected)
                    t->removeClip (t->clips[i]);
        arrangeContent.repaint();
        return true;
    }
    return false;
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
        none.text = "Nessun VST3 — usa il menu VST > Scansiona cartella VST3";
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
    g.drawLine (x, 64.0f, x, (float) arrangeView.getBottom(), 2.0f);
}

void MainComponent::resized()
{
    auto r = getLocalBounds().reduced (6);
    menuBar.setBounds (r.removeFromTop (24));
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
        << ":" << juce::String (seconds, 2).paddedLeft ('0', 5);
    lblPos.setText (txt, juce::dontSendNotification);
    arrangeView.setViewPosition (juce::jmax (0, (int) (pos * pxPerSec) - arrangeView.getMaximumVisibleWidth() / 3),
                                 arrangeView.getViewPositionY());
    repaint();
}

void MainComponent::buttonClicked (juce::Button* bt)
{
    if (bt == &btnPlay)
        engine.play();
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
        selectedTrack = t;
        rebuildViews();
        importAudio();
    }
    else if (bt == &btnAddMidi)
    {
        auto* t = engine.addTrack ("MIDI " + juce::String (engine.getTracks().size() + 1), true);
        selectedTrack = t;
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
                             lblStatus.setText ("Esportazione in corso...", juce::dontSendNotification);
                             if (engine.exportToWav (out, [] (double) {}))
                                 lblStatus.setText ("Mix esportato: " + out.getFullPathName(), juce::dontSendNotification);
                             else
                                 lblStatus.setText ("Export fallito: progetto vuoto?", juce::dontSendNotification);
                         });
    }
}

//==============================================================================
void MainComponent::TrackLane::paint (juce::Graphics& g)
{
    auto r = getLocalBounds();
    bool sel = owner.getSelectedTrack() == track;
    g.fillAll (track->armed ? Neon::pink.withAlpha (0.09f) : Neon::bg1);
    g.setColour (sel ? Neon::cyan.withAlpha (0.8f) : Neon::panel);
    g.drawRect (r);

    auto head = r.removeFromLeft (150);
    g.setColour (sel ? Neon::cyan.withAlpha (0.15f) : Neon::bg0);
    g.fillRect (head);
    g.setColour (Neon::text);
    g.setFont (14.0f);
    g.drawText (track->name, head.reduced (8, 0), juce::Justification::centredLeft);
    g.setFont (11.0f);
    g.setColour (track->isMidi ? Neon::pink.withAlpha (0.9f) : Neon::cyan.withAlpha (0.9f));
    g.drawText (track->isMidi ? "MIDI" : "AUDIO",
                head.reduced (8, 0).withTrimmedTop (20), juce::Justification::centredLeft);
    g.setColour (Neon::text.withAlpha (0.4f));
    juce::String flags;
    if (track->mute) flags << "M ";
    if (track->solo) flags << "S ";
    if (track->armed) flags << "REC";
    g.drawText (flags, head.reduced (8, 0).withTrimmedTop (36), juce::Justification::centredLeft);

    auto area = r.reduced (4).withTrimmedTop (8);
    if (! track->isMidi)
    {
        for (auto* c : track->clips)
        {
            float x = 150.0f + (float) (c->startTime * owner.pxPerSec)
                      - (float) owner.arrangeView.getViewPositionX();
            float w = juce::jmax (4.0f, (float) (c->length * owner.pxPerSec));
            auto clipArea = juce::Rectangle<float> (x, (float) area.getY(), w, (float) area.getHeight());

            g.setColour (Neon::cyan.withAlpha (c->selected ? 0.35f : 0.18f));
            g.fillRoundedRectangle (clipArea, 4.0f);

            if (auto* th = track->thumbFor (c, owner.engine.getAudioFormatManager()))
            {
                g.setColour (Neon::cyan.withAlpha (0.9f));
                th->drawChannels (g, clipArea.reduced (2.0f).toNearestInt(),
                                  c->trimStart, c->trimStart + c->length, 1.0f);
            }

            g.setColour (c->selected ? Neon::pink : Neon::cyan.withAlpha (0.6f));
            g.drawRoundedRectangle (clipArea, 4.0f, c->selected ? 2.0f : 1.0f);

            g.setColour (Neon::text);
            g.setFont (11.0f);
            g.drawText (c->file.getFileName(),
                        clipArea.reduced (4.0f, 1.0f).toNearestInt(),
                        juce::Justification::centredLeft);
        }
    }
    else
    {
        for (int i = 0; i < track->midi.getNumEvents(); ++i)
        {
            auto* ev = track->midi.getEventPointer (i);
            auto& msg = ev->message;
            if (! msg.isNoteOn()) continue;
            float x = 150.0f + (float) (msg.getTimeStamp() * owner.pxPerSec)
                      - (float) owner.arrangeView.getViewPositionX();
            if (x > area.getRight()) break;
            int note = msg.getNoteNumber();
            float y = (float) area.getY() + (float) area.getHeight() * (1.0f - (float) note / 127.0f);
            g.setColour (Neon::pink.withAlpha (0.5f + 0.5f * msg.getFloatVelocity()));
            g.fillRect (x, y, 4.0f, juce::jmax (3.0f, (float) area.getHeight() * 0.03f));
        }
    }
}

AudioClip* MainComponent::TrackLane::clipAt (double t)
{
    for (auto* c : track->clips)
        if (t >= c->startTime && t < c->startTime + c->length)
            return c;
    return nullptr;
}

void MainComponent::TrackLane::mouseDown (const juce::MouseEvent& e)
{
    owner.selectTrack (track);
    double t = timeAt (e.x);
    if (auto* c = clipAt (t))
    {
        for (auto* tr : owner.engine.getTracks())
            for (auto* cl : tr->clips)
                cl->selected = (cl == c);
        owner.dragOffset = t - c->startTime;
    }
    else
    {
        for (auto* tr : owner.engine.getTracks())
            for (auto* cl : tr->clips)
                cl->selected = false;
        owner.engine.setPosition (t);
    }
    owner.arrangeContent.repaint();
}

void MainComponent::TrackLane::mouseDrag (const juce::MouseEvent& e)
{
    for (auto* c : track->clips)
        if (c->selected)
        {
            c->startTime = juce::jmax (0.0, timeAt (e.x) - owner.dragOffset);
            owner.arrangeContent.repaint();
            break;
        }
}

void MainComponent::TrackLane::mouseDoubleClick (const juce::MouseEvent& e)
{
    double t = timeAt (e.x);
    if (auto* c = clipAt (t))
    {
        track->removeClip (c);
        owner.arrangeContent.repaint();
    }
}

} // namespace fbd
