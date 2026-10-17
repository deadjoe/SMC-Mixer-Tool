#pragma once

#include "DeviceModel.h"

#include <cstdint>
#include <functional>
#include <optional>
#include <vector>

// The read sequence, apart from any transport: ask for the settings image in chunks, retry a
// chunk that does not arrive, then assemble and check the whole image.
class SettingsReader
{
public:
    // Sends one read for this range and returns every reply that arrived. Throws on a link error.
    using Exchange = std::function<std::vector<Bytes>(std::uint32_t address, std::uint32_t length)>;

    static constexpr int attemptsPerChunk = 2;

    // Returns the image, or nothing if cancelled() turned true between requests.
    // Throws with a message for the person when a chunk cannot be read.
    static std::optional<SettingsReadResult> read(const Exchange& exchange,
                                                  const std::function<void(double)>& progress = {},
                                                  const std::function<bool()>& cancelled = {});
};
