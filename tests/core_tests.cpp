#include "ControlConfiguration.h"
#include "MidiStream.h"
#include "SettingsDocument.h"
#include "SettingsProtocol.h"
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
                    "was not written");
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
}

void testFieldLimits()
{
    SettingsDocument document;
    document.load(Bytes(layout::snapshotSize));
    std::string error;
    const ControlSelection knob { ControlKind::knob, 0 };
    const ControlSelection slider { ControlKind::slider, 0 };
    const ControlSelection button { ControlKind::trackButton, 0 };
    expect(document.apply(knob, DeviceProfile::ccUser, EditableField::channel, 15, error), "channel 16");
    expect(!document.apply(knob, DeviceProfile::ccUser, EditableField::channel, 16, error), "channel 17 rejected");
    expect(error == "Channel must be 1 to 16.", "channel message");
    expect(!document.apply(knob, DeviceProfile::ccUser, EditableField::cc, 128, error), "cc 128 rejected");
    expect(!document.apply(button, DeviceProfile::ccUser, EditableField::value2, 200, error), "value 200 rejected");
    expect(document.apply(button, DeviceProfile::ccUser, EditableField::led, 255, error), "led 255");
    expect(document.apply(knob, DeviceProfile::ccUser, EditableField::type, 3, error), "knob type 3");
    expect(!document.apply(knob, DeviceProfile::ccUser, EditableField::type, 4, error), "knob type 4 rejected");
    expect(!document.apply(slider, DeviceProfile::ccUser, EditableField::type, 3, error), "slider type 3 rejected");
    expect(!document.apply(button, DeviceProfile::ccUser, EditableField::mode, 6, error), "mode 6 rejected");

    // An edit sends the whole record, so bytes the app never shows must go back unchanged.
    Bytes image(layout::snapshotSize);
    const auto record = layout::profileSize + 5 * layout::buttonSize;
    image[record + 5] = 3;
    for (std::size_t offset = 6; offset < 70; ++offset)
        image[record + offset] = static_cast<std::uint8_t>(offset);
    SettingsDocument kept;
    kept.load(image);
    expect(kept.apply({ ControlKind::trackButton, 5 }, DeviceProfile::ccUser, EditableField::cc, 99, error), "button cc");
    const auto spans = kept.dirtySpans();
    bool untouched = spans.size() == 1 && spans.front().address == record;
    for (std::size_t offset = 5; untouched && offset < 70; ++offset)
        untouched = spans.front().data[offset] == image[record + offset];
    expect(untouched, "message and unused bytes are written back as read");
    expect(!spans.empty() && spans.front().data[2] == 99, "only the edited byte changes");
}
}

int main()
{
    const Bytes knob { 0x00, 0x02, 0x00, 0x1F, 0x00, 0x7F };
    const Bytes writeFrame {
        0x00, 0x59, 0x22, 0x0E, 0x00, 0x00, 0x05, 0x32, 0x18, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1F, 0x00, 0x7F, 0x0A
    };
    const Bytes readFrame {
        0x00, 0x59, 0x23, 0x08, 0x00, 0x00, 0x05, 0x32, 0x18, 0x00, 0x00,
        0x06, 0x00, 0x00, 0xAA
    };
    expect(SettingsProtocol::writePayload(6194, knob) == writeFrame, "write frame");
    expect(SettingsProtocol::readPayload(5, 6194, 6) == readFrame, "read frame");
    const Bytes highBytes { 0x00, 0xff, 0x80, 0x7f };
    expect(VendorProtocol::decode7To8(VendorProtocol::encode8To7(highBytes)) == highBytes, "high-byte round trip");
    expect(VendorProtocol::decode7To8(VendorProtocol::encode8To7(writeFrame)) == writeFrame, "8-to-7 round trip");
    expect(SettingsProtocol::sysEx(writeFrame).front() == 0xf0, "sysex start");
    expect(SettingsProtocol::sysEx(writeFrame).back() == 0xf7, "sysex end");

    const Bytes ack { 0x00, 0x59, 0x00, 0x01, 0x00, 0x00, 0x00, 0xFF };
    const Bytes chunk {
        0x00, 0x59, 0x23, 0x0E, 0x00, 0x00, 0x05, 0x32, 0x18, 0x00, 0x00,
        0x06, 0x00, 0x00, 0x00, 0x02, 0x00, 0x1F, 0x00, 0x7F, 0x0A
    };
    SettingsProtocol::parseWriteAcknowledgement({ ack });
    SettingsProtocol::parseWriteAcknowledgement({ SettingsProtocol::sysEx(ack) });
    expect(SettingsProtocol::parseChunk({ chunk }, 6194, 6) == knob, "raw chunk");
    expect(SettingsProtocol::parseChunk({ SettingsProtocol::sysEx(chunk) }, 6194, 6) == knob, "sysex chunk");
    expectThrow("rejected write", [] {
        const Bytes rejected { 0x00, 0x59, 0x00, 0x01, 0x00, 0x00, 0x01, 0xFE };
        SettingsProtocol::parseWriteAcknowledgement({ rejected });
    }, "rejected the write");
    expectThrow("missing acknowledgement", [] {
        SettingsProtocol::parseWriteAcknowledgement({});
    }, "No write acknowledgement");
    expectThrow("bad acknowledgement checksum", [ack] {
        auto bad = ack;
        bad.back() = 0x00;
        SettingsProtocol::parseWriteAcknowledgement({ bad });
    }, "checksum");
    const std::pair<int, const char*> ccNames[] {
        { 0, "Bank Select" }, { 1, "Modulation" }, { 7, "Channel Volume" }, { 16, "General Purpose 1" },
        { 31, "Undefined" }, { 40, "Balance LSB" }, { 64, "Sustain" }, { 74, "Brightness" },
        { 88, "High Resolution Velocity" }, { 91, "Reverb Depth" }, { 95, "Phaser Depth" },
        { 98, "NRPN LSB" }, { 101, "RPN MSB" }, { 102, "Undefined" }, { 119, "Undefined" },
        { 120, "All Sound Off" }, { 123, "All Notes Off" }, { 127, "Poly On" }
    };
    for (const auto& [cc, name] : ccNames)
        expect(std::string(controlChangeName(static_cast<std::uint8_t>(cc))) == name, name);

    expectThrow("empty write", [] { SettingsProtocol::writePayload(0, {}); }, "Write length");
    // Captured from Midi Suite 1.15.0 over USB-C, right after its data write.
    const Bytes saveFrame {
        0x00, 0x59, 0x22, 0x08, 0x00, 0x00, 0x05, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xFA
    };
    const Bytes saveSysEx {
        0xF0, 0x00, 0x32, 0x09, 0x41, 0x00, 0x00, 0x40, 0x02, 0x00,
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x7A, 0x01, 0xF7
    };
    expect(SettingsProtocol::savePayload() == saveFrame, "save frame");
    expect(SettingsProtocol::sysEx(saveFrame) == saveSysEx, "save sysex");
    expectThrow("oversized write", [] { SettingsProtocol::writePayload(0, Bytes(257, 1)); }, "Write length");
    expect(SettingsProtocol::writePayload(6194, knob).size() == 21, "knob frame is 21 bytes");
    expect(SettingsProtocol::writePayload(0, Bytes(layout::sliderSize, 1)).size() == 20, "slider frame is 20 bytes");
    expect(SettingsProtocol::writePayload(0, Bytes(layout::buttonSize, 1)).size() == 86, "button frame is 86 bytes");

    const auto assembled = SettingsProtocol::parsePayloads({
        settingsChunk(0, { 1, 2, 3 }),
        settingsChunk(3, { 4 })
    });
    expect(assembled.data == Bytes({ 1, 2, 3, 4 }), "chunks assemble in order");
    expect(assembled.chunkCount == 2, "chunk count");
    expectThrow("gap", [] { SettingsProtocol::parsePayloads({ settingsChunk(4, { 1 }) }); }, "missing at address");
    expectThrow("conflict", [] {
        SettingsProtocol::parsePayloads({ settingsChunk(0, { 1 }), settingsChunk(0, { 2 }) });
    }, "Conflicting");
    expectThrow("short version", [] { VendorProtocol::parseVersionPayload({ 0x00, 0x59, 0x11 }); }, "shorter");

    const std::string versionText = "SMC-Mixer_013";
    Bytes versionBody(versionText.begin(), versionText.end());
    Bytes versionPayload { 0x00, 0x59, 0x11, static_cast<std::uint8_t>(versionBody.size()), 0x00, 0x00 };
    versionPayload.insert(versionPayload.end(), versionBody.begin(), versionBody.end());
    versionPayload.push_back(VendorProtocol::checksum(versionBody));
    const auto version = VendorProtocol::parseVersionPayload(versionPayload);
    expect(version.deviceName == "SMC-Mixer", "version device");
    expect(version.versionLabel == "V013", "version label");
    const auto wrapped = VendorProtocol::parseVersionResponse(SettingsProtocol::sysEx(versionPayload));
    expect(wrapped.versionNumber == 13, "version sysex");

    SettingsDocument document;
    expectThrow("short snapshot", [&] { document.load(Bytes(10)); }, "6282");
    expect(!document.loaded(), "failed load stays empty");

    auto image = snapshotWith(layout::profileSize + layout::knobsOffset, knob);
    document.load(image);
    std::string error;
    expect(document.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::cc, 70, error), "apply cc");
    expect(error.empty(), "apply error cleared");
    expect(document.workingBytes()[layout::profileSize + layout::knobsOffset + 3] == 70, "cc byte");
    expect(!document.apply({ ControlKind::knob, 0 }, DeviceProfile::daw, EditableField::cc, 1, error), "daw locked");
    expect(error == "DAW profile is read-only.", "daw message");
    expect(!document.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::minimum, 200, error), "min 200 rejected");
    expect(error == "Value must be 0 to 127.", "range message");
    expect(document.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::maximum, 50, error), "max 50");
    expect(!document.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::minimum, 60, error), "min above max");
    expect(error == "Min cannot exceed Max.", "min message");
    expect(document.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::maximum, 127, error), "max back to 127");
    expect(document.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::minimum, 127, error), "min may equal max");
    expect(!document.apply({ ControlKind::knob, 8 }, DeviceProfile::ccUser, EditableField::cc, 1, error), "index rejected");

    expect(error == "This field cannot be edited.", "index message");
    SettingsDocument relative;
    relative.load(image);
    expect(relative.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::type, knobTypeRelative, error), "relative type");
    expect(relative.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::minimum, 65, error), "relative ccw value");
    expect(relative.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::maximum, 1, error), "relative cw value below ccw");
    expect(relative.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::type, 0, error), "back to cc");
    expect(!relative.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::minimum, 66, error), "cc range checked again");

    const auto knobOffset = layout::profileSize + layout::knobsOffset;
    SettingsSpan matching;
    matching.address = static_cast<std::uint32_t>(knobOffset);
    matching.data.assign(document.workingBytes().begin() + static_cast<std::ptrdiff_t>(knobOffset),
                         document.workingBytes().begin() + static_cast<std::ptrdiff_t>(knobOffset + layout::knobSize));
    SettingsSpan mismatched = matching;
    mismatched.address = static_cast<std::uint32_t>(layout::profileSize + layout::slidersOffset);
    mismatched.data.assign(layout::sliderSize, 9);
    expectThrow("verify is all or nothing", [&] { document.noteVerified({ matching, mismatched }); }, "do not match");
    expect(document.isDirty(), "failed verify keeps the edit");
    expect(document.workingBytes()[knobOffset + 3] == 70, "failed verify keeps the cc");

    const auto spans = document.dirtySpans();
    expect(spans.size() == 1, "one dirty record");
    expect(!spans.empty() && spans.front().address == layout::profileSize + layout::knobsOffset, "knob address");
    expect(!spans.empty() && spans.front().data.size() == layout::knobSize, "knob record size");
    document.noteVerified(spans);
    expect(!document.isDirty(), "verified record is clean");

    expect(document.apply({ ControlKind::globalButton, 0 }, DeviceProfile::ccUser, EditableField::led, 7, error), "global led");
    const auto ledOffset = layout::profileSize + 32 * layout::buttonSize + 70;
    expect(document.workingBytes()[ledOffset] == 7, "led byte");
    document.revert({ ControlKind::globalButton, 0 }, DeviceProfile::ccUser);
    expect(document.workingBytes()[ledOffset] == 0, "revert led");

    auto broken = image;
    broken[5] = 65;
    expectThrow("bad message length preserves the loaded image", [&] { document.load(broken); }, "Invalid DAW");
    expect(document.workingBytes()[layout::profileSize + layout::knobsOffset + 3] == 70, "previous edit remains");

    SettingsDocument records;
    records.load(image);
    expect(records.apply({ ControlKind::slider, 3 }, DeviceProfile::ccUser, EditableField::cc, 90, error), "slider edit");
    auto recordSpans = records.dirtySpans();
    const auto sliderAddress = layout::profileSize + layout::slidersOffset + 3 * layout::sliderSize;
    expect(recordSpans.size() == 1 && recordSpans.front().address == sliderAddress, "slider address");
    expect(recordSpans.size() == 1 && recordSpans.front().data.size() == layout::sliderSize, "slider record size");
    expect(records.apply({ ControlKind::trackButton, 0 }, DeviceProfile::ccUser, EditableField::cc, 20, error), "button edit");
    recordSpans = records.dirtySpans();
    expect(recordSpans.size() == 2, "slider and button are separate records");
    bool sawButton = false;
    for (const auto& span : recordSpans)
    {
        if (span.data.size() != layout::buttonSize)
            continue;
        sawButton = span.address == layout::profileSize && span.data[2] == 20;
    }
    expect(sawButton, "button record address and cc");
    expect(!records.apply({ ControlKind::slider, 0 }, DeviceProfile::daw, EditableField::minimum, 1, error), "daw slider locked");
    expect(std::string(knobTypeName(0)) == "CC Abs" && std::string(knobTypeName(3)) == "CC Rel", "knob type names");
    expect(knobTypeName(4) == nullptr, "unknown knob type");
    expect(std::string(sliderTypeName(1)) == "Aftertouch" && std::string(sliderTypeName(2)) == "DAW", "slider type names");
    expect(std::string(buttonModeName(0)) == "CC Single" && std::string(buttonModeName(2)) == "CC Push", "button mode names");
    expect(std::string(buttonModeName(4)) == "Custom" && std::string(buttonModeName(5)) == "Note", "button custom and note");
    expect(records.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::type, 3, error), "knob type");
    expect(records.workingBytes()[layout::profileSize + layout::knobsOffset] == 3, "knob type byte");
    expect(records.workingBytes()[layout::profileSize + layout::knobsOffset + 3] == 0x1F, "type edit leaves cc");
    expect(records.apply({ ControlKind::slider, 1 }, DeviceProfile::ccUser, EditableField::type, 2, error), "slider type");
    const auto sliderTypeAt = layout::profileSize + layout::slidersOffset + layout::sliderSize;
    expect(records.workingBytes()[sliderTypeAt] == 2, "slider type byte");
    expect(records.apply({ ControlKind::trackButton, 4 }, DeviceProfile::ccUser, EditableField::mode, 5, error), "track mode");
    expect(records.workingBytes()[layout::profileSize + 4 * layout::buttonSize] == 5, "track mode byte");
    expect(records.apply({ ControlKind::globalButton, 1 }, DeviceProfile::ccUser, EditableField::mode, 1, error), "global mode");
    expect(records.workingBytes()[layout::profileSize + 33 * layout::buttonSize] == 1, "global mode byte");
    expect(!records.apply({ ControlKind::knob, 0 }, DeviceProfile::ccUser, EditableField::mode, 1, error), "knob has no mode");

    auto exported = image;
    const auto messageAt = layout::profileSize;
    exported[messageAt + 5] = 3;
    exported[messageAt + 6] = 0xf0;
    exported[messageAt + 7] = 0x7f;
    exported[messageAt + 8] = 0xf7;
    const auto json = ControlConfiguration::parse(exported).toJson(DeviceProfile::ccUser);
    expect(json.find("\"profile\": \"CC / User\"") != std::string::npos, "json profile");
    expect(json.find("\"channel\": 1") != std::string::npos, "json channel is one-based");
    expect(json.find("\"cc\": 31") != std::string::npos, "json knob cc");
    expect(json.find("F0 7F F7") != std::string::npos, "json message");
    const auto dawJson = ControlConfiguration::parse(exported).toJson(DeviceProfile::daw);
    expect(dawJson.find("\"profile\": \"DAW\"") != std::string::npos, "json daw profile");
    expect(dawJson.find("\"cc\": 31") == std::string::npos, "daw json omits the cc user cc");

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
