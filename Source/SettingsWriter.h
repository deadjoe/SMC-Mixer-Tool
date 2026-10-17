#pragma once

#include "DeviceModel.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// The write sequence, apart from any transport: write each record, read it back, then save.
// A 0x22 write only changes the mixer's RAM, and the read-back reads RAM too, so only the
// save at the end makes the edits survive a power cycle. Nothing is saved unless every
// record was written and read back intact; a power cycle then discards what reached RAM.
class SettingsWriter
{
public:
    enum class Step
    {
        write,
        readBack,
        save
    };

    struct Request
    {
        Step step = Step::write;
        Bytes payload;               // vendor payload, 00 59 ...
        std::uint32_t address = 0;   // record address for write and readBack
        std::uint32_t length = 0;    // record length for write and readBack
    };

    // Sends one payload and returns every reply that arrived for it. Throws on a link error.
    using Exchange = std::function<std::vector<Bytes>(const Request&)>;

    // Checks the spans, then runs the sequence. Returns the written spans, or nothing if
    // cancelled() turned true between steps. Throws with a message for the person on failure.
    static std::optional<std::vector<SettingsSpan>> write(const std::vector<SettingsSpan>& spans,
                                                          const Exchange& exchange,
                                                          const std::function<void(double)>& progress = {},
                                                          const std::function<bool()>& cancelled = {});

    static void validate(const std::vector<SettingsSpan>& spans);
};
