#pragma once

#include "PluginProcessor.h"

#include <ehl/juce_design/EhlDesign.h>

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <memory>
#include <vector>

class SpectrummingAudioProcessorEditor final : public juce::AudioProcessorEditor,
                                               private juce::Timer
{
public:
    explicit SpectrummingAudioProcessorEditor(SpectrummingAudioProcessor&);
    ~SpectrummingAudioProcessorEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    class FrameDisplay final : public juce::Component
    {
    public:
        void setState(juce::Image, float scanPosition, float peak, int voices,
                      const juce::String& status);
        void paint(juce::Graphics&) override;

    private:
        juce::Image image;
        juce::String statusText { "NO INPUT" };
        float scan = 0.0f;
        float level = 0.0f;
        int activeVoices = 0;
    };

    using SliderAttachment = juce::AudioProcessorValueTreeState::SliderAttachment;
    using ButtonAttachment = juce::AudioProcessorValueTreeState::ButtonAttachment;
    using ComboAttachment = juce::AudioProcessorValueTreeState::ComboBoxAttachment;

    void timerCallback() override;
    void configureSlider(juce::Slider&, juce::Label&, const juce::String&);
    void configureCombo(juce::ComboBox&, juce::Label&, const juce::StringArray&,
                        const juce::String&);
    void configureToggle(juce::ToggleButton&, const juce::String&);
    void loadImage();

    SpectrummingAudioProcessor& ownerProcessor;
    ehl::juce_design::LookAndFeel lookAndFeel;
    FrameDisplay frameDisplay;

    juce::TextButton imageSource { "IMAGE" };
    juce::TextButton cameraSource { "CAMERA" };
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton bridgeButton { "OPEN BRIDGE" };
    juce::TextButton freezeButton { "FREEZE" };

    std::array<juce::Slider, 13> sliders;
    std::array<juce::Label, 13> sliderLabels;
    std::array<juce::ComboBox, 5> combos;
    std::array<juce::Label, 5> comboLabels;
    std::array<juce::ToggleButton, 2> toggles;

    std::vector<std::unique_ptr<SliderAttachment>> sliderAttachments;
    std::vector<std::unique_ptr<ComboAttachment>> comboAttachments;
    std::vector<std::unique_ptr<ButtonAttachment>> buttonAttachments;
    std::unique_ptr<juce::FileChooser> fileChooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(SpectrummingAudioProcessorEditor)
};
