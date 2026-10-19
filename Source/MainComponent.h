#pragma once
#include "Engine.h"
#include "Theme.h"

namespace fbd
{

class MixerStrip;

class MainComponent : public juce::Component,
                      public juce::Timer,
                      public juce::Button::Listener,
                      public juce::ComboBox::Listener
{
public:
    MainComponent();
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    void timerCallback() override;
    void buttonClicked (juce::Button*) override;
    void comboBoxChanged (juce::ComboBox*) override;

    void showPluginMenu (Track&, bool forInstrument);

private:
    Engine engine;
    FBLookAndFeel laf;

    juce::TextButton btnRec { "REC" }, btnPlay { "PLAY" }, btnStop { "STOP" },
                     btnRew { "|<<" }, btnAddAudio { "+ AUDIO" }, btnAddMidi { "+ MIDI" },
                     btnExport { "EXPORT" };
    juce::Label lblPos, lblStatus;

    struct TrackLane : juce::Component
    {
        TrackLane (MainComponent& o, Track* t) : owner (o), track (t)
        {
            setInterceptsMouseClicks (false, false);
        }
        void paint (juce::Graphics& g) override
        {
            auto r = getLocalBounds();
            g.fillAll (track->armed ? Neon::pink.withAlpha (0.09f) : Neon::bg1);
            g.setColour (Neon::panel);
            g.drawRect (r);

            auto head = r.removeFromLeft (150);
            g.setColour (Neon::bg0);
            g.fillRect (head);
            g.setColour (Neon::text);
            g.setFont (14.0f);
            g.drawText (track->name, head.reduced (8, 0), juce::Justification::centredLeft);
            g.setFont (11.0f);
            g.setColour (track->isMidi ? Neon::pink.withAlpha (0.9f) : Neon::cyan.withAlpha (0.9f));
            g.drawText (track->isMidi ? "MIDI" : "AUDIO",
                        head.reduced (8, 0).withTrimmedTop (20),
                        juce::Justification::centredLeft);
            g.setColour (Neon::text.withAlpha (0.4f));
            juce::String flags;
            if (track->mute) flags << "M ";
            if (track->solo) flags << "S ";
            if (track->armed) flags << "REC";
            g.drawText (flags, head.reduced (8, 0).withTrimmedTop (36),
                        juce::Justification::centredLeft);

            auto area = r.reduced (4).withTrimmedTop (8);
            if (! track->isMidi && track->readerSource != nullptr)
            {
                g.setColour (Neon::cyan.withAlpha (0.85f));
                track->thumb().drawChannels (g, area, 0.0, track->fileLength(), 1.0f);
            }
            else if (track->isMidi)
            {
                for (int i = 0; i < track->midi.getNumEvents(); ++i)
                {
                    auto* ev = track->midi.getEventPointer (i);
                    auto& msg = ev->message;
                    if (! msg.isNoteOn()) continue;
                    float x = area.getX() + (float) (msg.getTimeStamp() / 60.0) * owner.pxPerSec;
                    if (x > area.getRight()) break;
                    int note = msg.getNoteNumber();
                    float y = area.getY() + area.getHeight() * (1.0f - (float) note / 127.0f);
                    g.setColour (Neon::pink.withAlpha (0.5f + 0.5f * msg.getFloatVelocity()));
                    g.fillRect (x, y, 4.0f, juce::jmax (3.0f, area.getHeight() * 0.03f));
                }
            }
        }
        MainComponent& owner;
        Track* track;
    };

    juce::Viewport arrangeView;
    juce::Component arrangeContent;
    juce::OwnedArray<TrackLane> lanes;
    std::vector<std::unique_ptr<MixerStrip>> strips;
    juce::Viewport mixerView;
    juce::Component mixerContent;

    juce::ComboBox midiInputList;
    double pxPerSec = 100.0;

    void rebuildViews();
    juce::File lastAudioDir;

    JUCE_DECLARE_NON_COPYABLE (MainComponent)
};

class MixerStrip : public juce::Component,
                   public juce::Slider::Listener,
                   public juce::Button::Listener
{
public:
    MixerStrip (MainComponent& o, Track* t) : owner (o), track (t)
    {
        addAndMakeVisible (slider);
        slider.setSliderStyle (juce::Slider::LinearVertical);
        slider.setRange (0.0, 2.0, 0.01);
        slider.setValue (1.0);
        slider.addListener (this);

        addAndMakeVisible (pan);
        pan.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        pan.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
        pan.setRange (-1.0, 1.0, 0.01);
        pan.addListener (this);

        addAndMakeVisible (btnMute); btnMute.setButtonText ("M"); btnMute.setClickingTogglesState (true); btnMute.addListener (this);
        addAndMakeVisible (btnSolo); btnSolo.setButtonText ("S"); btnSolo.setClickingTogglesState (true); btnSolo.addListener (this);
        addAndMakeVisible (btnRec);  btnRec.setButtonText ("O"); btnRec.setClickingTogglesState (true); btnRec.addListener (this);
        addAndMakeVisible (btnInst); btnInst.setButtonText ("VSTi"); btnInst.addListener (this);
        addAndMakeVisible (btnFx);   btnFx.setButtonText ("FX");   btnFx.addListener (this);

        addAndMakeVisible (lblName); lblName.setText (track->name, juce::dontSendNotification);
        lblName.setJustificationType (juce::Justification::centred);
        lblName.setFont (juce::Font (13.0f, juce::Font::bold));
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Neon::bg1);
        g.setColour (Neon::panel);
        g.drawRect (getLocalBounds());

        auto meterArea = getLocalBounds().reduced (4).removeFromRight (9);
        g.setColour (Neon::bg0);
        g.fillRect (meterArea);
        float lvl = juce::jlimit (0.0f, 1.0f, track->lastLevel);
        auto filled = meterArea.withTrimmedTop ((float) meterArea.getHeight() * (1.0f - lvl));
        g.setColour (lvl > 0.85f ? Neon::pink : (lvl > 0.6f ? Neon::yellow : Neon::green));
        g.fillRect (filled);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (4);
        r.removeFromRight (12);
        lblName.setBounds (r.removeFromTop (20));
        auto row = r.removeFromBottom (22);
        btnMute.setBounds (row.removeFromLeft (row.getWidth() / 3));
        btnSolo.setBounds (row.removeFromLeft (row.getWidth() / 2));
        btnRec.setBounds (row);
        auto row2 = r.removeFromBottom (22);
        btnInst.setBounds (row2.removeFromLeft (row2.getWidth() / 2));
        btnFx.setBounds (row2);
        pan.setBounds (r.removeFromBottom (50).reduced (2));
        slider.setBounds (r);
    }

    void sliderValueChanged (juce::Slider* s) override
    {
        if (s == &slider) track->gain = (float) s->getValue();
        if (s == &pan)    track->pan  = (float) s->getValue();
    }

    void buttonClicked (juce::Button* bt) override
    {
        if (bt == &btnMute) track->mute = bt->getToggleState();
        if (bt == &btnSolo) track->solo = bt->getToggleState();
        if (bt == &btnRec)  track->armed = bt->getToggleState();
        if (bt == &btnInst) owner.showPluginMenu (*track, true);
        if (bt == &btnFx)   owner.showPluginMenu (*track, false);
    }

private:
    MainComponent& owner;
    Track* track;
    juce::Slider slider, pan;
    juce::TextButton btnMute, btnSolo, btnRec, btnInst, btnFx;
    juce::Label lblName;
};

} // namespace fbd
