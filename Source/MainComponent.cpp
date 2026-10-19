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
            }), true);
