#include "PresetManager.h"
#include "Fonts.h"
#include "BinaryData.h"

// ==============================================================================
// PresetManager
// ==============================================================================

PresetManager::PresetManager(juce::AudioProcessorValueTreeState& apvtsRef)
    : apvts(apvtsRef)
{
    getPresetsFolder().createDirectory();
    installFactoryPresets();
}

void PresetManager::installFactoryPresets() const
{
    // The factory presets are compiled into the binary, so they arrive with the
    // plug-in itself rather than depending on the installer - which also means
    // they work identically on Windows, and survive a user moving the plug-in.
    auto folder = getPresetsFolder();

    for (int i = 0; i < BinaryData::namedResourceListSize; ++i)
    {
        const juce::String originalName (BinaryData::originalFilenames[i]);

        if (! originalName.endsWithIgnoreCase(".wxpreset"))
            continue;

        auto destination = folder.getChildFile(originalName);

        // Never clobber what is already there: the user may have edited a
        // factory preset, or deliberately deleted one.
        if (destination.existsAsFile())
            continue;

        int dataSize = 0;
        if (auto* data = BinaryData::getNamedResource(BinaryData::namedResourceList[i], dataSize))
            if (dataSize > 0)
                destination.replaceWithData(data, (size_t) dataSize);
    }
}

juce::File PresetManager::getPresetsFolder() const
{
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
               .getChildFile("WX Presets");
}

juce::StringArray PresetManager::getPresetList() const
{
    juce::StringArray list;
    auto files = getPresetsFolder().findChildFiles(juce::File::findFiles, false, "*.wxpreset");
    files.sort();
    for (auto& f : files)
        list.add(f.getFileNameWithoutExtension());
    return list;
}

void PresetManager::savePreset(const juce::String& name)
{
    if (name.isEmpty()) return;
    auto xml = apvts.copyState().createXml();
    if (xml == nullptr) return;
    xml->writeTo(getPresetsFolder().getChildFile(name + ".wxpreset"));
    currentPresetName = name;
}

void PresetManager::loadPreset(const juce::String& name)
{
    auto file = getPresetsFolder().getChildFile(name + ".wxpreset");
    if (!file.existsAsFile()) return;
    auto xml = juce::XmlDocument::parse(file);
    if (xml == nullptr) return;
    if (xml->hasTagName(apvts.state.getType()))
    {
        apvts.replaceState(juce::ValueTree::fromXml(*xml));
        currentPresetName = name;
    }
}

void PresetManager::deletePreset(const juce::String& name)
{
    auto file = getPresetsFolder().getChildFile(name + ".wxpreset");
    if (file.existsAsFile())
        file.deleteFile();
}

// ==============================================================================
// PresetPanel
// ==============================================================================

PresetPanel::PresetPanel(PresetManager& pm)
    : presetManager(pm)
{
    presetNameLabel.setText(presetManager.getCurrentPresetName(), juce::dontSendNotification);
    presetNameLabel.setFont(WXFonts::get(16.0f, juce::Font::bold));
    presetNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
    presetNameLabel.setJustificationType(juce::Justification::centred);
    presetNameLabel.setInterceptsMouseClicks(true, false);
    presetNameLabel.addMouseListener(this, false);
    addAndMakeVisible(presetNameLabel);
    
    addAndMakeVisible(prevBtn);
    addAndMakeVisible(nextBtn);
    
    prevBtn.onClick = [this]
    {
        presetList = presetManager.getPresetList();
        if (presetList.isEmpty()) return;
        int idx = presetList.indexOf(presetManager.getCurrentPresetName());
        currentIndex = (idx >= 0) ? idx : 0;
        currentIndex = juce::jmax(0, currentIndex - 1);
        loadPresetByIndex(currentIndex);
    };
    
    nextBtn.onClick = [this]
    {
        presetList = presetManager.getPresetList();
        if (presetList.isEmpty()) return;
        int idx = presetList.indexOf(presetManager.getCurrentPresetName());
        currentIndex = (idx >= 0) ? idx : -1;
        currentIndex = juce::jmin(presetList.size() - 1, currentIndex + 1);
        loadPresetByIndex(currentIndex);
    };
}

void PresetPanel::paint(juce::Graphics&)
{
    // Transparent — blends into the header bar
}

void PresetPanel::resized()
{
    auto bounds = getLocalBounds();
    
    prevBtn.setBounds(bounds.removeFromLeft(20));
    nextBtn.setBounds(bounds.removeFromRight(20));
    presetNameLabel.setBounds(bounds);
}

void PresetPanel::refreshPresetName()
{
    presetNameLabel.setText(presetManager.getCurrentPresetName(), juce::dontSendNotification);
    presetNameLabel.setFont(WXFonts::get(16.0f, juce::Font::bold));
    presetNameLabel.setColour(juce::Label::textColourId, juce::Colour(0xFFFFFFFF));
}

void PresetPanel::loadPresetByIndex(int index)
{
    if (index >= 0 && index < presetList.size())
    {
        presetManager.loadPreset(presetList[index]);
        refreshPresetName();
        if (onPresetLoaded)
            onPresetLoaded();
    }
}

void PresetPanel::showSaveDialog()
{
    auto* editor = new juce::AlertWindow("SAVE PRESET", "Enter preset name:", juce::AlertWindow::NoIcon);
    editor->addTextEditor("name", presetManager.getCurrentPresetName(), "Name:");
    editor->addButton("SAVE", 1);
    editor->addButton("CANCEL", 0);
    
    editor->enterModalState(true, juce::ModalCallbackFunction::create(
        [this, editor](int result)
        {
            if (result == 1)
            {
                auto name = editor->getTextEditorContents("name").trim();
                if (name.isNotEmpty())
                {
                    presetManager.savePreset(name);
                    refreshPresetName();
                }
            }
            delete editor;
        }), false);
}

void PresetPanel::showPresetPopup()
{
    presetList = presetManager.getPresetList();
    
    juce::PopupMenu menu;
    
    // Preset list
    for (int i = 0; i < presetList.size(); ++i)
    {
        bool isCurrent = (presetList[i] == presetManager.getCurrentPresetName());
        menu.addItem(i + 1, presetList[i], true, isCurrent);
    }
    
    if (presetList.isEmpty())
        menu.addItem(-1, "(no presets)", false);
    
    menu.addSeparator();
    menu.addItem(10000, "Save Preset...");
    menu.addItem(10001, "Open Presets Folder...");
    
    // Delete submenu
    if (!presetList.isEmpty())
    {
        juce::PopupMenu deleteMenu;
        for (int i = 0; i < presetList.size(); ++i)
            deleteMenu.addItem(20000 + i, presetList[i]);
        menu.addSubMenu("Delete Preset", deleteMenu);
    }
    
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&presetNameLabel),
        [this](int result)
        {
            if (result >= 1 && result < 10000)
            {
                currentIndex = result - 1;
                loadPresetByIndex(currentIndex);
            }
            else if (result == 10000)
            {
                showSaveDialog();
            }
            else if (result == 10001)
            {
                presetManager.getPresetsFolder().startAsProcess();
            }
            else if (result >= 20000)
            {
                int deleteIdx = result - 20000;
                if (deleteIdx >= 0 && deleteIdx < presetList.size())
                {
                    presetManager.deletePreset(presetList[deleteIdx]);
                    refreshPresetName();
                }
            }
        });
}

void PresetPanel::mouseDown(const juce::MouseEvent&)
{
    showPresetPopup();
}
