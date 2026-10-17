#include "SettingsReader.h"

#include "SettingsLayout.h"
#include "SettingsProtocol.h"

#include <algorithm>
#include <stdexcept>
#include <string>

std::optional<SettingsReadResult> SettingsReader::read(const Exchange& exchange,
                                                       const std::function<void(double)>& progress,
                                                       const std::function<bool()>& cancelled)
{
    constexpr auto total = static_cast<std::uint32_t>(layout::snapshotSize);
    const auto stopped = [&] { return cancelled && cancelled(); };
    std::vector<Bytes> replies;

    for (std::uint32_t address = 0; address < total; address += layout::readChunkSize)
    {
        if (progress)
            progress(static_cast<double>(address) / total);

        // A dropped reply is asked for again. The mixer answers a repeated read the same way.
        const auto length = std::min(layout::readChunkSize, total - address);
        std::string problem = "no reply arrived";
        bool received = false;
        for (int attempt = 0; attempt < attemptsPerChunk && !received; ++attempt)
        {
            if (stopped())
                return std::nullopt;
            try
            {
                auto part = exchange(address, length);
                received = SettingsProtocol::findChunk(part, address, length).has_value();
                if (received)
                    replies.insert(replies.end(), std::make_move_iterator(part.begin()), std::make_move_iterator(part.end()));
            }
            catch (const std::exception& error)
            {
                problem = error.what();
            }
        }
        if (stopped())
            return std::nullopt;
        if (!received)
            throw std::runtime_error("The settings could not be read at address " + std::to_string(address) + ": "
                                     + problem + ". Check the connection and read again.");
    }

    auto result = SettingsProtocol::parseResponses(replies);
    if (result.data.size() != total)
        throw std::runtime_error("The settings image should be " + std::to_string(total) + " bytes, but "
                                 + std::to_string(result.data.size()) + " arrived");
    if (progress)
        progress(1.0);
    return result;
}
