#pragma once

#include "ControlConfiguration.h"

#include <JuceHeader.h>

// Reads a profile exported as JSON back into a ControlProfile. The file only has to name the
// controls it changes; anything it leaves out keeps its value in the profile passed in.
// Channels in the file count from 1, as in the export.
namespace profilejson
{
juce::Result read(const juce::String& text, ControlProfile& profile);
}
