#include "SettingsWriter.h"

#include "ControlConfiguration.h"
#include "SettingsProtocol.h"

#include <set>
#include <stdexcept>
#include <string>

namespace
{
const std::string unsavedAdvice
    = " Nothing was saved. Write again to retry, or switch the mixer off and on to discard what reached it.";

bool rejected(const std::string& message)
{
    return message.find("rejected the write") != std::string::npos;
}

std::string at(std::uint32_t address)
{
    return "address " + std::to_string(address);
}

// True when the span is exactly one CC / User knob, fader, or button record.
bool wholeUserRecord(const SettingsSpan& span)
{
    if (span.address < layout::profileSize)
        return false;
    const auto local = static_cast<std::size_t>(span.address) - layout::profileSize;
    const auto size = span.data.size();
    if (size == layout::buttonSize)
        return local % layout::buttonSize == 0 && local / layout::buttonSize < layout::buttonCount;
    if (size == layout::knobSize && local >= layout::knobsOffset)
    {
        const auto offset = local - layout::knobsOffset;
        return offset % layout::knobSize == 0 && offset / layout::knobSize < layout::knobCount;
    }
    if (size == layout::sliderSize && local >= layout::slidersOffset)
    {
        const auto offset = local - layout::slidersOffset;
        return offset % layout::sliderSize == 0 && offset / layout::sliderSize < layout::sliderCount;
    }
    return false;
}
}

void SettingsWriter::validate(const std::vector<SettingsSpan>& spans)
{
    if (spans.empty())
        throw std::runtime_error("There are no changed records to write");

    std::set<std::uint32_t> addresses;
    for (const auto& span : spans)
    {
        if (span.address < layout::profileSize)
            throw std::runtime_error("The DAW profile is read-only, " + at(span.address));
        if (!wholeUserRecord(span))
            throw std::runtime_error("A changed record has an invalid address or length, " + at(span.address));
        if (!addresses.insert(span.address).second)
            throw std::runtime_error("A changed record appears twice, " + at(span.address));
    }
}

std::optional<std::vector<SettingsSpan>> SettingsWriter::write(const std::vector<SettingsSpan>& spans,
                                                               const Exchange& exchange,
                                                               const std::function<void(double)>& progress,
                                                               const std::function<bool()>& cancelled)
{
    validate(spans);

    const auto steps = spans.size() * 2 + 1;
    std::size_t step = 0;
    const auto report = [&] {
        if (progress)
            progress(static_cast<double>(step) / static_cast<double>(steps));
    };
    const auto stopped = [&] { return cancelled && cancelled(); };

    for (const auto& span : spans)
    {
        const auto length = static_cast<std::uint32_t>(span.data.size());

        // A lost acknowledgement is retried once. Writing the same bytes twice is harmless.
        report();
        std::string writeError = "No write acknowledgement was received";
        bool acknowledged = false;
        for (int attempt = 0; attempt < 2 && !acknowledged; ++attempt)
        {
            if (stopped())
                return std::nullopt;
            try
            {
                SettingsProtocol::parseWriteAcknowledgement(
                    exchange({ Step::write, SettingsProtocol::writePayload(span.address, span.data), span.address, length }));
                acknowledged = true;
            }
            catch (const std::exception& error)
            {
                writeError = error.what();
                if (rejected(writeError))
                    break;
            }
        }
        if (!acknowledged)
            throw std::runtime_error("The record at " + at(span.address) + " was not written: " + writeError + "."
                                     + unsavedAdvice);
        ++step;

        report();
        std::optional<Bytes> readBack;
        std::string readError = "No read-back data was received";
        for (int attempt = 0; attempt < 2 && !readBack; ++attempt)
        {
            if (stopped())
                return std::nullopt;
            try
            {
                readBack = SettingsProtocol::parseChunk(
                    exchange({ Step::readBack, SettingsProtocol::readPayload(5, span.address, length), span.address, length }),
                    span.address,
                    length);
            }
            catch (const std::exception& error)
            {
                readError = error.what();
            }
        }
        if (!readBack)
            throw std::runtime_error("The record at " + at(span.address) + " could not be read back: " + readError + "."
                                     + unsavedAdvice);
        if (*readBack != span.data)
            throw std::runtime_error("The record at " + at(span.address) + " read back different bytes." + unsavedAdvice);
        ++step;
    }

    report();
    std::string saveError = "No save acknowledgement was received";
    bool saved = false;
    for (int attempt = 0; attempt < 2 && !saved; ++attempt)
    {
        if (stopped())
            return std::nullopt;
        try
        {
            SettingsProtocol::parseWriteAcknowledgement(exchange({ Step::save, SettingsProtocol::savePayload(), 0, 0 }));
            saved = true;
        }
        catch (const std::exception& error)
        {
            saveError = error.what();
            if (rejected(saveError))
                throw std::runtime_error("The mixer took the records but refused to save them."
                                         " Switch it off and on to discard them, then write again.");
        }
    }
    if (!saved)
        throw std::runtime_error("The records reached the mixer, but the save was not confirmed: " + saveError
                                 + ". Write again to retry the save.");

    ++step;
    report();
    return spans;
}
