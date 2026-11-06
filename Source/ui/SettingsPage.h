#pragma once

#include "Components.h"
#include "AiSettingsView.h"
#include "stream/StreamServer.h"
#include <memory>
#include <vector>

namespace wwma::ui
{
/** The footer's settings button: a gear, with a green dot and the number of listeners while
    the stream is on (so a running stream is never forgotten). */
class SettingsButton : public juce::Button
{
public:
    SettingsButton() : juce::Button ("settings") {}
    void setStream (bool on, int listeners);
    void paintButton (juce::Graphics&, bool highlighted, bool down) override;
private:
    bool streaming = false;
    int count = 0;
};

/** WWMA's settings, on a page of its own over the window, in tabs:
    General (the language), AI assistant (the service, how WWMA reaches it, where answers are
    kept) and Listen anywhere (the output as a stream for a browser, with its port, address,
    password and link). */
class SettingsPage : public juce::Component
{
public:
    SettingsPage (const ViewState& s, stream::Server& server);
    void paint (juce::Graphics&) override;
    void resized() override;
    void updateTexts();
    /** Reads the stream's state again (the editor's timer, while the page is shown). */
    void refresh();

    enum class Tab { general, ai, stream };
    void setTab (Tab t);
    Tab getTab() const noexcept { return tab; }
    /** The AI assistant's settings (the editor fills it and listens to it). */
    AiSettingsView& aiSettings() noexcept { return aiView; }

    std::function<void()> onClose, onStreamChanged;
    std::function<void (Lang)> onLanguageChanged;

private:
    /** One link to the player: the address, where it works, Copy and Open. */
    class LinkRow : public juce::Component
    {
    public:
        LinkRow (stream::Link l, Lang lang);
        void paint (juce::Graphics&) override;
        void resized() override;
    private:
        stream::Link link;
        Lang lang;
        juce::TextButton copyButton, openButton;
    };

    const ViewState& state;
    stream::Server& server;
    Tab tab = Tab::general;
    std::array<juce::TextButton, 3> tabButtons;
    juce::TextButton englishButton, finnishButton;
    AiSettingsView aiView;
    juce::Rectangle<int> sectionArea, languageLabel, languageHelp;
    int dividerY = 0;
    /** The stream tab's own controls (hidden on the other tabs). */
    std::vector<juce::Component*> streamControls();
    stream::Settings settings;
    juce::String note;          // after a click: "Link copied", "Password set"...
    juce::Colour noteColour;
    stream::Server::State shownState = stream::Server::State::off;
    int shownListeners = -1;
    juce::String shownLinks;

    juce::TextButton closeButton, streamButton, newLinkButton, homeButton, localButton, setPasswordButton;
    juce::ToggleButton passwordToggle;
    juce::TextEditor portEditor, passwordEditor, addressEditor;
    std::vector<std::unique_ptr<LinkRow>> linkRows;

    juce::Rectangle<int> streamArea, optionsArea, introArea, statusArea, linksArea, portLabel, listenLabel, passwordLabel,
                         passwordStatus, listenHelp, helpArea, addressLabel, addressHelp;

    void toggleStream();
    void rebuildLinks();
    void syncControls();
    void save();
    void setNote (const juce::String& text, juce::Colour c);
    juce::String statusText (juce::Colour& c) const;
};
} // namespace wwma::ui
