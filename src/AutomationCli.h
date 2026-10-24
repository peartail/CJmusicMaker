#pragma once

#include <tracktion_engine/tracktion_engine.h>

#include <memory>

namespace musicbitmaker
{
std::unique_ptr<tracktion::engine::EngineBehaviour> createHeadlessEngineBehaviour();
int runHeadlessAutomation (tracktion::engine::Engine&, const juce::StringArray& arguments);
int runAutomationCliSelfTest();
}
