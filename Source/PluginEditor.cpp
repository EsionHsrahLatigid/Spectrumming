#include "PluginEditor.h"

#include "ParameterIDs.h"

#include <cmath>

namespace
{
constexpr int editorWidth = 640;
constexpr int editorHeight = 480;

const std::array<const char*, 13> sliderIds {
    spectrumming::parameters::freeDuration,
    spectrumming::parameters::startOffset,
    spectrumming::parameters::lowFrequency,
    spectrumming::parameters::highFrequency,
    spectrumming::parameters::gamma,
    spectrumming::parameters::blackPoint,
    spectrumming::parameters::frequencySmooth,
    spectrumming::parameters::frameSmooth,
    spectrumming::parameters::rootNote,
    spectrumming::parameters::attack,
    spectrumming::parameters::release,
    spectrumming::parameters::stereoWidth,
    spectrumming::parameters::outputGain
};

const std::array<const char*, 5> comboIds {
    spectrumming::parameters::triggerMode,
    spectrumming::parameters::clockMode,
    spectrumming::parameters::syncLength,
    spectrumming::parameters::direction,
    spectrumming::parameters::cycleMode
};

const std::array<const char*, 2> toggleIds {
    spectrumming::parameters::invert,
    spectrumming::parameters::mute
};

void styleCommand(juce::Button& button)
{
    button.setColour(juce::TextButton::buttonColourId, ehl::juce_design::Palette::ink());
    button.setColour(juce::TextButton::buttonOnColourId, ehl::juce_design::Palette::paper());
    button.setColour(juce::TextButton::textColourOffId, ehl::juce_design::Palette::paper());
    button.setColour(juce::TextButton::textColourOnId, ehl::juce_design::Palette::ink());
    button.setWantsKeyboardFocus(true);
}
} // namespace

SpectrummingAudioProcessorEditor::SpectrummingAudioProcessorEditor(
    SpectrummingAudioProcessor& owner)
    : AudioProcessorEditor(owner), ownerProcessor(owner)
{
    setLookAndFeel(&lookAndFeel);
    setSize(editorWidth, editorHeight);
    setResizable(false, false);

    addAndMakeVisible(frameDisplay);
    for(auto* button : { &imageSource, &cameraSource, &loadButton, &bridgeButton, &freezeButton })
    {
        styleCommand(*button);
        addAndMakeVisible(*button);
    }

    imageSource.setClickingTogglesState(true);
    cameraSource.setClickingTogglesState(true);
    freezeButton.setClickingTogglesState(true);
    imageSource.setRadioGroupId(0x5350);
    cameraSource.setRadioGroupId(0x5350);
    imageSource.setToggleState(true, juce::dontSendNotification);

    imageSource.onClick = [this] { ownerProcessor.selectImageSource(); };
    cameraSource.onClick = [this] { ownerProcessor.selectCameraSource(); };
    loadButton.onClick = [this] { loadImage(); };
    bridgeButton.onClick = [this] { ownerProcessor.launchBridge(); };
    freezeButton.onClick = [this] { ownerProcessor.setCameraFrozen(freezeButton.getToggleState()); };

    const std::array<const char*, 13> sliderNames {
        "DURATION", "START", "LOW HZ", "HIGH HZ", "GAMMA", "BLACK",
        "FREQ SMOOTH", "FRAME SMOOTH", "ROOT", "ATTACK", "RELEASE", "WIDTH", "OUTPUT DB"
    };
    for(std::size_t index = 0; index < sliders.size(); ++index)
    {
        configureSlider(sliders[index], sliderLabels[index], sliderNames[index]);
        sliderAttachments.push_back(std::make_unique<SliderAttachment>(
            ownerProcessor.parameterState(), sliderIds[index], sliders[index]));
    }

    const std::array<juce::StringArray, 5> comboItems {
        juce::StringArray { "AUTO", "MIDI" },
        juce::StringArray { "FREE", "HOST" },
        juce::StringArray { "1/16", "1/8", "1/4", "1/2", "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR" },
        juce::StringArray { "FORWARD", "REVERSE", "PING-PONG" },
        juce::StringArray { "LOOP", "ONE-SHOT" }
    };
    const std::array<const char*, 5> comboNames { "TRIGGER", "CLOCK", "LENGTH", "DIRECTION", "CYCLE" };
    for(std::size_t index = 0; index < combos.size(); ++index)
    {
        configureCombo(combos[index], comboLabels[index], comboItems[index], comboNames[index]);
        comboAttachments.push_back(std::make_unique<ComboAttachment>(
            ownerProcessor.parameterState(), comboIds[index], combos[index]));
    }

    configureToggle(toggles[0], "INVERT");
    configureToggle(toggles[1], "MUTE");
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(
        ownerProcessor.parameterState(), toggleIds[0], toggles[0]));
    buttonAttachments.push_back(std::make_unique<ButtonAttachment>(
        ownerProcessor.parameterState(), toggleIds[1], toggles[1]));

    startTimerHz(30);
}

SpectrummingAudioProcessorEditor::~SpectrummingAudioProcessorEditor()
{
    stopTimer();
    setLookAndFeel(nullptr);
}

void SpectrummingAudioProcessorEditor::paint(juce::Graphics& graphics)
{
    ehl::juce_design::paintEditorChrome(
        graphics, getLocalBounds(), "Spectrumming", "IMAGE / SPECTRUM INSTRUMENT");

    graphics.setColour(ehl::juce_design::Palette::low());
    graphics.drawRect(getLocalBounds().reduced(16).withTrimmedTop(64), 1);
}

void SpectrummingAudioProcessorEditor::resized()
{
    auto toolbar = juce::Rectangle<int>(16, 70, 608, 26);
    imageSource.setBounds(toolbar.removeFromLeft(68));
    toolbar.removeFromLeft(4);
    cameraSource.setBounds(toolbar.removeFromLeft(76));
    toolbar.removeFromLeft(8);
    loadButton.setBounds(toolbar.removeFromLeft(60));
    toolbar.removeFromLeft(4);
    bridgeButton.setBounds(toolbar.removeFromLeft(104));
    toolbar.removeFromLeft(4);
    freezeButton.setBounds(toolbar.removeFromLeft(72));

    frameDisplay.setBounds(16, 104, 608, 120);

    std::array<juce::Component*, 20> controls {
        &combos[0], &combos[1], &sliders[0], &combos[2], &combos[3],
        &combos[4], &sliders[1], &sliders[2], &sliders[3], &sliders[8],
        &sliders[4], &sliders[5], &toggles[0], &sliders[6], &sliders[7],
        &sliders[9], &sliders[10], &sliders[11], &sliders[12], &toggles[1]
    };
    std::array<juce::Label*, 20> labels {
        &comboLabels[0], &comboLabels[1], &sliderLabels[0], &comboLabels[2], &comboLabels[3],
        &comboLabels[4], &sliderLabels[1], &sliderLabels[2], &sliderLabels[3], &sliderLabels[8],
        &sliderLabels[4], &sliderLabels[5], nullptr, &sliderLabels[6], &sliderLabels[7],
        &sliderLabels[9], &sliderLabels[10], &sliderLabels[11], &sliderLabels[12], nullptr
    };

    constexpr int columns = 5;
    constexpr int cellWidth = 116;
    constexpr int cellHeight = 56;
    for(std::size_t index = 0; index < controls.size(); ++index)
    {
        const auto column = static_cast<int>(index) % columns;
        const auto row = static_cast<int>(index) / columns;
        auto cell = juce::Rectangle<int>(16 + column * 122, 232 + row * cellHeight,
                                         cellWidth, cellHeight - 4);
        if(labels[index] != nullptr)
        {
            labels[index]->setBounds(cell.removeFromTop(14));
            controls[index]->setBounds(cell.removeFromTop(30));
        }
        else
        {
            controls[index]->setBounds(cell.reduced(0, 8));
        }
    }

}

void SpectrummingAudioProcessorEditor::timerCallback()
{
    const auto state = ownerProcessor.sourceStateSnapshot();
    imageSource.setToggleState(state.kind == spectrumming::plugin::SourceKind::image,
                               juce::dontSendNotification);
    cameraSource.setToggleState(state.kind == spectrumming::plugin::SourceKind::liveBridge,
                                juce::dontSendNotification);
    freezeButton.setToggleState(state.frozen, juce::dontSendNotification);
    freezeButton.setEnabled(state.kind == spectrumming::plugin::SourceKind::liveBridge);
    frameDisplay.setState(ownerProcessor.previewImageSnapshot(), ownerProcessor.scanPosition(),
                          ownerProcessor.outputPeak(), ownerProcessor.activeVoiceCount(),
                          ownerProcessor.sourceStatusSnapshot());
}

void SpectrummingAudioProcessorEditor::configureSlider(
    juce::Slider& slider, juce::Label& label, const juce::String& text)
{
    ehl::juce_design::styleSlider(slider);
    ehl::juce_design::styleLabel(label);
    slider.setSliderStyle(juce::Slider::LinearHorizontal);
    slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 48, 20);
    slider.setName(text);
    label.setText(text, juce::dontSendNotification);
    addAndMakeVisible(slider);
    addAndMakeVisible(label);
}

void SpectrummingAudioProcessorEditor::configureCombo(
    juce::ComboBox& combo, juce::Label& label, const juce::StringArray& items,
    const juce::String& text)
{
    ehl::juce_design::styleComboBox(combo);
    ehl::juce_design::styleLabel(label);
    combo.addItemList(items, 1);
    combo.setName(text);
    label.setText(text, juce::dontSendNotification);
    addAndMakeVisible(combo);
    addAndMakeVisible(label);
}

void SpectrummingAudioProcessorEditor::configureToggle(
    juce::ToggleButton& toggle, const juce::String& text)
{
    ehl::juce_design::styleToggle(toggle);
    toggle.setButtonText(text);
    toggle.setName(text);
    addAndMakeVisible(toggle);
}

void SpectrummingAudioProcessorEditor::loadImage()
{
    fileChooser = std::make_unique<juce::FileChooser>(
        "Load image", juce::File {}, "*.png;*.jpg;*.jpeg;*.gif;*.bmp");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode
                                 | juce::FileBrowserComponent::canSelectFiles,
                             [safe = juce::Component::SafePointer<SpectrummingAudioProcessorEditor>(this)](
                                 const juce::FileChooser& chooser)
                             {
                                 if(safe == nullptr)
                                     return;
                                 const auto file = chooser.getResult();
                                 if(file.existsAsFile())
                                 {
                                     juce::String error;
                                     safe->ownerProcessor.loadImageFile(file, error);
                                 }
                             });
}

void SpectrummingAudioProcessorEditor::FrameDisplay::setState(
    juce::Image newImage, const float scanPosition, const float peak,
    const int voices, const juce::String& status)
{
    image = std::move(newImage);
    scan = juce::jlimit(0.0f, 1.0f, scanPosition);
    level = juce::jlimit(0.0f, 1.0f, peak);
    activeVoices = voices;
    statusText = status;
    repaint();
}

void SpectrummingAudioProcessorEditor::FrameDisplay::paint(juce::Graphics& graphics)
{
    const auto bounds = getLocalBounds();
    graphics.setColour(ehl::juce_design::Palette::low());
    graphics.fillRect(bounds);

    auto imageArea = bounds.reduced(8);
    imageArea.removeFromBottom(22);
    if(image.isValid())
        graphics.drawImage(image, imageArea.toFloat(), juce::RectanglePlacement::centred);
    else
    {
        graphics.setColour(ehl::juce_design::Palette::ink());
        graphics.fillRect(imageArea);
    }

    const auto x = imageArea.getX() + juce::roundToInt(scan * static_cast<float>(imageArea.getWidth() - 1));
    graphics.setColour(ehl::juce_design::Palette::paper());
    graphics.drawVerticalLine(x, static_cast<float>(imageArea.getY()),
                              static_cast<float>(imageArea.getBottom()));

    const auto drawFrequencyLabel = [&graphics](const juce::String& text, juce::Rectangle<int> area)
    {
        graphics.setColour(ehl::juce_design::Palette::ink());
        graphics.fillRect(area);
        graphics.setColour(ehl::juce_design::Palette::paper());
        graphics.setFont(juce::FontOptions(9.0f));
        graphics.drawText(text, area, juce::Justification::centred, false);
    };
    drawFrequencyLabel("HIGH", { imageArea.getRight() - 36, imageArea.getY(), 36, 12 });
    drawFrequencyLabel("LOW", { imageArea.getRight() - 36, imageArea.getBottom() - 12, 36, 12 });

    auto footer = bounds.reduced(8).removeFromBottom(18);
    graphics.setFont(juce::FontOptions(10.0f));
    graphics.setColour(ehl::juce_design::Palette::paper());
    graphics.drawText(statusText, footer.removeFromLeft(420), juce::Justification::centredLeft, true);
    graphics.setColour(ehl::juce_design::Palette::mid());
    graphics.drawText("V " + juce::String(activeVoices), footer.removeFromLeft(44),
                      juce::Justification::centredLeft, true);
    const auto meter = footer.reduced(2, 5);
    graphics.drawRect(meter, 1);
    graphics.setColour(ehl::juce_design::Palette::paper());
    graphics.fillRect(meter.withWidth(juce::roundToInt(level * static_cast<float>(meter.getWidth()))));
}
