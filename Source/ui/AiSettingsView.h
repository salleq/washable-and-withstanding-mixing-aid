#pragma once

#include "Components.h"
#include "ai/AiSettings.h"
#include <array>

namespace wwma::ui
{
/** The AI assistant's settings: which service (Claude, Gemini, ChatGPT), how WWMA reaches it
    (browser, the app on this computer, an API key), the consent, and where the answers are
    saved. A tab of the settings page. */
class AiSettingsView : public juce::Component
{
public:
    explicit AiSettingsView (const ViewState& s);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();

    void setSettings (const ai::Settings& s);
    const ai::Settings& getSettings() const noexcept { return settings; }
    void setTestStatus (const juce::String& text, bool ok);
    void setModels (const juce::StringArray& models);
    /** Where the app was found, or empty when it was not. */
    void setCliFound (const juce::File& exe);
    /** The folder the reports and the AI's answers are saved in. */
    void setAnswerFolder (const juce::File& folder);

    std::function<void()> onSettingsChanged, onTest, onBrowseCli, onOpenAnswerFolder;

private:
    const ViewState& state;
    ai::Settings settings;
    juce::String testStatus;
    bool testOk = false;
    juce::File cliFound, answerFolder;
    bool cliChecked = false;

    std::array<juce::TextButton, ai::numProviders> providerButtons;
    std::array<juce::TextButton, 3> modeButtons;
    juce::TextEditor keyEditor, modelEditor, cliEditor;
    juce::ToggleButton showKey, consent;
    juce::TextButton modelsButton, testButton, browseCliButton, folderButton;
    juce::StringArray models;

    juce::Rectangle<int> serviceLabel, howLabel, modeTextArea, keyLabelArea, modelLabelArea, cliLabelArea, cliStatusArea,
                         testStatusArea, privacyArea, savedArea, savedLabel;

    void syncControls();
    void edited();
    ai::Mode mode() const { return settings.current().mode; }
};
} // namespace wwma::ui
