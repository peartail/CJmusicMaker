#pragma once

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace musicbitmaker
{
struct LiveCommandResult
{
    bool ok = true;
    juce::var data { new juce::DynamicObject() };
    juce::String errorCode, errorMessage;
};

LiveCommandResult liveCommandSuccess (juce::var data);
LiveCommandResult liveCommandFailure (juce::String code, juce::String message);

using LiveCommandHandler = std::function<LiveCommandResult (const juce::String&, const juce::var&)>;

class LiveControlServer final
{
public:
    explicit LiveControlServer (LiveCommandHandler);
    ~LiveControlServer();

    bool start();
    void stop();

private:
    LiveControlServer (LiveCommandHandler, juce::File sessionFile);
    friend int runLiveControlSelfTest();

    struct Impl;
    std::unique_ptr<Impl> impl;
};

int runLiveControlClient (const juce::StringArray& arguments);
int runLiveControlSelfTest();
}
