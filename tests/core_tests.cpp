#include "ControlConfiguration.h"
#include "ControlDescription.h"
#include "MidiStream.h"
#include "SettingsDocument.h"
#include "SettingsProtocol.h"
#include "SettingsReader.h"
#include "SettingsWriter.h"
#include "VendorProtocol.h"

#include <algorithm>
#include <cstdint>
#include <functional>
#include <iostream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (condition)
        return;
    ++failures;
    std::cerr << "FAIL " << message << std::endl;
}

void expectThrow(const char* message, const std::function<void()>& action, const std::string& part)
{
    try
    {
        action();
        expect(false, message);
    }
    catch (const std::exception& error)
    {
        expect(std::string(error.what()).find(part) != std::string::npos, message);
    }
}

Bytes snapshotWith(std::size_t offset, Bytes bytes)
{
    Bytes image(layout::snapshotSize);
    for (std::size_t index = 0; index < bytes.size(); ++index)
        image[offset + index] = bytes[index];
    return image;
}

// Built by hand rather than with VendorProtocol::frame(), so the tests check the helper too.
Bytes framed(std::uint8_t command, const Bytes& body)
{
    const auto length = static_cast<std::uint32_t>(body.size());
    Bytes payload {
        0x00, 0x59, command,
        static_cast<std::uint8_t>(length & 0xff),
        static_cast<std::uint8_t>((length >> 8) & 0xff),
        static_cast<std::uint8_t>((length >> 16) & 0xff)
    };
    payload.insert(payload.end(), body.begin(), body.end());
    payload.push_back(VendorProtocol::checksum(body));
    return payload;
}

Bytes settingsChunk(std::uint32_t address, const Bytes& data)
{
    Bytes body {
        0x05,
        static_cast<std::uint8_t>(address & 0xff),
        static_cast<std::uint8_t>((address >> 8) & 0xff),
        static_cast<std::uint8_t>((address >> 16) & 0xff),
        static_cast<std::uint8_t>((address >> 24) & 0xff),
        static_cast<std::uint8_t>(data.size() & 0xff),
        static_cast<std::uint8_t>((data.size() >> 8) & 0xff),
        static_cast<std::uint8_t>((data.size() >> 16) & 0xff)
    };
    body.insert(body.end(), data.begin(), data.end());
    return framed(0x23, body);
}

// A mixer with separate RAM and flash, answering vendor payloads the way the real one does:
// a 0x22 write changes RAM, a 0x23 read returns RAM, and the empty 0x22 save copies RAM to flash.
struct FakeMixer
{
    Bytes ram = Bytes(layout::snapshotSize);
    Bytes flash = Bytes(layout::snapshotSize);
    std::vector<SettingsWriter::Step> steps;
    int dropWriteAcks = 0;        // writes applied but not acknowledged
    int failReads = 0;            // read requests that get no reply
    int dropSaveAcks = 0;         // saves not performed and not acknowledged
    bool rejectWrites = false;    // status 1 for data writes
    bool rejectSave = false;      // status 1 for the save
    bool corruptReads = false;    // read-back returns one changed byte
    bool linkDown = false;        // exchange throws

    std::vector<Bytes> answer(const SettingsWriter::Request& request)
    {
        steps.push_back(request.step);
        if (linkDown)
            throw std::runtime_error("The mixer did not answer on this link");
        const auto& payload = request.payload;
        const auto address = static_cast<std::size_t>(payload[7]) | (static_cast<std::size_t>(payload[8]) << 8);
        const auto length = static_cast<std::size_t>(payload[11]) | (static_cast<std::size_t>(payload[12]) << 8);
        if (payload[2] == 0x23)
        {
            if (failReads > 0)
            {
                --failReads;
                return {};
            }
            Bytes data(ram.begin() + static_cast<std::ptrdiff_t>(address),
                       ram.begin() + static_cast<std::ptrdiff_t>(address + length));
            if (corruptReads)
                data.front() ^= 1;
            return { settingsChunk(static_cast<std::uint32_t>(address), data) };
        }
        if (length == 0)
        {
            if (rejectSave)
                return { framed(0x00, { 0x01 }) };
            if (dropSaveAcks > 0)
            {
                --dropSaveAcks;
                return {};
            }
            flash = ram;
            return { framed(0x00, { 0x00 }) };
        }
        if (rejectWrites)
            return { framed(0x00, { 0x01 }) };
        std::copy(payload.begin() + 14, payload.begin() + 14 + static_cast<std::ptrdiff_t>(length),
                  ram.begin() + static_cast<std::ptrdiff_t>(address));
        if (dropWriteAcks > 0)
        {
            --dropWriteAcks;
            return {};
        }
        return { framed(0x00, { 0x00 }) };
    }

    SettingsWriter::Exchange exchange()
    {
        return [this](const SettingsWriter::Request& request) { return answer(request); };
    }
};

SettingsSpan userKnob(int index, Bytes data)
{
    return { static_cast<std::uint32_t>(layout::profileSize + layout::knobsOffset + static_cast<std::size_t>(index) * layout::knobSize),
             std::move(data) };
}

SettingsSpan userButton(int index, std::uint8_t fill)
{
    return { static_cast<std::uint32_t>(layout::profileSize + static_cast<std::size_t>(index) * layout::buttonSize),
             Bytes(layout::buttonSize, fill) };
}

bool sameRange(const Bytes& image, const SettingsSpan& span)
{
    return std::equal(span.data.begin(), span.data.end(), image.begin() + static_cast<std::ptrdiff_t>(span.address));
}

void testWriter()
{
    using Step = SettingsWriter::Step;
    const auto knob = userKnob(1, { 3, 2, 0, 103, 127, 1 });
    const auto button = userButton(42, 9);

    {
        FakeMixer mixer;
        std::vector<double> progress;
        const auto written = SettingsWriter::write({ knob, button }, mixer.exchange(),
                                                   [&](double value) { progress.push_back(value); });
        expect(written && written->size() == 2, "writer returns both spans");
        expect(mixer.steps == std::vector<Step>({ Step::write, Step::readBack, Step::write, Step::readBack, Step::save }),
               "writer order is write, read back, then one save");
        expect(sameRange(mixer.flash, knob) && sameRange(mixer.flash, button), "save puts both records in flash");
        expect(!progress.empty() && progress.back() == 1.0, "progress ends at 1");
        expect(std::is_sorted(progress.begin(), progress.end()), "progress never goes back");
    }
    {
        FakeMixer mixer;
        mixer.dropWriteAcks = 1;
        expect(SettingsWriter::write({ knob }, mixer.exchange()).has_value(), "a lost write ack is retried");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::write) == 2, "the write is sent twice");
        expect(sameRange(mixer.flash, knob), "retried write is saved");
    }
    {
        FakeMixer mixer;
        mixer.failReads = 1;
        expect(SettingsWriter::write({ knob }, mixer.exchange()).has_value(), "a missing read-back is retried");
    }
    {
        FakeMixer mixer;
        mixer.rejectWrites = true;
        expectThrow("rejected write stops", [&] { SettingsWriter::write({ knob, button }, mixer.exchange()); },
                    "rejected it, status 01");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::write) == 1, "a rejected write is not retried");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::save) == 0, "a rejected write is never saved");
    }
    {
        FakeMixer mixer;
        mixer.corruptReads = true;
        expectThrow("read-back mismatch stops", [&] { SettingsWriter::write({ knob }, mixer.exchange()); },
                    "read back different bytes");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::save) == 0, "a mismatch is never saved");
        expect(mixer.flash == Bytes(layout::snapshotSize), "flash keeps the old bytes after a mismatch");
    }
    {
        FakeMixer mixer;
        mixer.failReads = 2;
        expectThrow("unreadable record stops", [&] { SettingsWriter::write({ knob }, mixer.exchange()); },
                    "could not be read back");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::save) == 0, "an unread record is never saved");
    }
    {
        FakeMixer mixer;
        mixer.dropSaveAcks = 1;
        expect(SettingsWriter::write({ knob }, mixer.exchange()).has_value(), "a lost save ack is retried");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::save) == 2, "the save is sent twice");
    }
    {
        FakeMixer mixer;
        mixer.dropSaveAcks = 2;
        expectThrow("unconfirmed save", [&] { SettingsWriter::write({ knob }, mixer.exchange()); },
                    "save was not confirmed");
    }
    {
        FakeMixer mixer;
        mixer.rejectSave = true;
        expectThrow("refused save", [&] { SettingsWriter::write({ knob }, mixer.exchange()); }, "refused to save");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::save) == 1, "a refused save is not retried");
    }
    {
        FakeMixer mixer;
        mixer.linkDown = true;
        expectThrow("link down", [&] { SettingsWriter::write({ knob }, mixer.exchange()); }, "did not answer");
    }
    {
        FakeMixer mixer;
        int checks = 0;
        const auto written = SettingsWriter::write({ knob, button }, mixer.exchange(), {},
                                                   [&] { return ++checks > 2; });
        expect(!written, "cancel returns nothing");
        expect(std::count(mixer.steps.begin(), mixer.steps.end(), Step::save) == 0, "a cancelled write is never saved");
    }

    expectThrow("no spans", [] { SettingsWriter::validate({}); }, "no changed records");
    expectThrow("daw span", [] { SettingsWriter::validate({ { 0, Bytes(layout::buttonSize) } }); }, "read-only");
    expectThrow("misaligned span", [] {
        SettingsWriter::validate({ { static_cast<std::uint32_t>(layout::profileSize + 1), Bytes(layout::buttonSize) } });
    }, "invalid address");
    expectThrow("knob size at a fader", [] {
        SettingsWriter::validate({ { static_cast<std::uint32_t>(layout::profileSize + layout::slidersOffset), Bytes(layout::knobSize) } });
    }, "invalid address");
    expectThrow("span past the end", [] {
        SettingsWriter::validate({ { static_cast<std::uint32_t>(layout::snapshotSize - 2), Bytes(layout::knobSize) } });
    }, "invalid address");
    expectThrow("duplicate span", [&] { SettingsWriter::validate({ knob, knob }); }, "twice");
    SettingsWriter::validate({ userKnob(7, Bytes(layout::knobSize)),
                               { static_cast<std::uint32_t>(layout::profileSize + layout::slidersOffset + 7 * layout::sliderSize),
                                 Bytes(layout::sliderSize) },
                               userButton(0, 0) });
}

void testMidiStream()
{
    std::vector<Bytes> done;
    SysExAssembler assembler;
    const Bytes first { 0xF0, 0x00, 0x32 };
    const Bytes rest { 0xF8, 0x45, 0xF7, 0x90, 0x40, 0x7F, 0xF0, 0x01, 0xF7 };
    assembler.feed(first.data(), first.size(), done);
    expect(done.empty(), "sysex waits for F7");
    assembler.feed(rest.data(), rest.size(), done);
    expect(done.size() == 2, "two messages complete");
    expect(!done.empty() && done.front() == Bytes({ 0xF0, 0x00, 0x32, 0x45, 0xF7 }), "split sysex skips clock");
    expect(done.size() == 2 && done.back() == Bytes({ 0xF0, 0x01, 0xF7 }), "note between messages is ignored");

    done.clear();
    const Bytes broken { 0xF0, 0x11, 0xB0, 0x22, 0xF7, 0xF0, 0x33, 0xF7 };
    assembler.feed(broken.data(), broken.size(), done);
    expect(done.size() == 1 && done.front() == Bytes({ 0xF0, 0x33, 0xF7 }), "status byte drops the unfinished sysex");

    done.clear();
    const Bytes empty { 0xF0, 0xF7 };
    assembler.feed(empty.data(), empty.size(), done);
    expect(done.empty(), "empty sysex is dropped");

    done.clear();
    Bytes huge(SysExAssembler::maximumSize + 10, 0x01);
    huge.front() = 0xF0;
    huge.back() = 0xF7;
    assembler.feed(huge.data(), huge.size(), done);
    expect(done.empty(), "oversized sysex is dropped");

    expect(midiname::join({ "SMC-Mixer", "-Private", "-Private" }) == "SMC-Mixer -Private", "private port name");
    expect(midiname::join({ "SMC-Mixer", "Bluetooth", "Bluetooth" }) == "SMC-Mixer Bluetooth", "bluetooth port name");
    expect(midiname::join({ "SMC-Mixer", "SMC-Mixer -Master", "-Master" }) == "SMC-Mixer -Master", "longer entity name");
    expect(midiname::join({ "(none)", "-Private", "(unknown)" }) == "-Private", "placeholders are skipped");
    expect(midiname::containsIgnoringCase("SMC-Mixer -Private", "private"), "port match ignores case");
    expect(midiname::isMixerName("SMC-Mixer") && midiname::isMixerName("smc-mixer Bluetooth")
               && midiname::isMixerName("SMC-MIXER-2F"), "mixer name match");
    expect(!midiname::isMixerName("SMC Mixer Tool") && !midiname::isMixerName("IAC Driver"), "other names");
}

void testFieldLimits()
{
    SettingsDocument document;
    document.load(Bytes(layout::snapshotSize));
    const ControlSelection knob { ControlKind::knob, 0 };
    const ControlSelection slider { ControlKind::slider, 0 };
    const ControlSelection button { ControlKind::trackButton, 0 };
    const auto edit = [&](ControlSelection control, EditableField field, std::uint8_t value) {
        return document.apply(control, DeviceProfile::ccUser, field, value);
    };
    expect(edit(knob, EditableField::channel, 15).applied, "channel 16");
    const auto channel17 = edit(knob, EditableField::channel, 16);
    expect(!channel17.applied, "channel 17 rejected");
    expect(channel17.message == "Channel must be 1 to 16.", "channel message");
    expect(!edit(knob, EditableField::cc, 128).applied, "cc 128 rejected");
    expect(!edit(button, EditableField::value2, 200).applied, "value 200 rejected");
    expect(edit(button, EditableField::led, 255).applied, "led 255");
    expect(edit(knob, EditableField::type, 3).applied, "knob type 3");
    expect(!edit(knob, EditableField::type, 4).applied, "knob type 4 rejected");
    expect(!edit(slider, EditableField::type, 3).applied, "slider type 3 rejected");
    expect(!edit(button, EditableField::mode, 6).applied, "mode 6 rejected");

    // An edit sends the whole record, so bytes the app never shows must go back unchanged.
    Bytes image(layout::snapshotSize);
    const auto record = layout::profileSize + 5 * layout::buttonSize;
    image[record + 5] = 3;
    for (std::size_t offset = 6; offset < 70; ++offset)
        image[record + offset] = static_cast<std::uint8_t>(offset);
    SettingsDocument kept;
    kept.load(image);
    expect(kept.apply({ ControlKind::trackButton, 5 }, DeviceProfile::ccUser, EditableField::cc, 99).applied, "button cc");
    const auto spans = kept.dirtySpans();
    bool untouched = spans.size() == 1 && spans.front().address == record;
    for (std::size_t offset = 5; untouched && offset < 70; ++offset)
        untouched = spans.front().data[offset] == image[record + offset];
    expect(untouched, "message and unused bytes are written back as read");
    expect(!spans.empty() && spans.front().data[2] == 99, "only the edited byte changes");
}

const Bytes knobRecord { 0x00, 0x02, 0x00, 0x1F, 0x00, 0x7F };
const auto knobAddress = static_cast<std::uint32_t>(layout::profileSize + layout::knobsOffset);

void testFrames()
{
    const Bytes writeFrame {
        0x00, 0x59, 0x22, 0x0E, 0x00, 0x00, 0x05, 0x32, 0x18, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1F, 0x00, 0x7F, 0x0A
    };
    const Bytes readFrame {
        0x00, 0x59, 0x23, 0x08, 0x00, 0x00, 0x05, 0x32, 0x18, 0x00, 0x00,
        0x06, 0x00, 0x00, 0xAA
    };
    expect(SettingsProtocol::writePayload(6194, knobRecord) == writeFrame, "write frame");
    expect(SettingsProtocol::readPayload(6194, 6) == readFrame, "read frame");
    expect(VendorProtocol::frame(0x22, Bytes(writeFrame.begin() + 6, writeFrame.end() - 1)) == writeFrame, "frame helper");

    const Bytes highBytes { 0x00, 0xff, 0x80, 0x7f };
    expect(VendorProtocol::decode7To8(VendorProtocol::encode8To7(highBytes)) == highBytes, "high-byte round trip");
    expect(VendorProtocol::decode7To8(VendorProtocol::encode8To7(writeFrame)) == writeFrame, "8-to-7 round trip");
    const auto wrapped = VendorProtocol::sysEx(writeFrame);
    expect(wrapped.front() == 0xf0 && wrapped.back() == 0xf7, "sysex start and end");
    expect(std::all_of(wrapped.begin() + 1, wrapped.end() - 1, [](std::uint8_t byte) { return byte < 0x80; }),
           "sysex body is seven-bit");
    expect(VendorProtocol::unwrapSysEx(wrapped) == writeFrame, "sysex unwraps");
    expect(!VendorProtocol::unwrapSysEx({ 0x90, 0x40, 0x7f }), "a note is not sysex");

    const auto parsed = VendorProtocol::parseFrame(writeFrame);
    expect(parsed && parsed->command == 0x22 && parsed->body.size() == 14, "frame parses");
    expect(!VendorProtocol::parseFrame({ 0x01, 0x59, 0x22, 0x00, 0x00, 0x00, 0xff }), "other header is not a frame");
    expectThrow("short frame length", [&] {
        auto shorter = writeFrame;
        shorter.pop_back();
        VendorProtocol::parseFrame(shorter);
    }, "length");
    expectThrow("bad frame checksum", [&] {
        auto bad = writeFrame;
        bad.back() ^= 1;
        VendorProtocol::parseFrame(bad);
    }, "checksum");

    Bytes little;
    VendorProtocol::appendLE(little, 0x00123456, 3);
    expect(little == Bytes({ 0x56, 0x34, 0x12 }), "little-endian append");
    expect(VendorProtocol::readLE(little, 0, 3) == 0x123456, "little-endian read");

    // Bluetooth replies arrive raw, USB-C replies as SysEx. A note and a stray SysEx are skipped.
    const auto payloads = VendorProtocol::vendorPayloads({ writeFrame, wrapped, { 0x90, 0x40, 0x7f }, { 0xf0, 0x7e, 0xf7 } });
    expect(payloads.size() == 2 && payloads[0] == writeFrame && payloads[1] == writeFrame, "vendor payloads from both links");

    const Bytes saveFrame {
        0x00, 0x59, 0x22, 0x08, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFA
    };
    // Captured from Midi Suite 1.15.0 over USB-C, right after its data write.
    const Bytes saveSysEx {
        0xF0, 0x00, 0x32, 0x09, 0x41, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7A, 0x01, 0xF7
    };
    expect(SettingsProtocol::savePayload() == saveFrame, "save frame");
    expect(VendorProtocol::sysEx(saveFrame) == saveSysEx, "save sysex");
    expectThrow("empty write", [] { SettingsProtocol::writePayload(0, {}); }, "Write length");
    expectThrow("oversized write", [] { SettingsProtocol::writePayload(0, Bytes(layout::maximumWriteSize + 1, 1)); },
                "Write length");
    expect(SettingsProtocol::writePayload(6194, knobRecord).size() == 21, "knob frame is 21 bytes");
    expect(SettingsProtocol::writePayload(0, Bytes(layout::sliderSize, 1)).size() == 20, "slider frame is 20 bytes");
    expect(SettingsProtocol::writePayload(0, Bytes(layout::buttonSize, 1)).size() == 86, "button frame is 86 bytes");

    // A button frame over a Bluetooth link that takes 20 bytes per write: 20, 20, 20, 20, 6.
    const auto frame = SettingsProtocol::writePayload(0, Bytes(layout::buttonSize, 1));
    const auto pieces = VendorProtocol::fragments(frame, 20);
    expect(pieces.size() == 5 && pieces.back().size() == 6, "a long frame is split to the write limit");
    Bytes joined;
    for (const auto& piece : pieces)
        joined.insert(joined.end(), piece.begin(), piece.end());
    expect(joined == frame, "the pieces join back into the frame");
    expect(VendorProtocol::fragments(frame, 182).size() == 1, "a frame within the limit goes whole");
    expect(VendorProtocol::fragments(frame, 0).size() == 1, "an unknown limit sends the frame whole");
}

void testAcknowledgements()
{
    using State = WriteAcknowledgement::State;
    const Bytes ack { 0x00, 0x59, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF };
    const Bytes rejected { 0x00, 0x59, 0x00, 0x01, 0x00, 0x00, 0x01, 0xFE };
    expect(SettingsProtocol::acknowledgement({ ack }).state == State::accepted, "raw acknowledgement");
    expect(SettingsProtocol::acknowledgement({ VendorProtocol::sysEx(ack) }).state == State::accepted, "sysex acknowledgement");
    const auto refusal = SettingsProtocol::acknowledgement({ rejected });
    expect(refusal.state == State::rejected && refusal.status == 0x01, "rejection carries its status");
    expect(SettingsProtocol::acknowledgement({ ack, rejected }).state == State::rejected, "a rejection outweighs an accept");
    expect(SettingsProtocol::acknowledgement({}).state == State::missing, "no reply is missing, not an error");
    expect(SettingsProtocol::acknowledgement({ settingsChunk(0, { 1 }) }).state == State::missing, "a chunk is not an acknowledgement");
    expectThrow("bad acknowledgement checksum", [ack] {
        auto bad = ack;
        bad.back() = 0x00;
        SettingsProtocol::acknowledgement({ bad });
    }, "checksum");
    expectThrow("long acknowledgement", [] { SettingsProtocol::acknowledgement({ framed(0x00, { 0x00, 0x00 }) }); },
                "unexpected length");
}

void testChunks()
{
    const auto chunk = settingsChunk(6194, knobRecord);
    expect(SettingsProtocol::findChunk({ chunk }, 6194, 6) == knobRecord, "raw chunk");
    expect(SettingsProtocol::findChunk({ VendorProtocol::sysEx(chunk) }, 6194, 6) == knobRecord, "sysex chunk");
    expect(!SettingsProtocol::findChunk({ chunk }, 6194, 5), "other length is not the chunk");
    expect(!SettingsProtocol::findChunk({ chunk }, 6200, 6), "other address is not the chunk");
    expect(!SettingsProtocol::findChunk({}, 6194, 6), "no chunk yet");
    expectThrow("conflicting chunk", [] {
        SettingsProtocol::findChunk({ settingsChunk(10, { 1 }), settingsChunk(10, { 2 }) }, 10, 1);
    }, "Conflicting");

    const auto assembled = SettingsProtocol::parsePayloads({ settingsChunk(0, { 1, 2, 3 }), settingsChunk(3, { 4 }) });
    expect(assembled.data == Bytes({ 1, 2, 3, 4 }), "chunks assemble in order");
    expect(assembled.chunkCount == 2, "chunk count");
    expectThrow("gap", [] { SettingsProtocol::parsePayloads({ settingsChunk(4, { 1 }) }); }, "missing at address");
    expectThrow("conflict", [] {
        SettingsProtocol::parsePayloads({ settingsChunk(0, { 1 }), settingsChunk(0, { 2 }) });
    }, "Conflicting");
    expectThrow("nothing read", [] { SettingsProtocol::parsePayloads({}); }, "No settings data");

    // A USB-C read: SysEx chunks out of order, a read acknowledgement, a repeat, and MIDI noise.
    const auto readAck = framed(0x23, { 0x00 });
    const auto fromUsb = SettingsProtocol::parseResponses({
        VendorProtocol::sysEx(settingsChunk(2, { 3, 4 })),
        { 0xb0, 0x07, 0x40 },
        VendorProtocol::sysEx(readAck),
        VendorProtocol::sysEx(settingsChunk(0, { 1, 2 })),
        VendorProtocol::sysEx(settingsChunk(2, { 3, 4 })),
        { 0xf0, 0x7e, 0x7f, 0x06, 0x02, 0xf7 }
    });
    expect(fromUsb.data == Bytes({ 1, 2, 3, 4 }), "sysex responses assemble");
    expect(fromUsb.chunkCount == 2, "a repeated chunk counts once");
    expect(fromUsb.acknowledgementCount == 1, "read acknowledgement counted");
    const auto fromBluetooth = SettingsProtocol::parseResponses({ settingsChunk(0, { 1, 2 }), settingsChunk(2, { 3 }) });
    expect(fromBluetooth.data == Bytes({ 1, 2, 3 }), "raw responses assemble");
}

Bytes versionFrame(const Bytes& body)
{
    return framed(0x11, body);
}

void testVersion()
{
    expectThrow("short version", [] { VendorProtocol::parseVersionPayload({ 0x00, 0x59, 0x11 }); }, "shorter");
    expectThrow("not sysex", [] { VendorProtocol::parseVersionResponse({ 0x00, 0x59 }); }, "SysEx");
    expectThrow("wrong command", [] { VendorProtocol::parseVersionPayload(framed(0x12, Bytes(9, 0x41))); }, "command");
    expectThrow("short data", [] { VendorProtocol::parseVersionPayload(versionFrame(Bytes(8, 0x41))); }, "too short");

    const std::string versionText = "SMC-Mixer_013";
    const auto payload = versionFrame(Bytes(versionText.begin(), versionText.end()));
    const auto version = VendorProtocol::parseVersionPayload(payload);
    expect(version.deviceName == "SMC-Mixer", "version device");
    expect(version.versionLabel == "V013", "version label");
    expect(version.rawPayload == payload, "version keeps the payload");
    expect(VendorProtocol::parseVersionResponse(VendorProtocol::sysEx(payload)).versionNumber == 13, "version sysex");

    // Firmware that sends the name, a zero, then the version as one digit per byte.
    const Bytes digits { 'S', 'M', 'C', 'M', 'I', 'X', 'E', 'R', 0, 1, 3, 0xff };
    const auto fallback = VendorProtocol::parseVersionPayload(versionFrame(digits));
    expect(fallback.deviceName == "SMCMIXER", "fallback device");
    expect(fallback.versionNumber == 13 && fallback.versionLabel == "V013", "fallback digits");
    expectThrow("no version", [] {
        VendorProtocol::parseVersionPayload(versionFrame({ 'S', 'M', 'C', 'M', 'I', 'X', 'E', 'R', 0x41, 0x42, 0 }));
    }, "no numeric version");
}

void testControlNames()
{
    const std::pair<int, const char*> ccNames[] {
        { 0, "Bank Select" }, { 1, "Modulation" }, { 7, "Channel Volume" }, { 16, "General Purpose 1" },
        { 31, "Undefined" }, { 40, "Balance LSB" }, { 64, "Sustain" }, { 74, "Brightness" },
        { 88, "High Resolution Velocity" }, { 91, "Reverb Depth" }, { 95, "Phaser Depth" },
        { 98, "NRPN LSB" }, { 101, "RPN MSB" }, { 102, "Undefined" }, { 119, "Undefined" },
        { 120, "All Sound Off" }, { 123, "All Notes Off" }, { 127, "Poly On" }
    };
    for (const auto& [cc, name] : ccNames)
        expect(std::string(controlChangeName(static_cast<std::uint8_t>(cc))) == name, name);
    expect(std::string(controlChangeName(128)) == "Out of range", "cc 128 name");

    expect(std::string(knobTypeName(0)) == "CC Abs" && std::string(knobTypeName(3)) == "CC Rel", "knob type names");
    expect(knobTypeName(4) == nullptr, "unknown knob type");
    expect(std::string(sliderTypeName(1)) == "Aftertouch" && std::string(sliderTypeName(2)) == "DAW", "slider type names");
    expect(std::string(buttonModeName(0)) == "CC Single" && std::string(buttonModeName(2)) == "CC Push", "button mode names");
    expect(std::string(buttonModeName(4)) == "Custom" && std::string(buttonModeName(5)) == "Note", "button custom and note");
    expect(controlDisplayName({ ControlKind::trackButton, 5 }) == "Track 2 \xc2\xb7 Solo", "track button display name");
    expect(controlDisplayName({ ControlKind::globalButton, 10 }) == "Global \xc2\xb7 Right", "global button display name");
}

void testControlList()
{
    const auto& controls = allControls();
    expect(controls.size() == layout::knobCount + layout::sliderCount + layout::buttonCount, "every control listed");
    expect(controls.front() == ControlSelection { ControlKind::knob, 0 }, "knobs first");
    expect(controls.back() == ControlSelection { ControlKind::globalButton, 10 }, "global buttons last");
    expect(std::all_of(controls.begin(), controls.end(), isValidControl), "listed controls are valid");
    expect(!isValidControl({ ControlKind::knob, 8 }) && !isValidControl({ ControlKind::trackButton, -1 }), "out of range");
    expect(buttonRecordIndex({ ControlKind::trackButton, 31 }) == 31, "track record index");
    expect(buttonRecordIndex({ ControlKind::globalButton, 0 }) == layout::trackButtonCount, "global record index");
}

void testDocumentEdits()
{
    SettingsDocument document;
    expectThrow("short snapshot", [&] { document.load(Bytes(10)); }, "6282");
    expect(!document.loaded(), "failed load stays empty");

    const ControlSelection knob { ControlKind::knob, 0 };
    const auto image = snapshotWith(knobAddress, knobRecord);
    document.load(image);
    const auto edit = [&](EditableField field, std::uint8_t value) {
        return document.apply(knob, DeviceProfile::ccUser, field, value);
    };

    expect(edit(EditableField::cc, 70).applied, "apply cc");
    expect(document.workingBytes()[knobAddress + 3] == 70, "cc byte");
    const auto daw = document.apply(knob, DeviceProfile::daw, EditableField::cc, 1);
    expect(!daw.applied && daw.message == "DAW profile is read-only.", "daw locked");
    const auto high = edit(EditableField::minimum, 200);
    expect(!high.applied && high.message == "Value must be 0 to 127.", "min 200 rejected");
    expect(edit(EditableField::maximum, 50).applied, "max 50");
    const auto inverted = edit(EditableField::minimum, 60);
    expect(!inverted.applied && inverted.message == "Min cannot exceed Max.", "min above max");
    expect(edit(EditableField::minimum, 20).applied, "min 20");
    const auto belowMinimum = edit(EditableField::maximum, 10);
    expect(!belowMinimum.applied && belowMinimum.message == "Max cannot be below Min.", "max below min");
    expect(edit(EditableField::maximum, 127).applied, "max back to 127");
    expect(edit(EditableField::minimum, 127).applied, "min may equal max");
    const auto missing = document.apply({ ControlKind::knob, 8 }, DeviceProfile::ccUser, EditableField::cc, 1);
    expect(!missing.applied && missing.message == "This field cannot be edited.", "index rejected");
    expect(!document.apply(knob, DeviceProfile::ccUser, EditableField::mode, 1).applied, "knob has no mode");

    SettingsDocument records;
    records.load(image);
    expect(records.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::type, 3).applied, "knob type");
    expect(records.workingBytes()[knobAddress] == 3, "knob type byte");
    expect(records.workingBytes()[knobAddress + 3] == 0x1F, "type edit leaves cc");
    expect(records.apply({ ControlKind::slider, 1 }, DeviceProfile::ccUser, EditableField::type, 2).applied, "slider type");
    expect(records.workingBytes()[layout::profileSize + layout::slidersOffset + layout::sliderSize] == 2, "slider type byte");
    expect(records.apply({ ControlKind::trackButton, 4 }, DeviceProfile::ccUser, EditableField::mode, 5).applied, "track mode");
    expect(records.workingBytes()[layout::profileSize + 4 * layout::buttonSize] == 5, "track mode byte");
    expect(records.apply({ ControlKind::globalButton, 1 }, DeviceProfile::ccUser, EditableField::mode, 1).applied, "global mode");
    expect(records.workingBytes()[layout::profileSize + 33 * layout::buttonSize] == 1, "global mode byte");
    expect(records.apply({ ControlKind::globalButton, 0 }, DeviceProfile::ccUser, EditableField::led, 7).applied, "global led");
    const auto ledOffset = layout::profileSize + layout::trackButtonCount * layout::buttonSize + layout::buttonSize - 1;
    expect(records.workingBytes()[ledOffset] == 7, "led byte");
    records.revert({ ControlKind::globalButton, 0 }, DeviceProfile::ccUser);
    expect(records.workingBytes()[ledOffset] == 0, "revert led");

    auto broken = image;
    broken[5] = 65;
    expectThrow("bad message length preserves the loaded image", [&] { document.load(broken); }, "Invalid DAW");
    expect(document.workingBytes()[knobAddress + 3] == 70, "previous edit remains");
}

void testRelativeKnobs()
{
    const ControlSelection knob { ControlKind::knob, 0 };
    SettingsDocument relative;
    relative.load(snapshotWith(knobAddress, knobRecord));
    const auto edit = [&](EditableField field, std::uint8_t value) {
        return relative.apply(knob, DeviceProfile::ccUser, field, value);
    };
    expect(edit(EditableField::type, knobTypeRelative).applied, "relative type");
    expect(edit(EditableField::minimum, 65).applied, "relative ccw value");
    expect(edit(EditableField::maximum, 1).applied, "relative cw value below ccw");

    // Leaving CC Rel with CCW 65 and CW 1 swaps them, so Min stays editable.
    const auto back = edit(EditableField::type, 0);
    expect(back.applied, "back to cc");
    expect(back.message.find("swapped") != std::string::npos, "the swap is reported");
    expect(relative.workingBytes()[knobAddress + 4] == 1 && relative.workingBytes()[knobAddress + 5] == 65, "min and max swapped");
    expect(edit(EditableField::minimum, 30).applied, "min editable after the swap");
    expect(!edit(EditableField::minimum, 66).applied, "cc range checked again");

    // A pair already inverted on the mixer can still move back toward a range.
    auto inverted = snapshotWith(knobAddress, { 0x00, 0x02, 0x00, 0x1F, 0x60, 0x10 });
    SettingsDocument stored;
    stored.load(inverted);
    const auto profile = stored.configuration().profile(DeviceProfile::ccUser);
    const auto minimumRange = allowedRange(profile, knob, EditableField::minimum);
    expect(minimumRange.minimum == 0 && minimumRange.maximum == 0x60, "inverted min can only fall");
    const auto maximumRange = allowedRange(profile, knob, EditableField::maximum);
    expect(maximumRange.minimum == 0x10 && maximumRange.maximum == 127, "inverted max can only rise");
    expect(stored.apply(knob, DeviceProfile::ccUser, EditableField::minimum, 0x20).applied, "inverted min moves down");
}

void testFieldRanges()
{
    SettingsDocument document;
    document.load(snapshotWith(knobAddress, { 0x00, 0x02, 0x00, 0x1F, 0x10, 0x60 }));
    const auto profile = document.configuration().profile(DeviceProfile::ccUser);
    const ControlSelection knob { ControlKind::knob, 0 };
    const auto minimum = allowedRange(profile, knob, EditableField::minimum);
    const auto maximum = allowedRange(profile, knob, EditableField::maximum);
    expect(minimum.minimum == 0 && minimum.maximum == 0x60, "min is bounded by max");
    expect(maximum.minimum == 0x10 && maximum.maximum == 127, "max is bounded by min");
    expect(allowedRange(profile, knob, EditableField::channel).maximum == 15, "channel range");
    expect(allowedRange(profile, { ControlKind::trackButton, 0 }, EditableField::led).maximum == 255, "led range");
    expect(fieldLimits(ControlKind::knob, EditableField::type).maximum == 3, "four knob types");
    expect(fieldLimits(ControlKind::slider, EditableField::type).maximum == 2, "three fader types");
    expect(fieldValue(profile, knob, EditableField::cc) == 0x1F, "field value");
    expect(!hasField(ControlKind::slider, EditableField::led) && hasField(ControlKind::globalButton, EditableField::led),
           "fields by kind");

    SettingsDocument relative;
    relative.load(snapshotWith(knobAddress, { 0x03, 0x02, 0x00, 0x1F, 0x41, 0x01 }));
    const auto relativeProfile = relative.configuration().profile(DeviceProfile::ccUser);
    expect(allowedRange(relativeProfile, knob, EditableField::minimum).maximum == 127, "relative min is free");
    expect(allowedRange(relativeProfile, knob, EditableField::maximum).minimum == 0, "relative max is free");
}

void testUndo()
{
    const ControlSelection knob { ControlKind::knob, 0 };
    SettingsDocument document;
    document.load(snapshotWith(knobAddress, knobRecord));
    expect(!document.canUndo() && !document.canRedo(), "fresh document has no history");

    document.apply(knob, DeviceProfile::ccUser, EditableField::cc, 40);
    // One drag across several values is one step.
    document.apply(knob, DeviceProfile::ccUser, EditableField::channel, 1);
    document.apply(knob, DeviceProfile::ccUser, EditableField::channel, 2, true);
    document.apply(knob, DeviceProfile::ccUser, EditableField::channel, 3, true);
    expect(document.workingBytes()[knobAddress + 2] == 3, "drag ends on channel 4");
    expect(document.undo(), "undo drag");
    expect(document.workingBytes()[knobAddress + 2] == 0, "drag undone in one step");
    expect(document.workingBytes()[knobAddress + 3] == 40, "earlier edit stays");
    expect(document.redo(), "redo drag");
    expect(document.workingBytes()[knobAddress + 2] == 3, "drag redone");
    expect(document.undo() && document.undo(), "undo both");
    expect(!document.isDirty() && !document.canUndo(), "back to the read image");
    expect(document.redo(), "redo after undo");
    document.apply(knob, DeviceProfile::ccUser, EditableField::maximum, 100);
    expect(!document.canRedo(), "a new edit clears redo");

    // A continuing gesture on another field starts a new step.
    document.apply(knob, DeviceProfile::ccUser, EditableField::minimum, 5, true);
    expect(document.undo() && document.workingBytes()[knobAddress + 4] == 0, "minimum undone alone");

    // Unchanged values add no step.
    const auto before = document.canUndo();
    document.apply(knob, DeviceProfile::ccUser, EditableField::cc, document.workingBytes()[knobAddress + 3]);
    expect(document.canUndo() == before, "same value adds no step");

    document.revertAll();
    expect(!document.isDirty(), "revert all");
    expect(document.undo() && document.isDirty(), "revert all is undoable");
    document.revert(knob, DeviceProfile::ccUser);
    expect(!document.isRecordDirty(knob, DeviceProfile::ccUser), "revert control");
    expect(document.undo() && document.isRecordDirty(knob, DeviceProfile::ccUser), "revert control is undoable");

    for (int step = 0; step < static_cast<int>(SettingsDocument::undoLimit) + 20; ++step)
        document.apply(knob, DeviceProfile::ccUser, EditableField::cc, static_cast<std::uint8_t>(step % 2 == 0 ? 10 : 11));
    int undone = 0;
    while (document.undo())
        ++undone;
    expect(undone == static_cast<int>(SettingsDocument::undoLimit), "history is capped");

    document.load(snapshotWith(knobAddress, knobRecord));
    expect(!document.canUndo() && !document.canRedo(), "load clears history");
}

void testDirtyTracking()
{
    SettingsDocument document;
    document.load(snapshotWith(knobAddress, knobRecord));
    document.apply({ ControlKind::globalButton, 2 }, DeviceProfile::ccUser, EditableField::cc, 9);
    document.apply({ ControlKind::slider, 3 }, DeviceProfile::ccUser, EditableField::cc, 90);
    document.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::cc, 70);
    document.apply({ ControlKind::trackButton, 0 }, DeviceProfile::ccUser, EditableField::cc, 20);

    const auto dirty = document.dirtyControls();
    expect(dirty == std::vector<ControlSelection>({ { ControlKind::knob, 0 }, { ControlKind::slider, 3 },
                                                    { ControlKind::trackButton, 0 }, { ControlKind::globalButton, 2 } }),
           "dirty controls in list order");

    const auto spans = document.dirtySpans();
    expect(spans.size() == 4, "one span per changed record");
    expect(std::is_sorted(spans.begin(), spans.end(), [](const SettingsSpan& left, const SettingsSpan& right) {
               return left.address < right.address;
           }), "spans in address order");
    expect(spans.front().address == layout::profileSize && spans.front().data[2] == 20, "button record address and cc");
    expect(spans[2].address == knobAddress && spans[2].data.size() == layout::knobSize, "knob address and size");
    const auto sliderAddress = layout::profileSize + layout::slidersOffset + 3 * layout::sliderSize;
    expect(spans[3].address == sliderAddress && spans[3].data.size() == layout::sliderSize, "slider address and size");
    SettingsWriter::validate(spans);

    SettingsSpan mismatched = spans.back();
    mismatched.data.assign(layout::sliderSize, 9);
    expectThrow("verify is all or nothing", [&] { document.noteVerified({ spans.front(), mismatched }); }, "do not match");
    expect(document.dirtySpans().size() == 4, "failed verify keeps every edit");
    document.noteVerified(spans);
    expect(!document.isDirty(), "verified records are clean");
}

void testDescriptions()
{
    auto image = Bytes(layout::snapshotSize);
    const auto user = layout::profileSize;
    const auto knobAt = [&](int index) { return user + layout::knobsOffset + static_cast<std::size_t>(index) * layout::knobSize; };
    const auto sliderAt = [&](int index) { return user + layout::slidersOffset + static_cast<std::size_t>(index) * layout::sliderSize; };
    const auto buttonAt = [&](int index) { return user + static_cast<std::size_t>(index) * layout::buttonSize; };
    const auto put = [&](std::size_t offset, Bytes bytes) { std::copy(bytes.begin(), bytes.end(), image.begin() + static_cast<std::ptrdiff_t>(offset)); };
    put(knobAt(0), { 0, 2, 0, 102, 10, 100 });
    put(knobAt(1), { 3, 2, 1, 16, 65, 1 });
    put(knobAt(2), { 1, 2, 2, 0, 0, 127 });
    put(knobAt(3), { 2, 2, 3, 0, 0, 127 });
    put(sliderAt(0), { 0, 0, 7, 0, 127 });
    put(sliderAt(1), { 2, 4, 0, 0, 127 });
    put(buttonAt(0), { 1, 0, 20, 0, 127 });
    put(buttonAt(1), { 3, 0, 0, 2, 9 });
    put(buttonAt(2), { 5, 9, 60, 0, 0 });
    put(buttonAt(3), { 4, 0, 0, 0, 0, 3, 0xf0, 0x7f, 0xf7 });
    const auto profile = ControlConfiguration::parse(image).profile(DeviceProfile::ccUser);

    const auto absolute = summarize(profile, { ControlKind::knob, 0 });
    expect(absolute.primary == "102" && absolute.secondary == "Abs \xc2\xb7 Ch 1", "absolute knob summary");
    expect(absolute.hasRange && absolute.rangeMinimum == 10 && absolute.rangeMaximum == 100, "absolute knob range");
    const auto relative = summarize(profile, { ControlKind::knob, 1 });
    expect(relative.relative && !relative.hasRange && relative.secondary == "Rel \xc2\xb7 Ch 2", "relative knob summary");
    expect(summarize(profile, { ControlKind::knob, 2 }).primary == "AT", "aftertouch knob summary");
    expect(summarize(profile, { ControlKind::knob, 3 }).primary == "PB", "note knob summary");
    expect(summarize(profile, { ControlKind::slider, 0 }).primary == "CC 7", "cc fader summary");
    expect(summarize(profile, { ControlKind::slider, 1 }).primary == "PB"
               && summarize(profile, { ControlKind::slider, 1 }).secondary == "Ch 5", "daw fader summary");
    expect(summarize(profile, { ControlKind::trackButton, 0 }).primary == "CC 20", "toggle button summary");
    expect(summarize(profile, { ControlKind::trackButton, 1 }).primary == "PC 9", "program button summary");
    expect(summarize(profile, { ControlKind::trackButton, 2 }).primary == "Note 60", "note button summary");
    expect(summarize(profile, { ControlKind::trackButton, 3 }).primary == "SysEx", "custom button summary");

    expect(meaningFor(profile, { ControlKind::knob, 1 }).minimumName == "CCW value", "relative field names");
    // Measured with Min 40 and Max 80: Note knobs and DAW faders keep Pitch Bend inside the range.
    const auto noteKnob = summarize(profile, { ControlKind::knob, 3 });
    expect(noteKnob.hasRange && noteKnob.rangeMinimum == 0 && noteKnob.rangeMaximum == 127, "note knob shows its range");
    expect(summarize(profile, { ControlKind::slider, 1 }).hasRange, "daw fader shows its range");
    expect(controlNote(profile, { ControlKind::knob, 3 }).find("Pitch Bend between Min and Max") != std::string::npos,
           "note knob note names the range");
    expect(describe(profile, { ControlKind::knob, 3 }).find("Min 0") != std::string::npos, "note knob description lists Min");
    expect(!meaningFor(profile, { ControlKind::trackButton, 2 }).value1Used, "note button values unused");
    expect(meaningFor(profile, { ControlKind::trackButton, 1 }).value2Name == "Program", "program field names");
    expect(controlNote(profile, { ControlKind::trackButton, 3 }) == "Each press sends F0 7F F7.", "custom message note");
    expect(controlNote(profile, { ControlKind::knob, 0 }).find("between Min and Max") != std::string::npos, "knob note");

    const auto text = describe(profile, { ControlKind::knob, 1 });
    expect(text.find("Knob 2") == 0, "description starts with the name");
    expect(text.find("CC Rel") != std::string::npos && text.find("Channel 2") != std::string::npos, "description type and channel");
    expect(text.find("CCW value 65") != std::string::npos && text.find("CW value 1") != std::string::npos, "description values");
    expect(describe(profile, { ControlKind::knob, 2 }).find("CC ") == std::string::npos, "unused cc left out");
    expect(describe(profile, { ControlKind::trackButton, 0 }).find("Off value 0") != std::string::npos, "button values described");
}

// A mixer answering reads of its image, with faults to inject.
struct FakeReader
{
    Bytes image = Bytes(layout::snapshotSize);
    std::vector<std::uint32_t> requests;
    int dropAt = -1;          // address whose first request gets no reply
    int dropCount = 0;        // how many requests at that address are dropped
    bool throwOnce = false;   // the first request throws, as a Bluetooth timeout does
    bool garbleFirst = false; // the first reply has a bad checksum

    std::vector<Bytes> answer(std::uint32_t address, std::uint32_t length)
    {
        requests.push_back(address);
        if (throwOnce)
        {
            throwOnce = false;
            throw std::runtime_error("Timed out waiting for the AE42 vendor reply");
        }
        if (static_cast<int>(address) == dropAt && dropCount > 0)
        {
            --dropCount;
            return { framed(0x23, { 0x00 }) };
        }
        Bytes data(image.begin() + address, image.begin() + address + length);
        auto reply = settingsChunk(address, data);
        if (garbleFirst)
        {
            garbleFirst = false;
            reply.back() ^= 0xff;
        }
        return { VendorProtocol::sysEx(reply) };
    }

    SettingsReader::Exchange exchange()
    {
        return [this](std::uint32_t address, std::uint32_t length) { return answer(address, length); };
    }
};

void testReader()
{
    FakeReader mixer;
    for (std::size_t index = 0; index < mixer.image.size(); ++index)
        mixer.image[index] = static_cast<std::uint8_t>(index * 7);
    // Keep every button message length valid, as a real image has.
    for (std::size_t profile = 0; profile < layout::profileCount; ++profile)
        for (std::size_t button = 0; button < layout::buttonCount; ++button)
            mixer.image[profile * layout::profileSize + button * layout::buttonSize + 5] = 3;

    std::vector<double> progress;
    const auto result = SettingsReader::read(mixer.exchange(), [&](double value) { progress.push_back(value); });
    expect(result && result->data == mixer.image, "reader assembles the image");
    expect(result && result->chunkCount == 7, "seven chunks");
    expect(mixer.requests.size() == 7, "one request per chunk");
    expect(!progress.empty() && progress.back() == 1.0 && std::is_sorted(progress.begin(), progress.end()), "read progress");

    FakeReader dropped = mixer;
    dropped.requests.clear();
    dropped.dropAt = static_cast<int>(layout::readChunkSize * 3);
    dropped.dropCount = 1;
    const auto retried = SettingsReader::read(dropped.exchange());
    expect(retried && retried->data == mixer.image, "a dropped chunk is read again");
    expect(dropped.requests.size() == 8, "only the dropped chunk is asked twice");
    expect(retried && retried->acknowledgementCount == 0, "the empty reply is not kept");

    FakeReader timedOut = mixer;
    timedOut.throwOnce = true;
    expect(SettingsReader::read(timedOut.exchange()).has_value(), "a link timeout is retried");

    FakeReader garbled = mixer;
    garbled.garbleFirst = true;
    expect(SettingsReader::read(garbled.exchange()).has_value(), "a corrupt reply is retried");

    FakeReader lost = mixer;
    lost.dropAt = static_cast<int>(layout::readChunkSize);
    lost.dropCount = SettingsReader::attemptsPerChunk;
    expectThrow("a chunk that never arrives", [&] { SettingsReader::read(lost.exchange()); }, "at address 1009");

    FakeReader cancelled = mixer;
    int checks = 0;
    expect(!SettingsReader::read(cancelled.exchange(), {}, [&] { return ++checks > 3; }), "cancel stops the read");
}

void testSerialization()
{
    Bytes image(layout::snapshotSize);
    for (std::size_t index = 0; index < image.size(); ++index)
        image[index] = static_cast<std::uint8_t>((index * 31) ^ (index >> 3));
    for (std::size_t profile = 0; profile < layout::profileCount; ++profile)
        for (std::size_t button = 0; button < layout::buttonCount; ++button)
            image[profile * layout::profileSize + button * layout::buttonSize + 5] = static_cast<std::uint8_t>(button % 65);

    const auto configuration = ControlConfiguration::parse(image);
    auto rebuilt = image;
    for (const auto profile : { DeviceProfile::daw, DeviceProfile::ccUser })
        ControlConfiguration::writeProfile(configuration.profile(profile), profile, rebuilt);
    expect(rebuilt == image, "writing a parsed profile gives back the same bytes");

    auto edited = configuration.profile(DeviceProfile::ccUser);
    edited.knobs[2].cc = 99;
    edited.buttons[40].dawMessage = { 0xf0, 0x01, 0xf7 };
    ControlConfiguration::writeProfile(edited, DeviceProfile::ccUser, rebuilt);
    const auto reparsed = ControlConfiguration::parse(rebuilt);
    expect(reparsed.profile(DeviceProfile::ccUser).knobs[2].cc == 99, "written knob cc");
    expect(reparsed.profile(DeviceProfile::ccUser).buttons[40].dawMessage == Bytes({ 0xf0, 0x01, 0xf7 }), "written message");
    expect(std::equal(image.begin(), image.begin() + static_cast<std::ptrdiff_t>(layout::profileSize), rebuilt.begin()),
           "the other profile is untouched");

    auto tooLong = edited;
    tooLong.buttons[0].dawMessage = Bytes(layout::buttonMessageCapacity + 1, 1);
    expectThrow("long message", [&] { ControlConfiguration::writeProfile(tooLong, DeviceProfile::ccUser, rebuilt); }, "longer");
}

void testImport()
{
    const auto image = snapshotWith(knobAddress, knobRecord);
    SettingsDocument document;
    expect(!document.importImage(image).applied, "nothing to import into before a read");
    document.load(image);

    auto profile = document.configuration().profile(DeviceProfile::ccUser);
    profile.knobs[0].cc = 64;
    profile.sliders[2].maximum = 100;
    profile.buttons[33].mode = 3;
    const auto imported = document.importProfile(profile);
    expect(imported.applied && imported.message.find("3 records differ") != std::string::npos, "profile imported");
    expect(document.workingBytes()[knobAddress + 3] == 64, "imported cc");
    expect(document.dirtySpans().size() == 3, "three records to write");
    expect(document.undo() && !document.isDirty(), "an import is one undo step");
    expect(document.redo() && document.dirtySpans().size() == 3, "and redo brings it back");

    auto same = document.configuration().profile(DeviceProfile::ccUser);
    expect(!document.importProfile(same).applied, "an identical profile changes nothing");

    auto badChannel = same;
    badChannel.sliders[0].channel = 16;
    const auto refused = document.importProfile(badChannel);
    expect(!refused.applied && refused.message.find("Fader 1") != std::string::npos, "out-of-range channel refused");
    auto inverted = same;
    inverted.knobs[3].minimum = 100;
    inverted.knobs[3].maximum = 10;
    expect(!document.importProfile(inverted).applied, "a new inverted range is refused");
    auto relative = inverted;
    relative.knobs[3].type = knobTypeRelative;
    expect(document.importProfile(relative).applied, "a relative knob may send a higher CCW value");

    // A whole image: the CC / User profile comes in byte for byte, the DAW profile stays.
    SettingsDocument fresh;
    fresh.load(image);
    auto file = image;
    file[layout::profileSize + layout::slidersOffset + 2] = 21;
    file[3] = 0x55;
    const auto fromImage = fresh.importImage(file);
    expect(fromImage.applied && fromImage.message.find("DAW profile was left out") != std::string::npos, "image imported");
    expect(fresh.workingBytes()[layout::profileSize + layout::slidersOffset + 2] == 21, "image fader cc");
    expect(fresh.workingBytes()[3] == image[3], "DAW profile unchanged");
    SettingsWriter::validate(fresh.dirtySpans());
    expect(!fresh.importImage(Bytes(100)).applied, "short image refused");
    auto broken = file;
    broken[layout::profileSize + 5] = 90;
    expect(!fresh.importImage(broken).applied, "image with a bad message length refused");
}

void testJson()
{
    auto exported = snapshotWith(knobAddress, knobRecord);
    const auto messageAt = layout::profileSize;
    exported[messageAt + 5] = 3;
    exported[messageAt + 6] = 0xf0;
    exported[messageAt + 7] = 0x7f;
    exported[messageAt + 8] = 0xf7;
    const auto json = ControlConfiguration::parse(exported).toJson(DeviceProfile::ccUser);
    expect(json.find("\"profile\": \"CC / User\"") != std::string::npos, "json profile");
    expect(json.find("\"channel\": 1") != std::string::npos, "json channel is one-based");
    expect(json.find("\"cc\": 31") != std::string::npos, "json knob cc");
    expect(json.find("\"message\": \"F0 7F F7\"") != std::string::npos, "json message");
    expect(json.find("\"name\": \"Right\"") != std::string::npos, "json last global button");
    const auto dawJson = ControlConfiguration::parse(exported).toJson(DeviceProfile::daw);
    expect(dawJson.find("\"profile\": \"DAW\"") != std::string::npos, "json daw profile");
    expect(dawJson.find("\"cc\": 31") == std::string::npos, "daw json omits the cc user cc");
}
}

int main()
{
    testFrames();
    testAcknowledgements();
    testChunks();
    testVersion();
    testControlNames();
    testControlList();
    testDocumentEdits();
    testRelativeKnobs();
    testFieldRanges();
    testUndo();
    testDirtyTracking();
    testDescriptions();
    testReader();
    testSerialization();
    testImport();
    testJson();
    testWriter();
    testMidiStream();
    testFieldLimits();

    if (failures != 0)
    {
        std::cerr << failures << " failed" << std::endl;
        return 1;
    }
    std::cout << "core-tests-ok" << std::endl;
    return 0;
}
