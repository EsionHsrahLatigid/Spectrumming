#include "BridgeDefaults.h"
#include "JuceCameraFrameSource.h"
#include "SharedFrameFile.h"

#include <ehl/juce_design/EhlDesign.h>

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace spectrumming::bridge
{
class BridgeComponent final : public juce::Component,
                              private juce::Timer
{
public:
    BridgeComponent()
        : channel(defaultStreamId), camera(channel, defaultStreamId)
    {
        setLookAndFeel(&lookAndFeel);
        setSize(520, 240);

        ehl::juce_design::styleComboBox(deviceBox);
        ehl::juce_design::styleLabel(statusLabel);
        ehl::juce_design::styleToggle(runButton);
        ehl::juce_design::styleToggle(refreshButton);

        deviceBox.setName("UVC camera device");
        runButton.setButtonText("START CAMERA");
        refreshButton.setButtonText("REFRESH");
        statusLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(deviceBox);
        addAndMakeVisible(runButton);
        addAndMakeVisible(refreshButton);
        addAndMakeVisible(statusLabel);

        refreshButton.onClick = [this] { refreshDevices(); };
        runButton.onClick = [this] { toggleCamera(); };

        if(channel.openWriter())
            statusLabel.setText("BRIDGE READY / NO CAMERA", juce::dontSendNotification);
        else
            statusLabel.setText("SHARED FRAME CHANNEL FAILED", juce::dontSendNotification);

        refreshDevices();
        startTimerHz(4);
    }

    ~BridgeComponent() override
    {
        camera.stop();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& graphics) override
    {
        ehl::juce_design::paintEditorChrome(
            graphics, getLocalBounds(), "Spectrumming Bridge", "UVC FRAME SOURCE");

        const auto field = getLocalBounds().reduced(16).withTrimmedTop(64);
        graphics.setColour(ehl::juce_design::Palette::low());
        graphics.drawRect(field, 1);
        graphics.setColour(ehl::juce_design::Palette::mid());
        graphics.drawText("LOCAL FRAME TRANSPORT / STREAM SPCT", field.reduced(12).removeFromTop(20),
                          juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced(28, 16);
        area.removeFromTop(72);
        deviceBox.setBounds(area.removeFromTop(40));
        area.removeFromTop(8);
        auto commands = area.removeFromTop(40);
        refreshButton.setBounds(commands.removeFromLeft(112));
        commands.removeFromLeft(8);
        runButton.setBounds(commands.removeFromLeft(160));
        area.removeFromTop(8);
        statusLabel.setBounds(area.removeFromTop(28));
    }

private:
    void refreshDevices()
    {
        const auto selected = deviceBox.getText();
        deviceBox.clear(juce::dontSendNotification);
        const auto devices = juce::CameraDevice::getAvailableDevices();
        for(int index = 0; index < devices.size(); ++index)
            deviceBox.addItem(devices[index], index + 1);

        if(devices.isEmpty())
        {
            deviceBox.setText("NO UVC DEVICE", juce::dontSendNotification);
            runButton.setEnabled(false);
            statusLabel.setText("NO UVC DEVICE", juce::dontSendNotification);
            return;
        }

        const auto restored = devices.indexOf(selected);
        deviceBox.setSelectedItemIndex(restored >= 0 ? restored : 0, juce::dontSendNotification);
        runButton.setEnabled(channel.isOpen());
    }

    void toggleCamera()
    {
        if(running)
        {
            camera.stop();
            running = false;
            runButton.setButtonText("START CAMERA");
            statusLabel.setText("BRIDGE READY / CAMERA STOPPED", juce::dontSendNotification);
            return;
        }

        const auto index = deviceBox.getSelectedItemIndex();
        running = index >= 0 && camera.start(index);
        runButton.setButtonText(running ? "STOP CAMERA" : "START CAMERA");
        statusLabel.setText(running ? "STREAMING UVC / SPCT" : "CAMERA OPEN FAILED",
                            juce::dontSendNotification);
    }

    void timerCallback() override
    {
        if(running && ! channel.isOpen())
        {
            camera.stop();
            running = false;
            runButton.setButtonText("START CAMERA");
            statusLabel.setText("SHARED FRAME CHANNEL LOST", juce::dontSendNotification);
        }
    }

    ehl::juce_design::LookAndFeel lookAndFeel;
    SharedFrameFile channel;
    JuceCameraFrameSource camera;
    juce::ComboBox deviceBox;
    juce::ToggleButton runButton;
    juce::ToggleButton refreshButton;
    juce::Label statusLabel;
    bool running = false;
};

class BridgeApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return bridgeProductName; }
    const juce::String getApplicationVersion() override { return "0.1.0"; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise(const juce::String&) override
    {
        window = std::make_unique<MainWindow>();
    }

    void shutdown() override
    {
        window.reset();
    }

    void systemRequestedQuit() override
    {
        quit();
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow()
            : DocumentWindow(bridgeProductName,
                             ehl::juce_design::Palette::ink(),
                             juce::DocumentWindow::closeButton)
        {
            setUsingNativeTitleBar(true);
            setContentOwned(new BridgeComponent(), true);
            setResizable(false, false);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> window;
};
} // namespace spectrumming::bridge

START_JUCE_APPLICATION(spectrumming::bridge::BridgeApplication)
