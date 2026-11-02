#pragma once

#include "Theme.h"
#include "advice/Advisor.h"
#include "reference/VersionStore.h"
#include <functional>
#include <memory>
#include <vector>

namespace wwma::ui
{
/** One line of a list: text on the left, small buttons on the right. */
class ListRow : public juce::Component
{
public:
    struct Action { juce::String label; std::function<void()> onClick; bool primary = false; juce::String tooltip; };
    ListRow (juce::String title, juce::String detail, std::vector<Action> actions, bool heading = false);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    juce::String title, detail;
    bool heading;
    std::vector<std::unique_ptr<juce::TextButton>> buttons;
    std::vector<Action> actions;
};

/** WWMA was just added to a project: offers to continue an earlier session from WWMA's own folder.
    Shown over the whole window until the user chooses; nothing is restored without a click. */
class SessionOffer : public juce::Component
{
public:
    SessionOffer();
    void show (const std::vector<store::SessionInfo>& sessions, Lang lang);
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void (const juce::File&)> onContinue;
    std::function<void()> onFresh;
private:
    Lang lang = Lang::english;
    std::vector<std::unique_ptr<ListRow>> rows;
    juce::TextButton freshButton;
    juce::Rectangle<int> card() const;
};

/** The before versions saved for this session, and the other sessions in WWMA's folder:
    use, name or remove them. Shown over the findings. */
class VersionsPanel : public juce::Component
{
public:
    VersionsPanel();
    struct Data
    {
        Lang lang = Lang::english;
        juce::String sessionName;
        juce::File sessionFolder;
        std::vector<store::VersionInfo> versions;
        std::vector<store::SessionInfo> others;
        juce::int64 totalBytes = 0;
        int beforeSeconds = 0;     // the before take in memory now
        bool busy = false;         // saving or loading in the background
    };
    void setData (Data d);
    void paint (juce::Graphics&) override;
    void resized() override;

    std::function<void (const store::VersionInfo&)> onUse, onRename, onRemove;
    std::function<void (const store::SessionInfo&)> onContinueSession, onRemoveSession;
    std::function<void()> onRemoveAll, onOpenFolder, onRenameSession, onClose;

private:
    Data data;
    juce::Viewport viewport;
    juce::Component list;
    std::vector<std::unique_ptr<ListRow>> rows;
    juce::TextButton closeButton, folderButton, removeAllButton, renameButton;
    void rebuild();
};

/** "today 21:40", "yesterday 21:40", "12.11. 21:40" (in Finnish) or "Nov 12 21:40". */
juce::String whenText (juce::Time t, Lang lang);
/** "3:12" */
juce::String lengthText (double seconds);
} // namespace wwma::ui
