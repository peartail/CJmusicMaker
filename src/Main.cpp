#include <tracktion_engine/tracktion_engine.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace
{
namespace te = tracktion::engine;

void routeTrackToAudioOutput (te::AudioTrack& track)
{
    track.getOutput().setOutputToDefaultDevice (false);
}

constexpr double timelineLengthSeconds = 16.0;
constexpr int keyboardOctaves = 3;
constexpr auto midiNotes = []
{
    std::array<int, keyboardOctaves * 12> notes {};
    for (size_t index = 0; index < notes.size(); ++index)
        notes[index] = 60 + static_cast<int> (index);
    return notes;
}();
constexpr std::array<char, 13> keyChars { 'A', 'W', 'S', 'E', 'D', 'F', 'T', 'G', 'Y', 'H', 'U', 'J', 'K' };
constexpr std::array<int, 7> whiteSemitones { 0, 2, 4, 5, 7, 9, 11 };
constexpr std::array<int, 5> blackSemitones { 1, 3, 6, 8, 10 };
constexpr std::array<int, 5> blackSlots { 0, 1, 3, 4, 5 };
constexpr auto whiteNoteIndices = []
{
    std::array<int, keyboardOctaves * whiteSemitones.size()> indices {};
    for (int octave = 0; octave < keyboardOctaves; ++octave)
        for (size_t note = 0; note < whiteSemitones.size(); ++note)
            indices[static_cast<size_t> (octave) * whiteSemitones.size() + note] = octave * 12 + whiteSemitones[note];
    return indices;
}();
constexpr auto blackNoteIndices = []
{
    std::array<int, keyboardOctaves * blackSemitones.size()> indices {};
    for (int octave = 0; octave < keyboardOctaves; ++octave)
        for (size_t note = 0; note < blackSemitones.size(); ++note)
            indices[static_cast<size_t> (octave) * blackSemitones.size() + note] = octave * 12 + blackSemitones[note];
    return indices;
}();
constexpr auto blackKeySlots = []
{
    std::array<int, keyboardOctaves * blackSlots.size()> slots {};
    for (int octave = 0; octave < keyboardOctaves; ++octave)
        for (size_t note = 0; note < blackSlots.size(); ++note)
            slots[static_cast<size_t> (octave) * blackSlots.size() + note] = octave * 7 + blackSlots[note];
    return slots;
}();
constexpr std::array<const char*, 12> rootNames {
    "C", "C#/Db", "D", "D#/Eb", "E", "F", "F#/Gb", "G", "G#/Ab", "A", "A#/Bb", "B"
};
constexpr std::array<const char*, 2> scaleNames { "Major", "Natural Minor" };
const juce::Identifier scaleRootProperty { "musicBitMakerScaleRoot" };
const juce::Identifier scaleTypeProperty { "musicBitMakerScaleType" };
const juce::Identifier octaveProperty { "musicBitMakerKeyboardOctave" };
constexpr int minimumOctave = 0, maximumOctave = 6;

juce::String instrumentStateForLog (const te::AudioTrack& track)
{
    const auto synths = track.pluginList.getPluginsOfType<te::FourOscPlugin>();
    juce::String result = "track=" + track.itemID.toString()
                        + " fourOscCount=" + juce::String (synths.size())
                        + " output=" + track.getOutput().getOutputName()
                        + " defaultAudioOut=" + juce::String (track.getOutput().usesDefaultAudioOut() ? 1 : 0);
    if (synths.isEmpty())
        return result;

    const auto& synth = *synths.getFirst();
    const auto property = [&synth] (const char* name)
    {
        return synth.state.getProperty (juce::Identifier (name), "<default>").toString();
    };
    return result + " plugin=" + synth.itemID.toString()
           + " preset=" + property ("musicBitMakerPreset")
           + " drum=" + property ("musicBitMakerDrumIndex")
           + " wave1=" + property ("waveShape1")
           + " wave2=" + property ("waveShape2")
           + " attack=" + property ("ampAttack")
           + " release=" + property ("ampRelease")
           + " filterFreq=" + property ("filterFreq")
           + " distortionOn=" + property ("distortionOn")
           + " delayOn=" + property ("delayOn")
           + " chorusOn=" + property ("chorusOn")
           + " reverbOn=" + property ("reverbOn");
}

int keyboardPitch (size_t index, int octave)
{
    return midiNotes[index] + 12 * (juce::jlimit (minimumOctave, maximumOctave, octave) - 4);
}

juce::String pitchLabel (int pitch)
{
    return juce::String (rootNames[static_cast<size_t> (pitch % 12)]) + juce::String (pitch / 12 - 1);
}

juce::String compactPitchLabel (int pitch)
{
    return juce::String (rootNames[static_cast<size_t> (pitch % 12)])
        .upToFirstOccurrenceOf ("/", false, false) + juce::String (pitch / 12 - 1);
}
const juce::Identifier trackHeightProperty { "musicBitMakerTrackHeight" };
constexpr auto lastNewProjectDirectoryKey = "lastNewProjectDirectory";

constexpr int synthTrackDefaultHeight = 140;
constexpr int drumTrackDefaultHeight = 64;
constexpr int synthTrackMinimumHeight = 80;
constexpr int drumTrackMinimumHeight = 44;
constexpr int trackMaximumHeight = 360;

int resizedTrackHeight (bool isDrum, int height)
{
    return juce::jlimit (isDrum ? drumTrackMinimumHeight : synthTrackMinimumHeight,
                         trackMaximumHeight, height);
}

juce::File projectDirectoryFor (const juce::String& rememberedPath)
{
    if (rememberedPath.isNotEmpty())
    {
        const juce::File rememberedDirectory (rememberedPath);
        if (rememberedDirectory.isDirectory())
            return rememberedDirectory;
    }
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
}

void clearAllTracks (te::Edit& edit)
{
    for (auto* track : te::getAudioTracks (edit))
        edit.deleteTrack (track);
}

bool noteIsInScale (int midiNote, int rootIndex, int scaleIndex)
{
    constexpr std::array<int, 7> majorIntervals { 0, 2, 4, 5, 7, 9, 11 };
    constexpr std::array<int, 7> minorIntervals { 0, 2, 3, 5, 7, 8, 10 };
    const auto interval = (midiNote % 12 - juce::jlimit (0, 11, rootIndex) + 12) % 12;
    const auto& intervals = scaleIndex == 1 ? minorIntervals : majorIntervals;
    return std::find (intervals.begin(), intervals.end(), interval) != intervals.end();
}

struct TimelineNote
{
    int pitch = 60;
    double startSeconds = 0.0;
    double endSeconds = 0.0;
};

TimelineNote editNoteRange (TimelineNote note, double deltaSeconds, bool resizeEnd)
{
    constexpr double minimumLength = 0.05;

    if (resizeEnd)
    {
        note.endSeconds = juce::jlimit (note.startSeconds + minimumLength,
                                        timelineLengthSeconds,
                                        note.endSeconds + deltaSeconds);
    }
    else
    {
        const auto length = note.endSeconds - note.startSeconds;
        note.startSeconds = juce::jlimit (0.0, timelineLengthSeconds - length,
                                          note.startSeconds + deltaSeconds);
        note.endSeconds = note.startSeconds + length;
    }

    return note;
}

struct SynthPreset
{
    const char* name;
    const char* xml;
};

constexpr std::array<SynthPreset, 5> synthPresets {{
    { "Generated Keys", R"(<PLUGIN type="4osc" presetName="Generated Keys" waveShape1="1" level1="0" waveShape2="4" level2="-12" tune2="12" ampAttack="0.005" ampDecay="0.8" ampSustain="45" ampRelease="0.4" filterType="1" filterFreq="120" filterResonance="8" reverbOn="1" reverbSize="0.35" reverbDamping="0.5" reverbWidth="0.8" reverbMix="0.08"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" },
    { "Retro Pulse", R"(<PLUGIN type="4osc" presetName="Retro Pulse" waveShape1="2" level1="0" pulseWidth1="0.25" waveShape2="4" level2="-18" tune2="-12" ampAttack="0.001" ampDecay="0.25" ampSustain="65" ampRelease="0.08" filterType="1" filterFreq="112" filterResonance="16"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" },
    { "8-Bit Lead", R"(<PLUGIN type="4osc" presetName="8-Bit Lead" waveShape1="2" level1="0" pulseWidth1="0.125" waveShape2="2" level2="-16" tune2="12" pulseWidth2="0.5" ampAttack="0.001" ampDecay="0.12" ampSustain="75" ampRelease="0.04" filterType="1" filterFreq="126" distortionOn="1" distortion="0.08"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" },
    { "Space Beep", R"(<PLUGIN type="4osc" presetName="Space Beep" waveShape1="1" level1="0" waveShape2="4" level2="-12" tune2="12" ampAttack="0.002" ampDecay="0.18" ampSustain="20" ampRelease="0.35" filterType="1" filterFreq="118" delayOn="1" delay="0.25" delayFeedback="-12" delayMix="0.25" reverbOn="1" reverbSize="0.45" reverbMix="0.15"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" },
    { "Cosmic Pad", R"(<PLUGIN type="4osc" presetName="Cosmic Pad" waveShape1="3" level1="-6" voices1="3" detune1="0.18" spread1="0.7" waveShape2="4" level2="-12" tune2="-12" ampAttack="1.5" ampDecay="2" ampSustain="75" ampRelease="2.5" filterType="1" filterFreq="82" filterResonance="12" chorusOn="1" reverbOn="1" reverbSize="0.75" reverbDamping="0.45" reverbWidth="1" reverbMix="0.3"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" }
}};

struct DrumPreset
{
    const char* name;
    int midiNote;
    double durationSeconds;
    const char* xml;
};

constexpr std::array<DrumPreset, 4> drumPresets {{
    { "Kick", 36, 0.24, R"(<PLUGIN type="4osc" presetName="Kick" waveShape1="1" level1="0" waveShape2="4" level2="-18" tune2="12" ampAttack="0.001" ampDecay="0.22" ampSustain="0" ampRelease="0.05" ampAnalog="0" masterLevel="-3" filterType="0"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" },
    { "Snare", 38, 0.18, R"(<PLUGIN type="4osc" presetName="Snare" waveShape1="5" level1="-3" waveShape2="4" level2="-15" tune2="-12" ampAttack="0.001" ampDecay="0.16" ampSustain="0" ampRelease="0.03" ampAnalog="0" masterLevel="-5" filterType="0"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" },
    { "Closed Hat", 42, 0.08, R"(<PLUGIN type="4osc" presetName="Closed Hat" waveShape1="5" level1="0" ampAttack="0.001" ampDecay="0.06" ampSustain="0" ampRelease="0.015" ampAnalog="0" masterLevel="-3" filterType="0"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" },
    { "Open Hat", 46, 0.42, R"(<PLUGIN type="4osc" presetName="Open Hat" waveShape1="5" level1="-2" ampAttack="0.001" ampDecay="0.35" ampSustain="0" ampRelease="0.08" ampAnalog="0" masterLevel="-5" filterType="0"><MACROPARAMETERS/><MODIFIERASSIGNMENTS/><MODMATRIX/></PLUGIN>)" }
}};

bool configureSynth (te::FourOscPlugin& synth, int presetIndex)
{
    presetIndex = juce::jlimit (0, static_cast<int> (synthPresets.size()) - 1, presetIndex);
    const auto patch = juce::ValueTree::fromXml (synthPresets[static_cast<size_t> (presetIndex)].xml);
    if (! patch.isValid())
        return false;

    synth.restorePluginStateFromValueTree (patch);
    synth.state.removeProperty ("musicBitMakerDrumIndex", nullptr);
    synth.state.setProperty ("musicBitMakerPreset", presetIndex, nullptr);
    synth.state.setProperty ("presetName", synthPresets[static_cast<size_t> (presetIndex)].name, nullptr);
    return true;
}

bool configureDrumSynth (te::FourOscPlugin& synth, int drumIndex)
{
    drumIndex = juce::jlimit (0, static_cast<int> (drumPresets.size()) - 1, drumIndex);
    const auto patch = juce::ValueTree::fromXml (drumPresets[static_cast<size_t> (drumIndex)].xml);
    if (! patch.isValid())
        return false;

    synth.restorePluginStateFromValueTree (patch);
    synth.state.removeProperty ("musicBitMakerPreset", nullptr);
    synth.state.setProperty ("musicBitMakerDrumIndex", drumIndex, nullptr);
    synth.state.setProperty ("presetName", drumPresets[static_cast<size_t> (drumIndex)].name, nullptr);
    return true;
}

class DiagnosticAudioTap final : public te::Plugin
{
public:
    static constexpr auto xmlTypeName = "musicBitMakerDiagnosticAudioTap";
    static constexpr std::uint32_t windowFrames = 4096;

    struct Window
    {
        std::uint32_t capture = 0, sequence = 0, frames = 0, samples = 0, nonZero = 0;
        std::uint64_t hash = 0;
        double sumSquares = 0.0;
        float peak = 0.0f;
    };

    explicit DiagnosticAudioTap (te::PluginCreationInfo info) : Plugin (info) {}

    juce::String getName() const override { return "MusicBitMaker Diagnostic Audio Tap"; }
    juce::String getPluginType() override { return xmlTypeName; }
    juce::String getSelectableDescription() override { return getName(); }
    bool shouldMeasureCpuUsage() const noexcept override { return false; }
    void initialise (const te::PluginInitialisationInfo&) override {}
    void deinitialise() override {}

    void armCapture (std::uint32_t capture) noexcept
    {
        requestedCapture.store (capture, std::memory_order_release);
    }

    bool popWindow (Window& result) noexcept
    {
        const auto read = readIndex.load (std::memory_order_relaxed);
        if (read == writeIndex.load (std::memory_order_acquire))
            return false;
        result = windows[read % windows.size()];
        readIndex.store (read + 1, std::memory_order_release);
        return true;
    }

    std::uint32_t takeDroppedCount() noexcept
    {
        return droppedWindows.exchange (0, std::memory_order_relaxed);
    }

    void applyToBuffer (const te::PluginRenderContext& context) override
    {
        const auto requested = requestedCapture.load (std::memory_order_acquire);
        if (requested != lastSeenCapture)
        {
            lastSeenCapture = activeCapture = requested;
            sequence = 0;
            resetWindow();
        }
        if (activeCapture == 0 || context.destBuffer == nullptr)
            return;

        const auto channels = juce::jmin (2, context.destBuffer->getNumChannels());
        if (channels == 0)
            return;

        for (int frame = 0; frame < context.bufferNumSamples; ++frame)
        {
            for (int channel = 0; channel < channels; ++channel)
            {
                const auto sample = context.destBuffer->getReadPointer (channel)
                    [context.bufferStartSample + frame];
                hash ^= std::bit_cast<std::uint32_t> (sample);
                hash *= 1099511628211ULL;
                const auto magnitude = std::abs (sample);
                peak = juce::jmax (peak, magnitude);
                sumSquares += static_cast<double> (sample) * sample;
                nonZero += magnitude > 0.000001f ? 1u : 0u;
                ++samples;
            }
            if (++frames == windowFrames)
            {
                publishWindow();
                if (activeCapture == 0)
                    break;
            }
        }
    }

private:
    void resetWindow() noexcept
    {
        frames = samples = nonZero = 0;
        hash = 14695981039346656037ULL;
        sumSquares = 0.0;
        peak = 0.0f;
    }

    void publishWindow() noexcept
    {
        const auto write = writeIndex.load (std::memory_order_relaxed);
        if (write - readIndex.load (std::memory_order_acquire) < windows.size())
        {
            windows[write % windows.size()] = { activeCapture, sequence, frames, samples,
                                                   nonZero, hash, sumSquares, peak };
            writeIndex.store (write + 1, std::memory_order_release);
        }
        else
        {
            droppedWindows.fetch_add (1, std::memory_order_relaxed);
        }
        if (++sequence == 24)
            activeCapture = 0;
        resetWindow();
    }

    std::array<Window, 128> windows {};
    std::atomic<std::uint32_t> requestedCapture { 0 }, readIndex { 0 }, writeIndex { 0 }, droppedWindows { 0 };
    std::uint32_t lastSeenCapture = 0, activeCapture = 0, sequence = 0;
    std::uint32_t frames = 0, samples = 0, nonZero = 0;
    std::uint64_t hash = 14695981039346656037ULL;
    double sumSquares = 0.0;
    float peak = 0.0f;
};

class KeyboardPadComponent final : public juce::Component
{
public:
    KeyboardPadComponent (std::function<void (size_t, bool)> noteChangedToUse,
                          std::function<bool (size_t)> noteIsDownToUse,
                          std::function<void (int, int)> scaleChangedToUse,
                          std::function<void (int)> octaveChangedToUse)
        : noteChanged (std::move (noteChangedToUse)),
          noteIsDown (std::move (noteIsDownToUse)),
          scaleChanged (std::move (scaleChangedToUse)),
          octaveChanged (std::move (octaveChangedToUse))
    {
        setWantsKeyboardFocus (true);
        setSize (1160, 250);
        for (int octave = minimumOctave; octave <= maximumOctave; ++octave)
            octaveBox.addItem ("Octave: " + juce::String (octave), octave + 1);
        octaveBox.setSelectedId (5, juce::dontSendNotification);
        octaveBox.setTitle ("Keyboard octave");
        octaveBox.onChange = [this] { changeOctave (octaveBox.getSelectedId() - 1); };
        addAndMakeVisible (octaveBox);

        for (size_t i = 0; i < rootNames.size(); ++i)
            rootBox.addItem ("Root: " + juce::String (rootNames[i]), static_cast<int> (i + 1));
        for (size_t i = 0; i < scaleNames.size(); ++i)
            scaleBox.addItem ("Scale: " + juce::String (scaleNames[i]), static_cast<int> (i + 1));
        rootBox.setSelectedId (1, juce::dontSendNotification);
        scaleBox.setSelectedId (1, juce::dontSendNotification);
        rootBox.setTitle ("Root note");
        scaleBox.setTitle ("Scale");
        rootBox.onChange = scaleBox.onChange = [this]
        {
            if (! updatingScale)
            {
                scaleChanged (rootBox.getSelectedId() - 1, scaleBox.getSelectedId() - 1);
                repaint();
            }
        };
        addAndMakeVisible (rootBox);
        addAndMakeVisible (scaleBox);
        for (auto* box : { &octaveBox, &rootBox, &scaleBox })
            box->setWantsKeyboardFocus (false);
    }

    ~KeyboardPadComponent() override
    {
        releaseAll();
    }

    void setTrackEnabled (bool shouldBeEnabled)
    {
        if (! shouldBeEnabled)
            releaseAll();
        setEnabled (shouldBeEnabled);
        repaint();
    }

    void setScale (int rootIndex, int scaleIndex)
    {
        juce::ScopedValueSetter<bool> guard (updatingScale, true);
        rootBox.setSelectedId (juce::jlimit (0, 11, rootIndex) + 1, juce::dontSendNotification);
        scaleBox.setSelectedId (juce::jlimit (0, 1, scaleIndex) + 1, juce::dontSendNotification);
        repaint();
    }

    void resized() override
    {
        octaveBox.setBounds (12, 10, 116, 30);
        rootBox.setBounds (136, 10, 120, 30);
        scaleBox.setBounds (264, 10, 160, 30);
    }

    void setOctave (int octave)
    {
        changeOctave (octave, false);
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.fillAll (juce::Colour::fromRGB (20, 23, 31));
        graphics.setColour (juce::Colour::fromRGB (190, 198, 214));
        graphics.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        graphics.drawText (isEnabled() ? "MOUSE: 3 OCTAVES  |  A-K: FIRST OCTAVE  |  Z/X: SHIFT"
                                       : "KEYBOARD DISABLED  |  Select a keyboard track",
                           432, 10, getWidth() - 444, 30, juce::Justification::centredLeft);

        for (const auto index : whiteNoteIndices)
            drawKey (graphics, static_cast<size_t> (index), false);
        for (const auto index : blackNoteIndices)
            drawKey (graphics, static_cast<size_t> (index), true);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto character = static_cast<char> (std::toupper (key.getTextCharacter()));
        if (character == 'Z' || character == 'X')
        {
            if (! key.getModifiers().isAnyModifierKeyDown())
                changeOctave (currentOctave + (character == 'Z' ? -1 : 1));
            return true;
        }
        for (size_t index = 0; index < keyChars.size(); ++index)
            if (character == keyChars[index])
            {
                keyboardDown[index] = true;
                updateNote (index);
                return true;
            }
        return false;
    }

    bool keyStateChanged (bool) override
    {
        for (size_t index = 0; index < keyChars.size(); ++index)
        {
            keyboardDown[index] = juce::KeyPress::isKeyCurrentlyDown (std::tolower (keyChars[index]));
            updateNote (index);
        }
        repaint();
        return true;
    }

    void focusLost (FocusChangeType) override
    {
        releaseAll();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        grabKeyboardFocus();
        updateMouseNote (noteAt (event.position));
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        updateMouseNote (noteAt (event.position));
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        updateMouseNote (-1);
    }

private:
    friend int runKeyboardOctaveTest();

    void changeOctave (int octave, bool notify = true)
    {
        octave = juce::jlimit (minimumOctave, maximumOctave, octave);
        if (octave != currentOctave)
        {
            releaseAll();
            currentOctave = octave;
            if (notify)
                octaveChanged (octave);
        }
        octaveBox.setSelectedId (octave + 1, juce::dontSendNotification);
        repaint();
    }

    juce::Rectangle<int> getKeyArea() const
    {
        return getLocalBounds().withTrimmedTop (50).reduced (8, 4);
    }

    juce::Rectangle<int> getKeyBounds (size_t index) const
    {
        const auto bounds = getKeyArea();
        const auto white = std::find (whiteNoteIndices.begin(), whiteNoteIndices.end(), static_cast<int> (index));
        if (white != whiteNoteIndices.end())
        {
            const auto slot = static_cast<int> (std::distance (whiteNoteIndices.begin(), white));
            const auto left = bounds.getX() + slot * bounds.getWidth() / static_cast<int> (whiteNoteIndices.size());
            const auto right = bounds.getX() + (slot + 1) * bounds.getWidth() / static_cast<int> (whiteNoteIndices.size());
            return { left, bounds.getY(), right - left, bounds.getHeight() };
        }

        const auto black = std::find (blackNoteIndices.begin(), blackNoteIndices.end(), static_cast<int> (index));
        if (black == blackNoteIndices.end())
            return {};
        const auto slot = blackKeySlots[static_cast<size_t> (std::distance (blackNoteIndices.begin(), black))];
        const auto whiteWidth = bounds.getWidth() / static_cast<float> (whiteNoteIndices.size());
        const auto width = juce::roundToInt (whiteWidth * 0.62f);
        const auto centre = juce::roundToInt (bounds.getX() + (slot + 1) * whiteWidth);
        return { centre - width / 2, bounds.getY(), width, juce::roundToInt (bounds.getHeight() * 0.62f) };
    }

    void drawKey (juce::Graphics& graphics, size_t index, bool black) const
    {
        const auto key = getKeyBounds (index).toFloat().reduced (black ? 1.0f : 2.0f);
        const auto down = isEnabled() && noteIsDown (index);
        const auto root = midiNotes[index] % 12 == rootBox.getSelectedId() - 1;
        const auto inScale = noteIsInScale (midiNotes[index], rootBox.getSelectedId() - 1,
                                            scaleBox.getSelectedId() - 1);
        const auto colour = ! isEnabled() ? (black ? juce::Colour::fromRGB (57, 60, 68)
                                                    : juce::Colour::fromRGB (105, 108, 116))
                          : down ? juce::Colour::fromRGB (105, 225, 180)
                          : root ? (black ? juce::Colour::fromRGB (230, 142, 58)
                                          : juce::Colour::fromRGB (255, 218, 125))
                          : inScale ? (black ? juce::Colour::fromRGB (46, 116, 105)
                                             : juce::Colour::fromRGB (190, 238, 220))
                          : black ? juce::Colour::fromRGB (36, 40, 50)
                                  : juce::Colour::fromRGB (239, 241, 246);
        graphics.setColour (colour);
        graphics.fillRoundedRectangle (key, black ? 5.0f : 7.0f);
        graphics.setColour (juce::Colour::fromRGB (46, 51, 64));
        graphics.drawRoundedRectangle (key, black ? 5.0f : 7.0f, 2.0f);
        graphics.setColour (black ? juce::Colours::white : juce::Colour::fromRGB (46, 51, 64));
        graphics.setFont (juce::FontOptions (black ? 15.0f : 19.0f, juce::Font::bold));
        auto labels = key.toNearestInt();
        const auto noteLabel = labels.removeFromBottom (24);
        const auto shortcutLabel = labels.removeFromBottom (28);
        if (index < keyChars.size())
            graphics.drawText (juce::String::charToString (keyChars[index]), shortcutLabel, juce::Justification::centred);
        graphics.setFont (juce::FontOptions (black ? 9.0f : 11.0f));
        graphics.drawText (compactPitchLabel (keyboardPitch (index, currentOctave)),
                           noteLabel, juce::Justification::centred);
    }

    int noteAt (juce::Point<float> position) const
    {
        for (const auto index : blackNoteIndices)
            if (getKeyBounds (static_cast<size_t> (index)).toFloat().contains (position))
                return index;
        for (const auto index : whiteNoteIndices)
            if (getKeyBounds (static_cast<size_t> (index)).toFloat().contains (position))
                return index;
        return -1;
    }

    void updateNote (size_t index)
    {
        const auto down = keyboardDown[index] || static_cast<int> (index) == mouseNote;
        if (sentDown[index] == down)
            return;
        sentDown[index] = down;
        noteChanged (index, down);
        repaint (getKeyBounds (index));
    }

    void updateMouseNote (int newNote)
    {
        if (mouseNote == newNote)
            return;
        const auto oldNote = mouseNote;
        mouseNote = newNote;
        if (oldNote >= 0)
            updateNote (static_cast<size_t> (oldNote));
        if (mouseNote >= 0)
            updateNote (static_cast<size_t> (mouseNote));
    }

    void releaseAll()
    {
        keyboardDown.fill (false);
        mouseNote = -1;
        for (size_t index = 0; index < sentDown.size(); ++index)
            updateNote (index);
    }

    std::function<void (size_t, bool)> noteChanged;
    std::function<bool (size_t)> noteIsDown;
    std::function<void (int, int)> scaleChanged;
    std::function<void (int)> octaveChanged;
    juce::ComboBox octaveBox, rootBox, scaleBox;
    int currentOctave = 4;
    std::array<bool, midiNotes.size()> keyboardDown {};
    std::array<bool, midiNotes.size()> sentDown {};
    int mouseNote = -1;
    bool updatingScale = false;
};

class KeyboardPadWindow final : public juce::DocumentWindow
{
public:
    KeyboardPadWindow (std::function<void (size_t, bool)> noteChanged,
                       std::function<bool (size_t)> noteIsDown,
                       std::function<void (int, int)> scaleChanged,
                       std::function<void (int)> octaveChanged,
                       std::function<void()> closedToUse)
        : DocumentWindow ("Keyboard Pad", juce::Colour::fromRGB (20, 23, 31),
                          minimiseButton | closeButton),
          closed (std::move (closedToUse))
    {
        setUsingNativeTitleBar (true);
        setResizable (true, false);
        setResizeLimits (900, 220, 1600, 420);
        pad = new KeyboardPadComponent (std::move (noteChanged), std::move (noteIsDown),
                                        std::move (scaleChanged), std::move (octaveChanged));
        setContentOwned (pad, true);
        centreWithSize (1160, 280);
        setAlwaysOnTop (true);
        setVisible (true);
    }

    void setTrackEnabled (bool enabled, const juce::String& trackName)
    {
        pad->setTrackEnabled (enabled);
        setName (enabled ? "Keyboard Pad - " + trackName
                         : "Keyboard Pad - Disabled (" + trackName + ")");
    }

    void repaintPad()
    {
        pad->repaint();
    }

    void setOctave (int octave) { pad->setOctave (octave); }

    void setScale (int rootIndex, int scaleIndex)
    {
        pad->setScale (rootIndex, scaleIndex);
    }

    void closeButtonPressed() override
    {
        pad->setTrackEnabled (false);
        setVisible (false);
        closed();
    }

private:
    KeyboardPadComponent* pad = nullptr;
    std::function<void()> closed;
};

class MainComponent final : public juce::Component,
                            public juce::MenuBarModel,
                            private juce::Timer
{
public:
    explicit MainComponent (te::Engine& engineToUse)
        : engine (engineToUse),
          edit (std::make_unique<te::Edit> (engine, te::Edit::forEditing))
    {
        setOpaque (true);
        setWantsKeyboardFocus (true);
        setSize (1000, 720);

        juce::PropertiesFile::Options settingsOptions;
        settingsOptions.applicationName = "MusicBitMaker";
        settingsOptions.filenameSuffix = ".settings";
        settingsOptions.folderName = "MusicBitMaker";
        settingsOptions.osxLibrarySubFolder = "Application Support";
        appProperties.setStorageParameters (settingsOptions);

        instrumentLog.reset (juce::FileLogger::createDefaultAppLogger (
            "MusicBitMaker", "instrument-diagnostic.log",
            "MusicBitMaker instrument diagnostics " JUCE_APPLICATION_VERSION_STRING, 1024 * 1024));
        logInstrumentEvent ("session executable="
            + juce::File::getSpecialLocation (juce::File::currentExecutableFile).getFullPathName());

        initialiseEdit (true);

        recordButton.onClick = [this] { toggleRecording(); };
        playButton.onClick = [this] { startPlayback(); };
        stopButton.onClick = [this] { stopPlayback(); };
        deleteButton.onClick = [this] { deleteSelectedNote(); };
        kickButton.onClick = [this] { triggerDrum (0, true); };
        snareButton.onClick = [this] { triggerDrum (1, true); };
        closedHatButton.onClick = [this] { triggerDrum (2, true); };
        openHatButton.onClick = [this] { triggerDrum (3, true); };
        addTrackButton.onClick = [this] { addTrack(); };
        settingsButton.onClick = [this] { showAudioSettings(); };

        for (auto* button : { &recordButton, &playButton, &stopButton, &deleteButton,
                              &kickButton, &snareButton, &closedHatButton, &openHatButton,
                              &addTrackButton, &settingsButton })
        {
            button->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (*button);
        }

        addAndMakeVisible (menuBar);

        volumeSlider.setRange (-60.0, 6.0, 0.1);
        volumeSlider.setTextValueSuffix (" dB");
        panSlider.setRange (-1.0, 1.0, 0.01);
        panSlider.textFromValueFunction = [] (double value)
        {
            if (std::abs (value) < 0.005)
                return juce::String ("CENTER");
            return juce::String (value < 0.0 ? "L " : "R ")
                 + juce::String (juce::roundToInt (std::abs (value) * 100.0));
        };
        for (auto* slider : { &volumeSlider, &panSlider })
        {
            slider->setSliderStyle (juce::Slider::LinearHorizontal);
            slider->setTextBoxStyle (juce::Slider::TextBoxRight, false, 68, 22);
            slider->setWantsKeyboardFocus (false);
            addAndMakeVisible (*slider);
        }
        volumeSlider.onValueChange = [this]
        {
            if (! updatingTrackControls)
                if (auto* track = selectedTrack())
                    if (auto* volume = track->getVolumePlugin())
                    {
                        volume->setVolumeDb (static_cast<float> (volumeSlider.getValue()));
                        statusMessage = "Volume: " + juce::String (volumeSlider.getValue(), 1) + " dB";
                        repaint();
                    }
        };
        panSlider.onValueChange = [this]
        {
            if (! updatingTrackControls)
                if (auto* track = selectedTrack())
                    if (auto* volume = track->getVolumePlugin())
                    {
                        volume->setPan (static_cast<float> (panSlider.getValue()));
                        statusMessage = "Pan: " + panSlider.getTextFromValue (panSlider.getValue());
                        repaint();
                    }
        };

        muteButton.onClick = [this]
        {
            if (! updatingTrackControls)
                if (auto* track = selectedTrack())
                    track->setMute (muteButton.getToggleState());
            repaint();
        };
        soloButton.onClick = [this]
        {
            if (! updatingTrackControls)
                if (auto* track = selectedTrack())
                    track->setSolo (soloButton.getToggleState());
            repaint();
        };
        for (auto* button : { &muteButton, &soloButton })
        {
            button->setClickingTogglesState (true);
            button->setMouseClickGrabsKeyboardFocus (false);
            addAndMakeVisible (*button);
        }

        keyboardWindow = std::make_unique<KeyboardPadWindow> (
            [this] (size_t index, bool down)
            {
                keyboardNotesDown[index] = down;
                updateCombinedLiveNote (index, "keyboard-pad");
            },
            [this] (size_t index) { return isNoteVisuallyDown (index); },
            [this] (int rootIndex, int scaleIndex)
            {
                edit->state.setProperty (scaleRootProperty, rootIndex, nullptr);
                edit->state.setProperty (scaleTypeProperty, scaleIndex, nullptr);
                statusMessage = juce::String (rootNames[static_cast<size_t> (rootIndex)])
                              + " " + scaleNames[static_cast<size_t> (scaleIndex)];
                repaint();
            },
            [this] (int octave)
            {
                edit->state.setProperty (octaveProperty, octave, nullptr);
                repaint();
            },
            [this] { menuItemsChanged(); });
        syncTrackControls();
        startTimerHz (30);
    }

    juce::StringArray getMenuBarNames() override
    {
        return { "File", "View" };
    }

    juce::PopupMenu getMenuForIndex (int topLevelMenuIndex, const juce::String&) override
    {
        juce::PopupMenu menu;
        if (topLevelMenuIndex == 0)
        {
            menu.addItem (newProjectMenuId, "New Project...\tCtrl+N");
            menu.addSeparator();
            menu.addItem (saveMenuId, "Save\tCtrl+S");
            menu.addItem (saveAsMenuId, "Save As...\tCtrl+Shift+S");
            menu.addSeparator();
            menu.addItem (loadMenuId, "Load...\tCtrl+O");
        }
        else
        {
            menu.addItem (keyboardPadMenuId, "Keyboard Pad", true,
                          keyboardWindow != nullptr && keyboardWindow->isVisible());
            menu.addItem (instrumentLogMenuId, "Show Instrument Diagnostic Log", instrumentLog != nullptr);
        }
        return menu;
    }

    void menuItemSelected (int menuItemId, int) override
    {
        if (menuItemId == newProjectMenuId) createNewProject();
        else if (menuItemId == saveMenuId) saveProject();
        else if (menuItemId == saveAsMenuId) saveProjectAs();
        else if (menuItemId == loadMenuId) chooseProjectToLoad();
        else if (menuItemId == keyboardPadMenuId) toggleKeyboardWindow();
        else if (menuItemId == instrumentLogMenuId && instrumentLog != nullptr)
            instrumentLog->getLogFile().revealToUser();
    }

    ~MainComponent() override
    {
        stopTimer();
        keyboardWindow.reset();
        releaseAllLiveNotes();
        edit->getTransport().stop (false, false);
    }

    void paint (juce::Graphics& graphics) override
    {
        graphics.fillAll (juce::Colour::fromRGB (20, 23, 31));
        graphics.setColour (juce::Colours::white);
        graphics.setFont (juce::FontOptions (28.0f, juce::Font::bold));
        graphics.drawText ("MusicBitMaker " JUCE_APPLICATION_VERSION_STRING,
                           30, 26, getWidth() - 60, 34, juce::Justification::centredLeft);
        drawStatus (graphics);
        drawTrackManager (graphics);
        drawTimeline (graphics);
    }

    void resized() override
    {
        menuBar.setBounds (0, 0, getWidth(), 24);
        auto controls = juce::Rectangle<int> (contentLeft, 92, getWidth() - contentLeft - 24, 42);
        recordButton.setBounds (controls.removeFromLeft (82).reduced (2));
        playButton.setBounds (controls.removeFromLeft (62).reduced (2));
        stopButton.setBounds (controls.removeFromLeft (62).reduced (2));
        deleteButton.setBounds (controls.removeFromLeft (88).reduced (2));
        kickButton.setBounds (controls.removeFromLeft (72).reduced (2));
        snareButton.setBounds (controls.removeFromLeft (72).reduced (2));
        closedHatButton.setBounds (controls.removeFromLeft (72).reduced (2));
        openHatButton.setBounds (controls.removeFromLeft (72).reduced (2));
        settingsButton.setBounds (getWidth() - 206, 26, 176, 34);

        addTrackButton.setBounds (24, 92, 212, 34);
        volumeSlider.setBounds (contentLeft, 154, 150, 30);
        panSlider.setBounds (contentLeft + 154, 154, 150, 30);
        muteButton.setBounds (contentLeft + 308, 154, 70, 30);
        soloButton.setBounds (contentLeft + 382, 154, 70, 30);
    }

    bool keyPressed (const juce::KeyPress& key) override
    {
        const auto character = static_cast<char> (std::toupper (key.getTextCharacter()));

        if (key.getModifiers().isCommandDown() && key.getModifiers().isShiftDown() && character == 'S')
        {
            saveProjectAs();
            return true;
        }

        if (key.getModifiers().isCommandDown() && character == 'S')
        {
            saveProject();
            return true;
        }

        if (key.getModifiers().isCommandDown() && character == 'N')
        {
            createNewProject();
            return true;
        }

        if (key.getModifiers().isCommandDown() && character == 'O')
        {
            chooseProjectToLoad();
            return true;
        }

        if (key == juce::KeyPress::deleteKey || key == juce::KeyPress::backspaceKey)
        {
            deleteSelectedNote();
            return true;
        }

        for (size_t index = 0; index < keyChars.size(); ++index)
        {
            if (character == keyChars[index])
            {
                keyboardNotesDown[index] = true;
                updateCombinedLiveNote (index, "main-keyboard");
                return true;
            }
        }

        constexpr std::array<char, 4> drumKeys { 'Z', 'X', 'C', 'V' };
        for (size_t index = 0; index < drumKeys.size(); ++index)
        {
            if (character == drumKeys[index])
            {
                triggerDrum (static_cast<int> (index), true);
                return true;
            }
        }

        if (key == juce::KeyPress::spaceKey)
        {
            if (! spaceWasDown)
                triggerDrum (0, true);
            spaceWasDown = true;
            return true;
        }
        return false;
    }

    bool keyStateChanged (bool) override
    {
        for (size_t index = 0; index < keyChars.size(); ++index)
        {
            keyboardNotesDown[index] = juce::KeyPress::isKeyCurrentlyDown (std::tolower (keyChars[index]));
            updateCombinedLiveNote (index, "main-key-state");
        }
        spaceWasDown = juce::KeyPress::isKeyCurrentlyDown (juce::KeyPress::spaceKey);
        repaint();
        return true;
    }

    void focusLost (FocusChangeType) override
    {
        releaseAllLiveNotes();
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isPopupMenu())
        {
            if (const auto trackIndex = trackIndexAt (event.getPosition()); trackIndex >= 0)
            {
                selectTrack (trackIndex);
                showTrackContextMenu();
            }
            return;
        }

        resizingTrackIndex = trackBoundaryAt (event.getPosition());
        if (resizingTrackIndex >= 0)
        {
            resizeStartY = event.getPosition().y;
            resizeStartHeight = trackHeight (resizingTrackIndex);
            statusMessage = "Resize track";
            return;
        }

        const auto trackIndex = trackIndexAt (event.getPosition());
        if (trackIndex >= 0)
        {
            selectTrack (trackIndex);
            if (event.position.x >= contentLeft)
            {
                selectedNote = findTimelineNote (event.position);
                if (selectedNote != nullptr)
                {
                    dragStart = event.position;
                    dragOriginal = toTimelineNote (*selectedNote);
                    resizingNote = noteBounds (dragOriginal).getRight() - event.position.x <= 8.0f;
                    draggingNote = true;
                    statusMessage = resizingNote ? "Resize note" : "Move note";
                }
                else
                {
                    draggingPlayhead = true;
                    movePlayheadTo (event.position.x);
                }
            }
            repaint();
        }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (resizingTrackIndex >= 0)
        {
            const auto tracks = te::getAudioTracks (*edit);
            if (juce::isPositiveAndBelow (resizingTrackIndex, tracks.size()))
            {
                const auto isDrum = drumIndexFor (*tracks[resizingTrackIndex]) >= 0;
                const auto height = resizedTrackHeight (
                    isDrum, resizeStartHeight + event.getPosition().y - resizeStartY);
                tracks[resizingTrackIndex]->state.setProperty (trackHeightProperty, height, nullptr);
                repaint();
            }
        }
        else if (getTrackArea().contains (event.getMouseDownPosition()))
        {
            if (draggingNote)
                dragSelectedNote (event.position);
            else if (draggingPlayhead)
                movePlayheadTo (event.position.x);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        if (resizingTrackIndex >= 0)
        {
            statusMessage = "Track height updated";
            resizingTrackIndex = -1;
        }
        draggingNote = false;
        draggingPlayhead = false;
        if (statusMessage == "Move note" || statusMessage == "Resize note")
            statusMessage = selectedNote != nullptr ? "Note selected - drag or press Delete" : juce::String();
        repaint();
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        setMouseCursor (trackBoundaryAt (event.getPosition()) >= 0
                            ? juce::MouseCursor::UpDownResizeCursor
                            : juce::MouseCursor::NormalCursor);
    }

    void mouseExit (const juce::MouseEvent&) override
    {
        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

private:
    enum MenuIds
    {
        newProjectMenuId = 1,
        saveMenuId,
        saveAsMenuId,
        loadMenuId,
        keyboardPadMenuId,
        instrumentLogMenuId
    };

    void logInstrumentEvent (const juce::String& event)
    {
        if (instrumentLog != nullptr)
            instrumentLog->logMessage (juce::Time::getCurrentTime().toISO8601 (true) + " " + event);
    }

    void armAudioCapture (te::AudioTrack& track, const char* reason)
    {
        const auto capture = ++nextAudioCapture;
        auto count = 0;
        for (auto* tap : track.pluginList.getPluginsOfType<DiagnosticAudioTap>())
        {
            tap->armCapture (capture);
            ++count;
        }
        for (auto* tap : edit->getMasterPluginList().getPluginsOfType<DiagnosticAudioTap>())
        {
            tap->armCapture (capture);
            ++count;
        }
        logInstrumentEvent ("audio-capture-start id=" + juce::String (capture)
                            + " reason=" + juce::String (reason)
                            + " taps=" + juce::String (count)
                            + " " + instrumentStateForLog (track));
    }

    void drainAudioCapture()
    {
        const auto drain = [this] (DiagnosticAudioTap& tap, const juce::String& trackID)
        {
            const auto stage = static_cast<int> (tap.state.getProperty ("musicBitMakerTapStage", -1));
            const auto stageName = stage == 0 ? "post-synth" : stage == 1 ? "post-track" : "post-master";
            DiagnosticAudioTap::Window window;
            while (tap.popWindow (window))
            {
                const auto rms = window.samples > 0
                    ? std::sqrt (window.sumSquares / window.samples) : 0.0;
                logInstrumentEvent ("audio-window id=" + juce::String (window.capture)
                    + " stage=" + stageName
                    + " track=" + trackID
                    + " tap=" + tap.itemID.toString()
                    + " sequence=" + juce::String (window.sequence)
                    + " frames=" + juce::String (window.frames)
                    + " nonZero=" + juce::String (window.nonZero)
                    + " rms=" + juce::String (rms, 8)
                    + " peak=" + juce::String (window.peak, 8)
                    + " hash=" + juce::String::toHexString (window.hash));
            }
            if (const auto dropped = tap.takeDroppedCount(); dropped > 0)
                logInstrumentEvent ("audio-window-dropped track=" + trackID
                                    + " stage=" + juce::String (stage)
                                    + " count=" + juce::String (dropped));
        };
        for (auto* track : te::getAudioTracks (*edit))
            for (auto* tap : track->pluginList.getPluginsOfType<DiagnosticAudioTap>())
                drain (*tap, track->itemID.toString());
        for (auto* tap : edit->getMasterPluginList().getPluginsOfType<DiagnosticAudioTap>())
            drain (*tap, "master");
    }

    static constexpr int contentLeft = 260;
    static constexpr int trackRowTop = 194;

    te::AudioTrack* selectedTrack() const
    {
        const auto tracks = te::getAudioTracks (*edit);
        return juce::isPositiveAndBelow (selectedTrackIndex, tracks.size())
             ? tracks[selectedTrackIndex] : nullptr;
    }

    te::MidiClip* findMidiClip (te::AudioTrack& track) const
    {
        for (auto* clip : track.getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
                return midi;

        return nullptr;
    }

    te::MidiClip* createMidiClip (te::AudioTrack& track)
    {
        if (auto* existing = findMidiClip (track))
            return existing;

        auto state = juce::ValueTree (te::IDs::MIDICLIP);
        const auto range = tracktion::TimeRange (tracktion::TimePosition::fromSeconds (0.0),
                                                 tracktion::TimeDuration::fromSeconds (timelineLengthSeconds));
        auto* clip = dynamic_cast<te::MidiClip*> (track.insertClipWithState (
            state, "Keyboard", te::TrackItem::Type::midi,
            { range, tracktion::TimeDuration::fromSeconds (0.0) }, false, false));
        jassert (clip != nullptr);
        if (clip != nullptr)
            clip->setMidiChannel (te::MidiChannel (1));
        return clip;
    }

    te::FourOscPlugin* findInstrument (te::AudioTrack& track) const
    {
        return track.pluginList.findFirstPluginOfType<te::FourOscPlugin>();
    }

    int presetIndexFor (te::FourOscPlugin& synth) const
    {
        return juce::jlimit (0, static_cast<int> (synthPresets.size()) - 1,
                             static_cast<int> (synth.state.getProperty ("musicBitMakerPreset", 0)));
    }

    int drumIndexFor (te::AudioTrack& track) const
    {
        if (auto* synth = findInstrument (track))
            return static_cast<int> (synth->state.getProperty ("musicBitMakerDrumIndex", -1));
        return -1;
    }

    te::AudioTrack* drumTrack (int drumIndex) const
    {
        for (auto* track : te::getAudioTracks (*edit))
            if (drumIndexFor (*track) == drumIndex)
                return track;
        return nullptr;
    }

    te::FourOscPlugin* ensureInstrument (te::AudioTrack& track, int presetIndex)
    {
        if (auto* existing = findInstrument (track))
            return existing;

        auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
        auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin.get());
        if (synth == nullptr || ! configureSynth (*synth, presetIndex))
            return nullptr;

        track.pluginList.insertPlugin (plugin, 0, nullptr);
        return synth;
    }

    void ensureDiagnosticAudioTaps (te::AudioTrack& track)
    {
        auto* synth = findInstrument (track);
        if (synth == nullptr)
            return;

        for (int stage = 0; stage < 2; ++stage)
        {
            bool found = false;
            for (auto* tap : track.pluginList.getPluginsOfType<DiagnosticAudioTap>())
                found = found || static_cast<int> (tap->state.getProperty ("musicBitMakerTapStage", -1)) == stage;
            if (found)
                continue;

            auto plugin = edit->getPluginCache().createNewPlugin (DiagnosticAudioTap::xmlTypeName, {});
            if (auto* tap = dynamic_cast<DiagnosticAudioTap*> (plugin.get()))
            {
                tap->state.setProperty ("musicBitMakerTapStage", stage, nullptr);
                const auto index = stage == 0 ? track.pluginList.indexOf (synth) + 1
                                              : track.pluginList.size();
                track.pluginList.insertPlugin (plugin, index, nullptr);
            }
            else
            {
                logInstrumentEvent ("audio-tap-install-failed track=" + track.itemID.toString()
                                    + " stage=" + juce::String (stage));
            }
        }
    }

    void ensureMasterDiagnosticAudioTap()
    {
        auto& plugins = edit->getMasterPluginList();
        for (auto* tap : plugins.getPluginsOfType<DiagnosticAudioTap>())
            if (static_cast<int> (tap->state.getProperty ("musicBitMakerTapStage", -1)) == 2)
                return;

        auto plugin = edit->getPluginCache().createNewPlugin (DiagnosticAudioTap::xmlTypeName, {});
        if (auto* tap = dynamic_cast<DiagnosticAudioTap*> (plugin.get()))
        {
            tap->state.setProperty ("musicBitMakerTapStage", 2, nullptr);
            plugins.insertPlugin (plugin, plugins.size(), nullptr);
        }
        else
        {
            logInstrumentEvent ("audio-tap-install-failed track=master stage=2");
        }
    }

    void ensureDrumTracks()
    {
        for (int drumIndex = 0; drumIndex < static_cast<int> (drumPresets.size()); ++drumIndex)
        {
            if (drumTrack (drumIndex) != nullptr)
                continue;

            const auto oldCount = te::getAudioTracks (*edit).size();
            edit->ensureNumberOfAudioTracks (oldCount + 1);
            auto* track = te::getAudioTracks (*edit)[oldCount];
            track->setName (drumPresets[static_cast<size_t> (drumIndex)].name);
            createMidiClip (*track);

            auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
            auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin.get());
            if (synth != nullptr && configureDrumSynth (*synth, drumIndex))
                track->pluginList.insertPlugin (plugin, 0, nullptr);
            routeTrackToAudioOutput (*track);
        }
    }

    void initialiseEdit (bool addDefaultDrums = false)
    {
        const auto storedOctave = static_cast<int> (edit->state.getProperty (octaveProperty, 4));
        const auto usableOctave = juce::jlimit (minimumOctave, maximumOctave, storedOctave);
        if (storedOctave != usableOctave)
            edit->state.setProperty (octaveProperty, usableOctave, nullptr);
        if (addDefaultDrums)
        {
            edit->ensureNumberOfAudioTracks (1);
            ensureDrumTracks();
        }
        const auto tracks = te::getAudioTracks (*edit);
        for (int i = 0; i < tracks.size(); ++i)
        {
            auto* track = tracks[i];
            if (track->getName() == track->getNameAsTrackNumber())
                track->setName ("Keys " + juce::String (i + 1));
            createMidiClip (*track);
            if (drumIndexFor (*track) < 0)
                ensureInstrument (*track, i % static_cast<int> (synthPresets.size()));
            ensureDiagnosticAudioTaps (*track);
            routeTrackToAudioOutput (*track);
        }
        ensureMasterDiagnosticAudioTap();
        selectedTrackIndex = tracks.isEmpty()
                           ? -1
                           : juce::jlimit (0, tracks.size() - 1, selectedTrackIndex);
        midiClip = selectedTrack() != nullptr ? createMidiClip (*selectedTrack()) : nullptr;
        edit->playInStopEnabled = true;
        edit->getTransport().ensureContextAllocated (true);
    }

    void selectTrack (int index)
    {
        const auto tracks = te::getAudioTracks (*edit);
        if (! juce::isPositiveAndBelow (index, tracks.size()) || index == selectedTrackIndex)
            return;

        finishOpenNotes (currentPosition());
        releaseAllLiveNotes();
        selectedTrackIndex = index;
        midiClip = createMidiClip (*tracks[index]);
        selectedNote = nullptr;
        syncTrackControls();
        statusMessage = "Selected " + tracks[index]->getName();
        repaint();
        grabKeyboardFocus();
    }

    void addTrack()
    {
        const auto instrumentChoice = showInstrumentDialog ("Add Track", 0);
        if (instrumentChoice < 0)
        {
            grabKeyboardFocus();
            return;
        }

        finishOpenNotes (currentPosition());
        releaseAllLiveNotes();
        const auto oldCount = te::getAudioTracks (*edit).size();
        edit->ensureNumberOfAudioTracks (oldCount + 1);
        auto* track = te::getAudioTracks (*edit)[oldCount];
        track->setName (instrumentName (instrumentChoice));
        createMidiClip (*track);
        if (! configureTrackInstrument (*track, instrumentChoice))
        {
            edit->deleteTrack (track);
            statusMessage = "Could not add track";
            repaint();
            return;
        }
        ensureDiagnosticAudioTaps (*track);
        routeTrackToAudioOutput (*track);
        edit->getTransport().ensureContextAllocated (true);
        selectedTrackIndex = -1;
        selectTrack (oldCount);
        statusMessage = "Track added";
    }

    void deleteSelectedTrack()
    {
        auto tracks = te::getAudioTracks (*edit);
        if (tracks.size() <= 1 || selectedTrack() == nullptr)
        {
            statusMessage = "At least one track is required";
            repaint();
            return;
        }

        if (! juce::AlertWindow::showOkCancelBox (
                juce::AlertWindow::WarningIcon, "Delete Track",
                "Delete \"" + selectedTrack()->getName() + "\" and all of its notes?",
                "Delete", "Cancel"))
        {
            grabKeyboardFocus();
            return;
        }

        finishOpenNotes (currentPosition());
        releaseAllLiveNotes();
        edit->deleteTrack (selectedTrack());
        selectedTrackIndex = juce::jlimit (0, tracks.size() - 2, selectedTrackIndex);
        midiClip = createMidiClip (*selectedTrack());
        selectedNote = nullptr;
        edit->getTransport().ensureContextAllocated (true);
        syncTrackControls();
        statusMessage = "Track deleted";
        repaint();
    }

    void renameSelectedTrack()
    {
        auto* track = selectedTrack();
        if (track == nullptr)
            return;

        juce::AlertWindow dialog ("Rename Track", "Enter a track name", juce::MessageBoxIconType::NoIcon);
        dialog.addTextEditor ("name", track->getName());
        dialog.addButton ("Rename", 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        if (dialog.runModalLoop() == 1)
        {
            const auto name = dialog.getTextEditorContents ("name").trim();
            if (name.isNotEmpty())
                track->setName (name);
        }
        repaint();
        grabKeyboardFocus();
    }

    static bool isDrumInstrument (int instrumentChoice)
    {
        return instrumentChoice >= static_cast<int> (synthPresets.size());
    }

    static juce::String instrumentName (int instrumentChoice)
    {
        if (isDrumInstrument (instrumentChoice))
            return drumPresets[static_cast<size_t> (
                instrumentChoice - static_cast<int> (synthPresets.size()))].name;
        return synthPresets[static_cast<size_t> (instrumentChoice)].name;
    }

    static juce::StringArray instrumentNames()
    {
        juce::StringArray names;
        for (const auto& preset : synthPresets)
            names.add ("Synth - " + juce::String (preset.name));
        for (const auto& preset : drumPresets)
            names.add ("Drum - " + juce::String (preset.name));
        return names;
    }

    int currentInstrumentChoice (te::AudioTrack& track) const
    {
        if (const auto drumIndex = drumIndexFor (track); drumIndex >= 0)
            return static_cast<int> (synthPresets.size()) + drumIndex;
        if (auto* synth = findInstrument (track))
            return presetIndexFor (*synth);
        return 0;
    }

    int showInstrumentDialog (const juce::String& title, int selectedChoice)
    {
        juce::AlertWindow dialog (title, "Choose an instrument", juce::MessageBoxIconType::NoIcon);
        dialog.addComboBox ("instrument", instrumentNames(), "Instrument");
        if (auto* combo = dialog.getComboBoxComponent ("instrument"))
            combo->setSelectedItemIndex (selectedChoice, juce::dontSendNotification);
        dialog.addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
        dialog.addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        if (dialog.runModalLoop() != 1)
            return -1;
        if (auto* combo = dialog.getComboBoxComponent ("instrument"))
            return combo->getSelectedItemIndex();
        return -1;
    }

    bool configureTrackInstrument (te::AudioTrack& track, int instrumentChoice)
    {
        const auto presetIndex = isDrumInstrument (instrumentChoice) ? 0 : instrumentChoice;
        auto* synth = ensureInstrument (track, presetIndex);
        if (synth == nullptr)
            return false;
        return isDrumInstrument (instrumentChoice)
            ? configureDrumSynth (*synth, instrumentChoice - static_cast<int> (synthPresets.size()))
            : configureSynth (*synth, instrumentChoice);
    }

    void changeSelectedTrackInstrument()
    {
        auto* track = selectedTrack();
        if (track == nullptr)
            return;

        const auto oldChoice = currentInstrumentChoice (*track);
        const auto newChoice = showInstrumentDialog ("Change Instrument", oldChoice);
        logInstrumentEvent ("instrument-selection old=" + juce::String (oldChoice)
                            + " new=" + juce::String (newChoice)
                            + " playing=" + juce::String (edit->getTransport().isPlaying() ? 1 : 0)
                            + " position=" + juce::String (currentPosition(), 3)
                            + " " + instrumentStateForLog (*track));
        if (newChoice < 0 || newChoice == oldChoice)
        {
            grabKeyboardFocus();
            return;
        }

        const auto changesTrackKind = isDrumInstrument (oldChoice) != isDrumInstrument (newChoice);
        if (changesTrackKind
            && ! juce::AlertWindow::showOkCancelBox (
                juce::AlertWindow::WarningIcon, "Change Track Type",
                "Changing between a synthesizer and a drum clears all notes on this track. Continue?",
                "Change", "Cancel"))
        {
            logInstrumentEvent ("instrument-change-cancelled track=" + track->itemID.toString());
            grabKeyboardFocus();
            return;
        }

        finishOpenNotes (currentPosition());
        releaseAllLiveNotes ("instrument-change");
        if (! configureTrackInstrument (*track, newChoice))
        {
            logInstrumentEvent ("instrument-apply-failed " + instrumentStateForLog (*track));
            statusMessage = "Could not change instrument";
            repaint();
            return;
        }
        ensureDiagnosticAudioTaps (*track);

        for (auto* clip : track->getClips())
            if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
            {
                if (changesTrackKind)
                {
                    midi->getSequence().clear (&edit->getUndoManager());
                }
                else if (isDrumInstrument (newChoice))
                {
                    const auto newPitch = drumPresets[static_cast<size_t> (
                        newChoice - static_cast<int> (synthPresets.size()))].midiNote;
                    for (auto* note : midi->getSequence().getNotes())
                        note->setNoteNumber (newPitch, &edit->getUndoManager());
                }
            }

        if (changesTrackKind)
            track->state.removeProperty (trackHeightProperty, nullptr);
        selectedNote = nullptr;
        midiClip = createMidiClip (*track);
        edit->getTransport().ensureContextAllocated (true);
        armAudioCapture (*track, "instrument-change");
        logInstrumentEvent ("instrument-applied choice=" + juce::String (newChoice)
                            + " playing=" + juce::String (edit->getTransport().isPlaying() ? 1 : 0)
                            + " position=" + juce::String (currentPosition(), 3)
                            + " " + instrumentStateForLog (*track));
        syncTrackControls();
        statusMessage = "Instrument changed: " + instrumentName (newChoice);
        repaint();
        grabKeyboardFocus();
    }

    void showTrackContextMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Rename...");
        menu.addItem (2, "Change Instrument...");
        menu.addSeparator();
        menu.addItem (3, "Delete Track", te::getAudioTracks (*edit).size() > 1);
        juce::Component::SafePointer<MainComponent> safeThis (this);
        menu.showMenuAsync ({}, [safeThis] (int result)
        {
            if (safeThis == nullptr)
                return;
            if (result == 1) safeThis->renameSelectedTrack();
            else if (result == 2) safeThis->changeSelectedTrackInstrument();
            else if (result == 3) safeThis->deleteSelectedTrack();
        });
    }

    void syncTrackControls()
    {
        juce::ScopedValueSetter<bool> guard (updatingTrackControls, true);
        if (auto* track = selectedTrack())
        {
            if (auto* volume = track->getVolumePlugin())
            {
                volumeSlider.setValue (volume->getVolumeDb(), juce::dontSendNotification);
                panSlider.setValue (volume->getPan(), juce::dontSendNotification);
            }
            muteButton.setToggleState (track->isMuted (false), juce::dontSendNotification);
            soloButton.setToggleState (track->isSolo (false), juce::dontSendNotification);
        }
        syncKeyboardWindow();
    }

    void syncKeyboardWindow()
    {
        if (keyboardWindow == nullptr)
            return;
        keyboardWindow->setOctave (keyboardOctave());
        auto* track = selectedTrack();
        keyboardWindow->setTrackEnabled (track != nullptr && drumIndexFor (*track) < 0,
                                          track != nullptr ? track->getName() : juce::String ("No track"));
        keyboardWindow->setScale (
            static_cast<int> (edit->state.getProperty (scaleRootProperty, 0)),
            static_cast<int> (edit->state.getProperty (scaleTypeProperty, 0)));
    }

    void toggleKeyboardWindow()
    {
        if (keyboardWindow == nullptr)
            return;
        if (keyboardWindow->isVisible())
        {
            keyboardWindow->closeButtonPressed();
            return;
        }
        syncKeyboardWindow();
        keyboardWindow->setMinimised (false);
        keyboardWindow->setVisible (true);
        keyboardWindow->toFront (true);
        menuItemsChanged();
    }

    int trackIndexAt (juce::Point<int> position) const
    {
        if (! getTrackArea().contains (position))
            return -1;

        const auto tracks = te::getAudioTracks (*edit);
        for (int index = 0; index < tracks.size(); ++index)
            if (getTrackRowBounds (index).contains (position))
                return index;
        return -1;
    }

    int trackBoundaryAt (juce::Point<int> position) const
    {
        if (position.x < 24 || position.x >= getWidth() - 24)
            return -1;

        const auto tracks = te::getAudioTracks (*edit);
        for (int index = 0; index < tracks.size(); ++index)
            if (std::abs (position.y - getTrackRowBounds (index).getBottom()) <= 4)
                return index;
        return -1;
    }

    void addRecordedNote (int pitch, double startSeconds, double endSeconds)
    {
        if (auto* track = selectedTrack())
            addRecordedNote (*track, pitch, startSeconds, endSeconds);
    }

    void addRecordedNote (te::AudioTrack& track, int pitch, double startSeconds, double endSeconds)
    {
        auto* clip = createMidiClip (track);
        if (clip == nullptr)
            return;

        startSeconds = juce::jlimit (0.0, timelineLengthSeconds, startSeconds);
        endSeconds = juce::jlimit (startSeconds + 0.01, timelineLengthSeconds, endSeconds);
        const auto startBeat = edit->tempoSequence.toBeats (tracktion::TimePosition::fromSeconds (startSeconds));
        const auto endBeat = edit->tempoSequence.toBeats (tracktion::TimePosition::fromSeconds (endSeconds));
        clip->getSequence().addNote (pitch, startBeat, endBeat - startBeat, 100, 0, nullptr);
    }

    TimelineNote toTimelineNote (const te::MidiNote& note, const te::MidiClip& clip) const
    {
        return { note.getNoteNumber(), note.getEditStartTime (clip).inSeconds(),
                 note.getEditEndTime (clip).inSeconds() };
    }

    TimelineNote toTimelineNote (const te::MidiNote& note) const
    {
        jassert (midiClip != nullptr);
        return toTimelineNote (note, *midiClip);
    }

    juce::Rectangle<float> noteBounds (const TimelineNote& note, int trackIndex) const
    {
        const auto lane = laneForPitch (note.pitch, trackIndex);
        if (lane < 0)
            return {};

        const auto content = getTimelineContentBounds (trackIndex);
        const auto laneHeight = content.getHeight() / static_cast<float> (timelineLaneCount (trackIndex));
        const auto x = content.getX() + content.getWidth()
            * static_cast<float> (note.startSeconds / timelineLengthSeconds);
        const auto width = juce::jmax (4.0f, content.getWidth()
            * static_cast<float> ((note.endSeconds - note.startSeconds) / timelineLengthSeconds));
        return { x, content.getY() + lane * laneHeight + 2.0f, width, laneHeight - 4.0f };
    }

    juce::Rectangle<float> noteBounds (const TimelineNote& note) const
    {
        return noteBounds (note, selectedTrackIndex);
    }

    te::MidiNote* findTimelineNote (juce::Point<float> position) const
    {
        if (midiClip == nullptr)
            return nullptr;

        const auto& notes = midiClip->getSequence().getNotes();
        for (int index = notes.size(); --index >= 0;)
            if (noteBounds (toTimelineNote (*notes[index])).expanded (2.0f).contains (position))
                return notes[index];

        return nullptr;
    }

    int pitchForY (float y) const
    {
        const auto content = getTimelineContentBounds();
        if (auto* track = selectedTrack())
            if (const auto drumIndex = drumIndexFor (*track); drumIndex >= 0)
                return drumPresets[static_cast<size_t> (drumIndex)].midiNote;
        const auto count = timelineLaneCount (selectedTrackIndex);
        const auto lane = juce::jlimit (0, count - 1,
                                        static_cast<int> ((y - content.getY())
                                            / (content.getHeight() / count)));
        return timelinePitchRange (selectedTrackIndex).first + lane;
    }

    void dragSelectedNote (juce::Point<float> position)
    {
        if (selectedNote == nullptr || midiClip == nullptr)
            return;

        const auto delta = (position.x - dragStart.x) / getTimelineContentBounds().getWidth()
                         * timelineLengthSeconds;
        auto edited = editNoteRange (dragOriginal, delta, resizingNote);
        edited.pitch = resizingNote ? dragOriginal.pitch : pitchForY (position.y);

        const auto startBeat = edit->tempoSequence.toBeats (
            tracktion::TimePosition::fromSeconds (edited.startSeconds));
        const auto endBeat = edit->tempoSequence.toBeats (
            tracktion::TimePosition::fromSeconds (edited.endSeconds));
        selectedNote->setStartAndLength (startBeat, endBeat - startBeat, nullptr);
        selectedNote->setNoteNumber (edited.pitch, nullptr);
        repaint();
    }

    void deleteSelectedNote()
    {
        if (selectedNote == nullptr || midiClip == nullptr)
            return;

        midiClip->getSequence().removeNote (*selectedNote, &edit->getUndoManager());
        selectedNote = nullptr;
        draggingNote = false;
        statusMessage = "Note deleted";
        updateButtonStates();
        repaint();
        grabKeyboardFocus();
    }

    bool confirmDiscardChanges (const juce::String& action)
    {
        return ! edit->hasChangedSinceSaved()
            || juce::AlertWindow::showOkCancelBox (
                juce::AlertWindow::WarningIcon, action,
                "The current project has unsaved changes. Continue without saving?",
                action, "Cancel");
    }

    void createNewProject()
    {
        if (! confirmDiscardChanges ("Create New Project"))
            return;

        const auto rememberedPath = appProperties.getUserSettings()->getValue (lastNewProjectDirectoryKey);
        const auto suggested = projectDirectoryFor (rememberedPath)
            .getChildFile ("MusicBitMaker Project.tracktionedit");
        juce::FileChooser chooser ("Create MusicBitMaker Project", suggested, "*.tracktionedit");
        if (! chooser.browseForFileToSave (true))
        {
            grabKeyboardFocus();
            return;
        }

        const auto file = chooser.getResult().withFileExtension (te::editFileSuffix);
        auto newEdit = te::createEmptyEdit (engine, file);
        if (newEdit == nullptr)
        {
            statusMessage = "Could not create project";
            repaint();
            return;
        }
        clearAllTracks (*newEdit);
        finishOpenNotes (currentPosition());
        recording = false;
        edit->getTransport().stop (false, false);
        releaseAllLiveNotes();

        auto previousEdit = std::move (edit);
        const auto previousFile = currentProjectFile;
        edit = std::move (newEdit);
        selectedTrackIndex = -1;
        selectedNote = nullptr;
        midiClip = nullptr;
        currentProjectFile = file;
        initialiseEdit (false);

        if (te::EditFileOperations (*edit).save (true, true, false))
        {
            appProperties.getUserSettings()->setValue (
                lastNewProjectDirectoryKey, file.getParentDirectory().getFullPathName());
            appProperties.saveIfNeeded();
            statusMessage = "Created: " + file.getFileNameWithoutExtension();
        }
        else
        {
            edit->getTransport().stop (false, false);
            edit = std::move (previousEdit);
            currentProjectFile = previousFile;
            selectedTrackIndex = te::getAudioTracks (*edit).isEmpty() ? -1 : 0;
            midiClip = selectedTrack() != nullptr ? createMidiClip (*selectedTrack()) : nullptr;
            edit->getTransport().ensureContextAllocated (true);
            statusMessage = "Could not create project";
        }

        syncTrackControls();
        updateButtonStates();
        repaint();
        grabKeyboardFocus();
    }

    void saveProject()
    {
        if (recording)
        {
            finishOpenNotes (currentPosition());
            recording = false;
        }

        auto operations = te::EditFileOperations (*edit);
        bool saved = false;

        if (currentProjectFile == juce::File())
        {
            const auto suggested = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                .getChildFile ("MusicBitMaker.tracktionedit");
            juce::FileChooser chooser ("Save MusicBitMaker Project", suggested, "*.tracktionedit");
            if (chooser.browseForFileToSave (true))
            {
                auto file = chooser.getResult().withFileExtension (te::editFileSuffix);
                saved = operations.saveAs (file, true);
                if (saved)
                    currentProjectFile = file;
            }
        }
        else
        {
            saved = operations.save (true, true, false);
        }

        statusMessage = saved ? "Saved: " + currentProjectFile.getFileName()
                              : "Save cancelled or failed";
        updateButtonStates();
        repaint();
        grabKeyboardFocus();
    }

    void saveProjectAs()
    {
        if (recording)
        {
            finishOpenNotes (currentPosition());
            recording = false;
        }

        const auto suggested = currentProjectFile == juce::File()
                             ? juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                   .getChildFile ("MusicBitMaker.tracktionedit")
                             : currentProjectFile;
        juce::FileChooser chooser ("Save MusicBitMaker Project As", suggested, "*.tracktionedit");
        if (! chooser.browseForFileToSave (true))
        {
            grabKeyboardFocus();
            return;
        }

        const auto file = chooser.getResult().withFileExtension (te::editFileSuffix);
        const auto saved = te::EditFileOperations (*edit).saveAs (file, true);
        if (saved)
            currentProjectFile = file;
        statusMessage = saved ? "Saved as: " + file.getFileName() : "Save As failed";
        updateButtonStates();
        repaint();
        grabKeyboardFocus();
    }

    void chooseProjectToLoad()
    {
        if (! confirmDiscardChanges ("Open Project"))
            return;
        const auto start = currentProjectFile == juce::File()
                         ? juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                         : currentProjectFile;
        juce::FileChooser chooser ("Open MusicBitMaker Project", start, "*.tracktionedit");
        if (chooser.browseForFileToOpen())
            loadProject (chooser.getResult());
        else
            grabKeyboardFocus();
    }

    void loadProject (const juce::File& file)
    {
        auto loaded = te::loadEditFromFile (engine, file, te::Edit::forEditing);
        if (loaded == nullptr)
        {
            statusMessage = "Could not load project";
            repaint();
            return;
        }

        finishOpenNotes (currentPosition());
        recording = false;
        edit->getTransport().stop (false, false);
        releaseAllLiveNotes();
        edit = std::move (loaded);
        midiClip = nullptr;
        selectedNote = nullptr;
        selectedTrackIndex = 0;
        initialiseEdit (false);
        currentProjectFile = file;
        statusMessage = "Loaded: " + file.getFileName();
        syncTrackControls();
        updateButtonStates();
        repaint();
        grabKeyboardFocus();
    }

    void toggleRecording()
    {
        recording = ! recording;
        logInstrumentEvent ("recording " + juce::String (recording ? "started" : "stopped")
                            + " position=" + juce::String (currentPosition(), 3));
        if (recording)
        {
            if (currentPosition() >= timelineLengthSeconds - 0.02)
                edit->getTransport().setPosition (tracktion::TimePosition::fromSeconds (0.0));
            startPlayback();
        }
        else
        {
            finishOpenNotes (currentPosition());
        }
        updateButtonStates();
        grabKeyboardFocus();
    }

    void startPlayback()
    {
        auto& transport = edit->getTransport();
        if (currentPosition() >= timelineLengthSeconds - 0.02)
            transport.setPosition (tracktion::TimePosition::fromSeconds (0.0));
        if (! transport.isPlaying())
            transport.play (false);
        if (auto* track = selectedTrack())
        {
            armAudioCapture (*track, "transport-play");
            logInstrumentEvent ("transport-play position=" + juce::String (currentPosition(), 3)
                                + " " + instrumentStateForLog (*track));
        }
        updateButtonStates();
        grabKeyboardFocus();
    }

    void stopPlayback()
    {
        auto& transport = edit->getTransport();
        logInstrumentEvent ("transport-stop position=" + juce::String (currentPosition(), 3));
        finishOpenNotes (currentPosition());
        recording = false;
        transport.stop (false, false);
        transport.setPosition (tracktion::TimePosition::fromSeconds (0.0));
        updateButtonStates();
        repaint();
        grabKeyboardFocus();
    }

    double currentPosition() const
    {
        return juce::jlimit (0.0, timelineLengthSeconds, edit->getTransport().getPosition().inSeconds());
    }

    int keyboardOctave() const
    {
        return juce::jlimit (minimumOctave, maximumOctave,
                            static_cast<int> (edit->state.getProperty (octaveProperty, 4)));
    }

    void updateCombinedLiveNote (size_t index, const char* source = "forced-release")
    {
        const auto down = keyboardNotesDown[index];
        if (liveNotesDown[index] == down)
            return;
        if (down)
            if (auto* track = selectedTrack(); track != nullptr && drumIndexFor (*track) >= 0)
                return;

        liveNotesDown[index] = down;
        if (auto* track = selectedTrack())
        {
            const auto pitch = keyboardPitch (index, keyboardOctave());
            const auto message = down ? juce::MidiMessage::noteOn (1, pitch, static_cast<juce::uint8> (100))
                                      : juce::MidiMessage::noteOff (1, pitch);
            logInstrumentEvent ("live-midi source=" + juce::String (source)
                                + " event=" + (down ? "note-on" : "note-off")
                                + " pitch=" + juce::String (pitch)
                                + " octave=" + juce::String (keyboardOctave())
                                + " playing=" + juce::String (edit->getTransport().isPlaying() ? 1 : 0)
                                + " " + instrumentStateForLog (*track));
            if (down)
                armAudioCapture (*track, "keyboard-note-on");
            track->injectLiveMidiMessage (message, {});
        }
        if (recording)
        {
            if (down)
                recordingStarts[index] = currentPosition();
            else if (recordingStarts[index] >= 0.0)
            {
                addRecordedNote (keyboardPitch (index, keyboardOctave()), recordingStarts[index], currentPosition());
                recordingStarts[index] = -1.0;
            }
        }
        repaint();
    }

    void finishOpenNotes (double endSeconds)
    {
        for (size_t index = 0; index < recordingStarts.size(); ++index)
        {
            if (recordingStarts[index] >= 0.0)
            {
                addRecordedNote (keyboardPitch (index, keyboardOctave()), recordingStarts[index], endSeconds);
                recordingStarts[index] = -1.0;
            }
        }
    }

    void triggerDrum (int drumIndex, bool recordEvent)
    {
        if (! juce::isPositiveAndBelow (drumIndex, static_cast<int> (drumPresets.size())))
            return;
        auto* track = drumTrack (drumIndex);
        if (track == nullptr)
            return;

        const auto& drum = drumPresets[static_cast<size_t> (drumIndex)];
        armAudioCapture (*track, "drum-note-on");
        logInstrumentEvent ("drum-midi event=note-on pitch=" + juce::String (drum.midiNote)
                            + " " + instrumentStateForLog (*track));
        track->injectLiveMidiMessage (juce::MidiMessage::noteOn (1, drum.midiNote, static_cast<juce::uint8> (115)), {});
        juce::Timer::callAfterDelay (juce::roundToInt (drum.durationSeconds * 1000.0),
                                     [safe = juce::Component::SafePointer<MainComponent> (this), drumIndex]
        {
            if (safe != nullptr)
                if (auto* drumTrack = safe->drumTrack (drumIndex))
                {
                    safe->logInstrumentEvent ("drum-midi event=note-off pitch="
                        + juce::String (drumPresets[static_cast<size_t> (drumIndex)].midiNote)
                        + " " + instrumentStateForLog (*drumTrack));
                    drumTrack->injectLiveMidiMessage (
                        juce::MidiMessage::noteOff (1, drumPresets[static_cast<size_t> (drumIndex)].midiNote), {});
                }
        });
        if (recordEvent && recording)
            addRecordedNote (*track, drum.midiNote, currentPosition(), currentPosition() + drum.durationSeconds);
        statusMessage = juce::String (drum.name) + " triggered";
        repaint();
    }

    void releaseAllLiveNotes (const char* reason = "general")
    {
        for (size_t index = 0; index < midiNotes.size(); ++index)
        {
            keyboardNotesDown[index] = false;
            updateCombinedLiveNote (index);
        }
        for (auto* track : te::getAudioTracks (*edit))
        {
            logInstrumentEvent ("live-midi event=all-notes-off reason=" + juce::String (reason)
                                + " " + instrumentStateForLog (*track));
            track->injectLiveMidiMessage (juce::MidiMessage::allNotesOff (1), {});
        }
        spaceWasDown = false;
        repaint();
    }

    void movePlayheadTo (float mouseX)
    {
        auto timeline = getTimelineContentBounds();
        const auto proportion = juce::jlimit (0.0, 1.0,
            static_cast<double> (mouseX - timeline.getX()) / timeline.getWidth());
        finishOpenNotes (currentPosition());
        edit->getTransport().setPosition (
            tracktion::TimePosition::fromSeconds (proportion * timelineLengthSeconds));
        repaint();
    }

    void timerCallback() override
    {
        drainAudioCapture();
        const auto isPlaying = edit->getTransport().isPlaying();
        if (lastUiPlaying && ! isPlaying && recording)
        {
            finishOpenNotes (currentPosition());
            recording = false;
        }
        if (isPlaying && currentPosition() >= timelineLengthSeconds - 0.02)
            stopPlayback();

        lastUiPlaying = isPlaying;
        updateButtonStates();
        repaint();
        if (keyboardWindow != nullptr)
            keyboardWindow->repaintPad();
    }

    void updateButtonStates()
    {
        recordButton.setToggleState (recording, juce::dontSendNotification);
        playButton.setToggleState (edit->getTransport().isPlaying(), juce::dontSendNotification);
        recordButton.setButtonText (recording ? "RECORDING" : "RECORD");
        playButton.setButtonText (edit->getTransport().isPlaying() ? "PLAYING" : "PLAY");
        deleteButton.setEnabled (selectedNote != nullptr);
    }

    int trackHeight (int index) const
    {
        const auto tracks = te::getAudioTracks (*edit);
        if (! juce::isPositiveAndBelow (index, tracks.size()))
            return 0;
        const auto isDrum = drumIndexFor (*tracks[index]) >= 0;
        const auto defaultHeight = isDrum ? drumTrackDefaultHeight : synthTrackDefaultHeight;
        return resizedTrackHeight (isDrum,
            static_cast<int> (tracks[index]->state.getProperty (trackHeightProperty, defaultHeight)));
    }

    juce::Rectangle<int> getTrackArea() const
    {
        return { 24, trackRowTop, getWidth() - 48, getHeight() - trackRowTop - 24 };
    }

    juce::Rectangle<int> getTrackRowBounds (int index) const
    {
        auto y = trackRowTop;
        for (int i = 0; i < index; ++i)
            y += trackHeight (i);
        return { 24, y, getWidth() - 48, trackHeight (index) };
    }

    juce::Rectangle<float> getTimelineBounds (int trackIndex) const
    {
        return getTrackRowBounds (trackIndex).withLeft (contentLeft).withTrimmedBottom (4).toFloat();
    }

    juce::Rectangle<float> getTimelineContentBounds (int trackIndex) const
    {
        return getTimelineBounds (trackIndex).withTrimmedLeft (64.0f).withTrimmedTop (18.0f)
                                               .withTrimmedBottom (4.0f);
    }

    juce::Rectangle<float> getTimelineContentBounds() const
    {
        return getTimelineContentBounds (selectedTrackIndex);
    }

    std::pair<int, int> timelinePitchRange (int trackIndex) const
    {
        auto low = keyboardPitch (0, keyboardOctave());
        auto high = keyboardPitch (midiNotes.size() - 1, keyboardOctave());
        const auto tracks = te::getAudioTracks (*edit);
        if (juce::isPositiveAndBelow (trackIndex, tracks.size()))
            if (auto* clip = findMidiClip (*tracks[trackIndex]))
                for (auto* note : clip->getSequence().getNotes())
                {
                    low = juce::jmin (low, note->getNoteNumber());
                    high = juce::jmax (high, note->getNoteNumber());
                }
        return { low, high };
    }

    int timelineLaneCount (int trackIndex) const
    {
        const auto tracks = te::getAudioTracks (*edit);
        if (juce::isPositiveAndBelow (trackIndex, tracks.size())
            && drumIndexFor (*tracks[trackIndex]) >= 0)
            return 1;
        const auto range = timelinePitchRange (trackIndex);
        return range.second - range.first + 1;
    }

    int laneForPitch (int pitch, int trackIndex) const
    {
        const auto tracks = te::getAudioTracks (*edit);
        if (juce::isPositiveAndBelow (trackIndex, tracks.size()))
        {
            if (const auto drumIndex = drumIndexFor (*tracks[trackIndex]); drumIndex >= 0)
                return pitch == drumPresets[static_cast<size_t> (drumIndex)].midiNote ? 0 : -1;
        }
        const auto range = timelinePitchRange (trackIndex);
        return pitch >= range.first && pitch <= range.second ? pitch - range.first : -1;
    }

    int laneForPitch (int pitch) const
    {
        return laneForPitch (pitch, selectedTrackIndex);
    }

    void drawStatus (juce::Graphics& graphics)
    {
        const auto state = recording ? "RECORDING" : edit->getTransport().isPlaying() ? "PLAYING" : "STOPPED";
        auto project = currentProjectFile == juce::File() ? juce::String ("Untitled")
                                                           : currentProjectFile.getFileNameWithoutExtension();
        if (edit->hasChangedSinceSaved())
            project += " *";
        graphics.setColour (recording ? juce::Colour::fromRGB (255, 92, 92)
                                      : juce::Colour::fromRGB (151, 160, 180));
        graphics.setFont (juce::FontOptions (15.0f, juce::Font::bold));
        graphics.drawText (juce::String (state) + "   " + juce::String (currentPosition(), 2)
                               + " / 16.00 sec   |   " + project,
                           contentLeft, 62, getWidth() - contentLeft - 24, 24, juce::Justification::centredLeft);
        graphics.setColour (juce::Colour::fromRGB (151, 160, 180));
        graphics.setFont (juce::FontOptions (12.0f));
        graphics.drawText (statusMessage.isNotEmpty() ? statusMessage
                                                       : "Piano A-K | Drums Z/X/C/V | Drag notes to edit",
                           contentLeft, 134, getWidth() - contentLeft - 24, 18, juce::Justification::centredLeft);
    }

    void drawTrackManager (juce::Graphics& graphics)
    {
        graphics.setColour (juce::Colour::fromRGB (151, 160, 180));
        graphics.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        graphics.drawText ("TRACKS", 24, 174, 212, 20, juce::Justification::centredLeft);

        const auto tracks = te::getAudioTracks (*edit);
        graphics.saveState();
        graphics.reduceClipRegion (getTrackArea());
        for (int i = 0; i < tracks.size(); ++i)
        {
            auto row = getTrackRowBounds (i).withRight (contentLeft - 8).withTrimmedBottom (4);
            graphics.setColour (i == selectedTrackIndex ? juce::Colour::fromRGB (59, 91, 105)
                                                        : juce::Colour::fromRGB (29, 34, 45));
            graphics.fillRoundedRectangle (row.toFloat(), 6.0f);
            graphics.setColour (i == selectedTrackIndex ? juce::Colour::fromRGB (105, 225, 180)
                                                        : juce::Colour::fromRGB (190, 198, 214));
            graphics.setFont (juce::FontOptions (13.0f, juce::Font::bold));
            graphics.drawText (juce::String (i + 1) + "  " + tracks[i]->getName(),
                               row.reduced (10, 0), juce::Justification::centredLeft, true);
            if (tracks[i]->isMuted (false) || tracks[i]->isSolo (false))
            {
                graphics.setFont (juce::FontOptions (11.0f, juce::Font::bold));
                graphics.drawText (tracks[i]->isMuted (false) ? "M" : "S",
                                   row.removeFromRight (24), juce::Justification::centred);
            }

            const auto boundary = getTrackRowBounds (i).getBottom();
            graphics.setColour (juce::Colour::fromRGB (72, 80, 99));
            graphics.fillRect (24, boundary - 2, getWidth() - 48, 4);
        }
        graphics.restoreState();
    }

    void drawTimeline (juce::Graphics& graphics)
    {
        const auto tracks = te::getAudioTracks (*edit);
        graphics.saveState();
        graphics.reduceClipRegion (getTrackArea());
        for (int trackIndex = 0; trackIndex < tracks.size(); ++trackIndex)
        {
            const auto bounds = getTimelineBounds (trackIndex);
            const auto content = getTimelineContentBounds (trackIndex);
            const auto laneCount = timelineLaneCount (trackIndex);
            const auto laneHeight = content.getHeight() / laneCount;
            const auto drumIndex = drumIndexFor (*tracks[trackIndex]);

            graphics.setColour (trackIndex == selectedTrackIndex ? juce::Colour::fromRGB (32, 45, 53)
                                                                  : juce::Colour::fromRGB (29, 34, 45));
            graphics.fillRoundedRectangle (bounds, 6.0f);

            for (int lane = 0; lane < laneCount; ++lane)
            {
                const auto y = content.getY() + lane * laneHeight;
                graphics.setColour (lane % 2 == 0 ? juce::Colour::fromRGB (35, 41, 54)
                                                   : juce::Colour::fromRGB (31, 37, 49));
                graphics.fillRect (content.getX(), y, content.getWidth(), laneHeight);
                graphics.setColour (juce::Colour::fromRGB (72, 80, 99));
                graphics.drawHorizontalLine (juce::roundToInt (y), content.getX(), content.getRight());
                graphics.setColour (juce::Colour::fromRGB (172, 181, 201));
                graphics.setFont (juce::FontOptions (juce::jmin (12.0f, laneHeight), juce::Font::bold));
                const auto label = drumIndex >= 0
                    ? juce::String (drumPresets[static_cast<size_t> (drumIndex)].name)
                    : pitchLabel (timelinePitchRange (trackIndex).first + lane);
                graphics.drawText (label, juce::Rectangle<float> { bounds.getX() + 5.0f, y, 54.0f, laneHeight },
                                   juce::Justification::centredRight);
            }

            for (int second = 0; second <= static_cast<int> (timelineLengthSeconds); ++second)
            {
                const auto x = content.getX() + content.getWidth() * second
                    / static_cast<float> (timelineLengthSeconds);
                graphics.setColour (second % 4 == 0 ? juce::Colour::fromRGB (91, 101, 123)
                                                     : juce::Colour::fromRGB (56, 64, 80));
                graphics.drawVerticalLine (juce::roundToInt (x), content.getY(), content.getBottom());
                if (second % 4 == 0)
                {
                    graphics.setFont (juce::FontOptions (10.0f));
                    graphics.setColour (juce::Colour::fromRGB (172, 181, 201));
                    graphics.drawText (juce::String (second) + "s",
                                       juce::Rectangle<float> { x + 3.0f, bounds.getY(), 28.0f, 16.0f },
                                       juce::Justification::centredLeft);
                }
            }

            if (auto* clip = findMidiClip (*tracks[trackIndex]))
            {
                for (auto* midiNote : clip->getSequence().getNotes())
                {
                    const auto note = toTimelineNote (*midiNote, *clip);
                    const auto rectangle = noteBounds (note, trackIndex);
                    if (rectangle.isEmpty())
                        continue;

                    graphics.setColour (drumIndex >= 0 ? juce::Colour::fromRGB (255, 163, 82)
                                                       : juce::Colour::fromRGB (105, 225, 180));
                    graphics.fillRoundedRectangle (rectangle, 3.0f);

                    if (trackIndex == selectedTrackIndex && midiNote == selectedNote)
                    {
                        graphics.setColour (juce::Colours::white);
                        graphics.drawRoundedRectangle (rectangle.expanded (1.0f), 3.0f, 2.0f);
                        graphics.fillRect (rectangle.getRight() - 5.0f, rectangle.getY(),
                                           5.0f, rectangle.getHeight());
                    }
                }
            }

            const auto playheadX = content.getX() + content.getWidth()
                * static_cast<float> (currentPosition() / timelineLengthSeconds);
            graphics.setColour (recording ? juce::Colour::fromRGB (255, 76, 76)
                                          : juce::Colour::fromRGB (255, 213, 86));
            graphics.fillRect (playheadX - 1.0f, content.getY(), 2.0f, content.getHeight());
        }
        graphics.restoreState();
    }

    bool isNoteVisuallyDown (size_t index) const
    {
        if (liveNotesDown[index])
            return true;
        if (! edit->getTransport().isPlaying() || midiClip == nullptr)
            return false;

        const auto position = currentPosition();
        return std::any_of (midiClip->getSequence().getNotes().begin(),
                            midiClip->getSequence().getNotes().end(),
                            [position, pitch = keyboardPitch (index, keyboardOctave()), this] (const auto* note)
        {
            return note->getNoteNumber() == pitch
                && note->getEditStartTime (*midiClip).inSeconds() <= position
                && note->getEditEndTime (*midiClip).inSeconds() > position;
        });
    }

    void showAudioSettings()
    {
        auto* selector = new juce::AudioDeviceSelectorComponent (engine.getDeviceManager().deviceManager, 0, 0, 0, 2,
                                                                 false, false, true, false);
        selector->setSize (520, 360);
        juce::DialogWindow::LaunchOptions options;
        options.dialogTitle = "Audio Settings";
        options.dialogBackgroundColour = juce::Colour::fromRGB (30, 34, 45);
        options.content.setOwned (selector);
        options.escapeKeyTriggersCloseButton = true;
        options.useNativeTitleBar = true;
        options.resizable = false;
        options.launchAsync();
    }

    te::Engine& engine;
    juce::ApplicationProperties appProperties;
    std::unique_ptr<juce::FileLogger> instrumentLog;
    std::unique_ptr<te::Edit> edit;
    std::unique_ptr<KeyboardPadWindow> keyboardWindow;
    juce::MenuBarComponent menuBar { this };
    te::MidiClip* midiClip = nullptr;
    juce::TextButton recordButton { "RECORD" }, playButton { "PLAY" }, stopButton { "STOP" };
    juce::TextButton deleteButton { "DELETE" }, kickButton { "KICK Z" }, snareButton { "SNARE X" };
    juce::TextButton closedHatButton { "HAT C" }, openHatButton { "HAT V" };
    juce::TextButton addTrackButton { "+ ADD TRACK" };
    juce::TextButton settingsButton { "AUDIO SETTINGS" };
    juce::Slider volumeSlider, panSlider;
    juce::ToggleButton muteButton { "MUTE" }, soloButton { "SOLO" };
    std::array<bool, midiNotes.size()> keyboardNotesDown {};
    std::array<bool, midiNotes.size()> liveNotesDown {};
    std::array<double, midiNotes.size()> recordingStarts = []
    {
        std::array<double, midiNotes.size()> starts {};
        starts.fill (-1.0);
        return starts;
    }();
    juce::File currentProjectFile;
    juce::String statusMessage;
    te::MidiNote* selectedNote = nullptr;
    TimelineNote dragOriginal;
    juce::Point<float> dragStart;
    int selectedTrackIndex = 0;
    int resizingTrackIndex = -1;
    int resizeStartY = 0;
    int resizeStartHeight = 0;
    bool draggingNote = false;
    bool draggingPlayhead = false;
    bool resizingNote = false;
    bool spaceWasDown = false;
    bool recording = false;
    bool lastUiPlaying = false;
    bool updatingTrackControls = false;
    std::uint32_t nextAudioCapture = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};

std::vector<float> renderSynthForTest (te::Engine& engine, int presetIndex, bool drumPreset = false,
                                       float volumeDb = 0.0f, float pan = 0.0f)
{
    auto edit = te::createEmptyEdit (engine, {});
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit)[0];

    auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
    auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin.get());
    if (synth == nullptr || ! configureSynth (*synth, 0))
        return {};
    track->pluginList.insertPlugin (plugin, 0, nullptr);
    if (drumPreset ? ! configureDrumSynth (*synth, presetIndex)
                   : ! configureSynth (*synth, presetIndex))
        return {};
    if (auto* volume = track->getVolumePlugin())
    {
        volume->setVolumeDb (volumeDb);
        volume->setPan (pan);
    }
    else
    {
        return {};
    }

    auto* clip = dynamic_cast<te::MidiClip*> (track->insertClipWithState (
        juce::ValueTree (te::IDs::MIDICLIP), "Preset test", te::TrackItem::Type::midi,
        { tracktion::TimeRange (tracktion::TimePosition::fromSeconds (0.0),
                                tracktion::TimeDuration::fromSeconds (2.0)),
          tracktion::TimeDuration::fromSeconds (0.0) }, false, false));
    if (clip == nullptr)
        return {};
    const auto note = drumPreset ? drumPresets[static_cast<size_t> (presetIndex)].midiNote : 60;
    clip->getSequence().addNote (note, tracktion::BeatPosition::fromBeats (0.0),
                                 tracktion::BeatDuration::fromBeats (3.0), 100, 0, nullptr);

    const auto file = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile (
        "MusicBitMakerPresetTest", ".wav", false);
    te::Renderer::Parameters params (*edit);
    params.destFile = file;
    params.audioFormat = engine.getAudioFileFormatManager().getWavFormat();
    params.time = tracktion::TimeRange (tracktion::TimePosition::fromSeconds (0.0),
                                        tracktion::TimeDuration::fromSeconds (2.0));
    params.sampleRateForAudio = 44100.0;
    params.bitDepth = 16;

    std::atomic<float> progress { 0.0f };
    te::Renderer::RenderTask task ("Preset audio test", params, &progress, nullptr);
    while (task.runJob() != juce::ThreadPoolJob::jobHasFinished) {}

    std::vector<float> samples;
    if (auto reader = std::unique_ptr<juce::AudioFormatReader> (
            engine.getAudioFileFormatManager().readFormatManager.createReaderFor (file)))
    {
        juce::AudioBuffer<float> buffer (static_cast<int> (reader->numChannels),
                                         static_cast<int> (reader->lengthInSamples));
        if (reader->read (&buffer, 0, buffer.getNumSamples(), 0, true, false))
            samples.assign (buffer.getReadPointer (0), buffer.getReadPointer (0) + buffer.getNumSamples());
    }
    file.deleteFile();
    return samples;
}

bool runPresetAudioTest()
{
    te::Engine engine { "MusicBitMakerPresetAudioTest" };
    const auto keys = renderSynthForTest (engine, 0);
    const auto lead = renderSynthForTest (engine, 2);
    const auto pad = renderSynthForTest (engine, 4);
    const auto quiet = renderSynthForTest (engine, 0, false, -18.0f, 0.0f);
    const auto right = renderSynthForTest (engine, 0, false, 0.0f, 1.0f);
    const auto kick = renderSynthForTest (engine, 0, true);
    const auto closedHat = renderSynthForTest (engine, 2, true);
    if (keys.empty() || keys.size() != lead.size() || keys.size() != pad.size()
        || keys.size() != quiet.size() || keys.size() != right.size()
        || keys.size() != kick.size() || keys.size() != closedHat.size())
        return false;

    const auto difference = [] (const auto& left, const auto& right)
    {
        double total = 0.0;
        for (size_t i = 0; i < left.size(); ++i)
            total += std::abs (left[i] - right[i]);
        return total / static_cast<double> (left.size());
    };
    const auto level = [] (const auto& samples)
    {
        double total = 0.0;
        for (const auto sample : samples)
            total += std::abs (sample);
        return total / static_cast<double> (samples.size());
    };
    const auto peak = [] (const auto& samples)
    {
        float maximum = 0.0f;
        for (const auto sample : samples)
            maximum = juce::jmax (maximum, std::abs (sample));
        return maximum;
    };
    return difference (keys, lead) > 0.01 && difference (keys, pad) > 0.01
        && peak (kick) > 0.05f && peak (closedHat) > 0.05f
        && difference (kick, closedHat) > 0.001
        && level (quiet) < level (keys) * 0.2 && level (right) < level (keys) * 0.1;
}

int runProjectRoundTripTest()
{
    te::Engine engine { "MusicBitMakerSelfTest" };
    const auto projectFile = juce::File::getCurrentWorkingDirectory().getNonexistentChildFile (
        "MusicBitMakerProjectTest", ".tracktionedit", false);
    auto edit = te::createEmptyEdit (engine, projectFile);
    edit->ensureNumberOfAudioTracks (2);
    auto tracks = te::getAudioTracks (*edit);
    auto* track = tracks[0];
    tracks[0]->setName ("Lead");
    tracks[1]->setName ("Pad");
    tracks[0]->state.setProperty (trackHeightProperty, 188, nullptr);
    tracks[1]->state.setProperty (trackHeightProperty, 96, nullptr);
    edit->state.setProperty (scaleRootProperty, 9, nullptr);
    edit->state.setProperty (scaleTypeProperty, 1, nullptr);

    auto conversionPlugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
    auto* conversionSynth = dynamic_cast<te::FourOscPlugin*> (conversionPlugin.get());
    if (conversionSynth == nullptr
        || ! configureDrumSynth (*conversionSynth, 2)
        || static_cast<int> (conversionSynth->state.getProperty ("musicBitMakerDrumIndex", -1)) != 2
        || ! configureSynth (*conversionSynth, 1)
        || static_cast<int> (conversionSynth->state.getProperty ("musicBitMakerDrumIndex", -1)) != -1)
        return 25;

    const auto addSynth = [&] (te::AudioTrack& destination, int presetIndex)
    {
        auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
        auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin.get());
        if (synth == nullptr || ! configureSynth (*synth, presetIndex))
            return false;
        destination.pluginList.insertPlugin (plugin, 0, nullptr);
        return true;
    };

    if (! addSynth (*tracks[0], 2) || ! addSynth (*tracks[1], 4))
        return 11;
    if (auto* firstSynth = tracks[0]->pluginList.findFirstPluginOfType<te::FourOscPlugin>())
        firstSynth->deleteFromParent();
    if (! addSynth (*tracks[0], 3))
        return 18;
    if (auto* volume = tracks[1]->getVolumePlugin())
    {
        volume->setVolumeDb (-6.0f);
        volume->setPan (0.25f);
    }

    auto* clip = dynamic_cast<te::MidiClip*> (track->insertClipWithState (
        juce::ValueTree (te::IDs::MIDICLIP), "Test", te::TrackItem::Type::midi,
        { tracktion::TimeRange (tracktion::TimePosition::fromSeconds (0.0),
                                tracktion::TimeDuration::fromSeconds (4.0)),
          tracktion::TimeDuration::fromSeconds (0.0) }, false, false));

    if (clip == nullptr)
        return 12;

    clip->getSequence().addNote (60, tracktion::BeatPosition::fromBeats (0.0),
                                 tracktion::BeatDuration::fromBeats (1.0), 100, 0, nullptr);
    if (! te::EditFileOperations (*edit).save (false, true, false))
        return 20;
    const auto projectFileWritten = projectFile.existsAsFile();
    edit.reset();

    auto loaded = te::loadEditFromFile (engine, projectFile, te::Edit::forEditing);
    auto noteCount = 0;
    auto tracksRestored = false;
    auto synthsRestored = false;
    auto mixerRestored = false;
    auto scaleRestored = false;
    auto trackHeightsRestored = false;
    if (loaded != nullptr)
    {
        const auto loadedTracks = te::getAudioTracks (*loaded);
        scaleRestored = static_cast<int> (loaded->state.getProperty (scaleRootProperty, -1)) == 9
                     && static_cast<int> (loaded->state.getProperty (scaleTypeProperty, -1)) == 1;
        tracksRestored = loadedTracks.size() == 2
                      && loadedTracks[0]->getName() == "Lead"
                      && loadedTracks[1]->getName() == "Pad";
        trackHeightsRestored = loadedTracks.size() == 2
                            && static_cast<int> (loadedTracks[0]->state.getProperty (trackHeightProperty, 0)) == 188
                            && static_cast<int> (loadedTracks[1]->state.getProperty (trackHeightProperty, 0)) == 96;
        for (auto* loadedTrack : te::getAudioTracks (*loaded))
            for (auto* loadedClip : loadedTrack->getClips())
                if (auto* midi = dynamic_cast<te::MidiClip*> (loadedClip))
                    noteCount += midi->getSequence().getNumNotes();
        if (loadedTracks.size() == 2)
        {
            auto* beep = loadedTracks[0]->pluginList.findFirstPluginOfType<te::FourOscPlugin>();
            auto* pad = loadedTracks[1]->pluginList.findFirstPluginOfType<te::FourOscPlugin>();
            synthsRestored = beep != nullptr && pad != nullptr
                          && static_cast<int> (beep->state.getProperty ("musicBitMakerPreset", -1)) == 3
                          && static_cast<int> (beep->state.getProperty ("waveShape1", -1)) == 1
                          && static_cast<bool> (beep->state.getProperty ("delayOn", false))
                          && static_cast<int> (pad->state.getProperty ("musicBitMakerPreset", -1)) == 4
                          && static_cast<int> (pad->state.getProperty ("waveShape1", -1)) == 3
                          && static_cast<bool> (pad->state.getProperty ("chorusOn", false));
            if (auto* volume = loadedTracks[1]->getVolumePlugin())
                mixerRestored = std::abs (volume->getVolumeDb() + 6.0f) < 0.01f
                             && std::abs (volume->getPan() - 0.25f) < 0.01f;
        }
    }

    loaded.reset();
    projectFile.deleteFile();
    if (! projectFileWritten) return 13;
    if (noteCount != 1) return 14;
    if (! tracksRestored) return 15;
    if (! synthsRestored) return 16;
    if (! mixerRestored) return 17;
    if (! scaleRestored) return 21;
    if (! trackHeightsRestored) return 23;
    return 0;
}

int renderSynthDemos()
{
    const auto folder = juce::File::getCurrentWorkingDirectory().getChildFile ("build/synth-demos");
    if (! folder.createDirectory()) return 40;
    const auto log = folder.getChildFile ("render-log.txt");
    log.replaceWithText ("FourOsc direct offline rendering; unchanged application presets\n");
    te::Engine engine { "MusicBitMakerSynthDemos" };
    constexpr double sampleRate = 44100.0;
    constexpr int blockSize = 256;
    constexpr int totalSamples = 12 * 44100;
    constexpr std::array<int, 12> melody { 60, 64, 67, 72, 71, 67, 65, 69, 67, 64, 62, 60 };
    for (int preset = 0; preset < static_cast<int> (synthPresets.size()); ++preset)
    {
        const auto name = juce::String (synthPresets[static_cast<size_t> (preset)].name);
        log.appendText ("Start: " + name + "\n");
        auto edit = te::createEmptyEdit (engine, {});
        auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
        auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin.get());
        if (synth == nullptr || ! configureSynth (*synth, preset)) return 41;
        synth->initialise ({ tracktion::TimePosition::fromSeconds (0.0), sampleRate, blockSize });
        juce::AudioBuffer<float> result (2, totalSamples);
        result.clear();
        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            const auto count = juce::jmin (blockSize, totalSamples - offset);
            juce::AudioBuffer<float> block (2, count);
            block.clear();
            te::MidiMessageArray midi;
            const auto addEvent = [&] (int sample, const juce::MidiMessage& message)
            {
                if (sample >= offset && sample < offset + count)
                    midi.addMidiMessage (message, static_cast<double> (sample - offset), {});
            };
            // Eight seconds of identical notes, followed by four seconds of release/effect tail.
            for (size_t note = 0; note < melody.size(); ++note)
            {
                const auto start = static_cast<int> (note) * 22050;
                const auto length = note == melody.size() - 1 ? 110250 : 17640;
                addEvent (start, juce::MidiMessage::noteOn (1, melody[note], static_cast<juce::uint8> (100)));
                addEvent (start + length, juce::MidiMessage::noteOff (1, melody[note]));
            }
            synth->applyToBuffer ({ &block, juce::AudioChannelSet::stereo(), 0, count, &midi, 0.0,
                { tracktion::TimePosition::fromSeconds (offset / sampleRate),
                  tracktion::TimeDuration::fromSeconds (count / sampleRate) }, true, false, true, false });
            for (int channel = 0; channel < 2; ++channel)
                result.copyFrom (channel, offset, block, channel, 0, count);
        }
        synth->deinitialise();
        // Identical headroom for all presets; preserve relative loudness without PCM clipping.
        result.applyGain (0.25f);
        const auto file = folder.getChildFile (juce::String (preset + 1) + "-" + name.replaceCharacter (' ', '-') + ".wav");
        file.deleteFile();
        auto stream = file.createOutputStream();
        if (stream == nullptr) return 42;
        juce::WavAudioFormat wav;
        std::unique_ptr<juce::AudioFormatWriter> writer (wav.createWriterFor (stream.get(), sampleRate, 2, 24, {}, 0));
        if (writer == nullptr) return 43;
        stream.release();
        if (! writer->writeFromAudioSampleBuffer (result, 0, totalSamples)) return 44;
        writer.reset();
        log.appendText ("Saved: " + file.getFileName() + "\n");
    }
    return 0;
}

int runKeyboardOctaveTest()
{
    int octave = 4;
    std::vector<int> events;
    KeyboardPadComponent pad (
        [&] (size_t index, bool down) { events.push_back ((down ? 1 : -1) * keyboardPitch (index, octave)); },
        [] (size_t) { return false; }, [] (int, int) {},
        [&] (int value) { octave = value; });
    pad.keyPressed (juce::KeyPress ('A', {}, 'a'));
    pad.keyPressed (juce::KeyPress ('Z', {}, 'z'));
    if (octave != 3 || events != std::vector<int> { 60, -60 }) return 27;
    pad.keyPressed (juce::KeyPress ('A', {}, 'a'));
    pad.keyPressed (juce::KeyPress ('X', {}, 'x'));
    if (octave != 4 || events != std::vector<int> { 60, -60, 48, -48 }) return 28;
    auto* box = dynamic_cast<juce::ComboBox*> (pad.getChildComponent (0));
    if (box == nullptr) return 29;
    box->setSelectedId (7, juce::sendNotificationSync);
    if (octave != 6 || keyboardPitch (0, octave) != 84 || pitchLabel (84) != "C6"
        || compactPitchLabel (85) != "C#6") return 30;
    for (int i = 0; i < 12; ++i) pad.keyPressed (juce::KeyPress ('X', {}, 'x'));
    if (octave != maximumOctave || keyboardPitch (midiNotes.size() - 1, octave) != 119) return 31;
    for (int i = 0; i < 12; ++i) pad.keyPressed (juce::KeyPress ('Z', {}, 'z'));
    if (octave != minimumOctave || keyboardPitch (0, octave) != 12
        || keyboardPitch (midiNotes.size() - 1, octave) != 47) return 32;
    const auto nearBottom = [&pad] (size_t index)
    {
        const auto bounds = pad.getKeyBounds (index);
        return juce::Point<float> (static_cast<float> (bounds.getCentreX()),
                                   static_cast<float> (bounds.getBottom() - 5));
    };
    for (size_t index = 0; index < midiNotes.size(); ++index)
    {
        const auto black = std::find (blackNoteIndices.begin(), blackNoteIndices.end(),
                                      static_cast<int> (index)) != blackNoteIndices.end();
        const auto point = black ? pad.getKeyBounds (index).getCentre().toFloat()
                                 : nearBottom (index);
        if (pad.noteAt (point) != static_cast<int> (index)) return 33;
    }
    pad.updateMouseNote (24);
    pad.updateMouseNote (35);
    pad.updateMouseNote (-1);
    return events == std::vector<int> { 60, -60, 48, -48, 36, -36, 47, -47 } ? 0 : 34;
}

int runEmptyProjectTest()
{
    te::Engine engine { "MusicBitMakerEmptyProjectTest" };
    const auto projectFile = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("MusicBitMakerEmptyProjectTest", ".tracktionedit", false);
    auto edit = te::createEmptyEdit (engine, projectFile);
    clearAllTracks (*edit);
    edit->state.setProperty (octaveProperty, 2, nullptr);
    const auto saved = te::EditFileOperations (*edit).save (false, true, false);
    edit.reset();
    auto loaded = saved ? te::loadEditFromFile (engine, projectFile, te::Edit::forEditing) : nullptr;
    const auto missingDirectory = projectFile.getSiblingFile ("MissingDirectoryForFallback");
    const auto passed = loaded != nullptr
                     && static_cast<int> (loaded->state.getProperty (octaveProperty, -1)) == 2
                     && te::getAudioTracks (*loaded).isEmpty()
                     && projectDirectoryFor (projectFile.getParentDirectory().getFullPathName())
                            == projectFile.getParentDirectory()
                     && projectDirectoryFor (missingDirectory.getFullPathName())
                            == juce::File::getSpecialLocation (juce::File::userDocumentsDirectory);
    loaded.reset();
    projectFile.deleteFile();
    return passed ? 0 : 26;
}

int runInstrumentLogTest()
{
    te::Engine engine { "MusicBitMakerInstrumentLogTest" };
    auto edit = te::createEmptyEdit (engine, {});
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit)[0];
    track->getOutput().setOutputToDefaultDevice (true);
    if (! track->getOutput().usesDefaultMIDIOut())
        return 52;
    routeTrackToAudioOutput (*track);
    if (! track->getOutput().usesDefaultAudioOut() || track->getOutput().usesDefaultMIDIOut())
        return 53;
    auto plugin = edit->getPluginCache().createNewPlugin (te::FourOscPlugin::xmlTypeName, {});
    auto* synth = dynamic_cast<te::FourOscPlugin*> (plugin.get());
    if (synth == nullptr || ! configureSynth (*synth, 0))
        return 45;
    track->pluginList.insertPlugin (plugin, 0, nullptr);

    const auto before = instrumentStateForLog (*track);
    if (! configureSynth (*synth, 2))
        return 46;
    const auto after = instrumentStateForLog (*track);
    if (! before.contains ("preset=0") || ! after.contains ("preset=2")
        || ! after.contains ("defaultAudioOut=1")
        || ! after.contains ("wave1=2") || ! after.contains ("distortionOn=1")
        || before == after)
        return 47;

    const auto file = juce::File::getSpecialLocation (juce::File::tempDirectory)
        .getNonexistentChildFile ("MusicBitMakerInstrumentLogTest", ".log", false);
    {
        juce::FileLogger logger (file, "Instrument log test", 0);
        logger.logMessage ("instrument-applied " + after);
        logger.logMessage ("live-midi event=note-on pitch=60 " + after);
    }
    const auto contents = file.loadFileAsString();
    file.deleteFile();
    return contents.contains ("instrument-applied track=")
        && contents.contains ("live-midi event=note-on pitch=60 track=") ? 0 : 48;
}

int runAudioTapTest()
{
    te::Engine engine { "MusicBitMakerAudioTapTest" };
    engine.getPluginManager().createBuiltInType<DiagnosticAudioTap>();
    auto edit = te::createEmptyEdit (engine, {});
    auto plugin = edit->getPluginCache().createNewPlugin (DiagnosticAudioTap::xmlTypeName, {});
    auto* tap = dynamic_cast<DiagnosticAudioTap*> (plugin.get());
    if (tap == nullptr)
        return 49;

    juce::AudioBuffer<float> buffer (2, static_cast<int> (DiagnosticAudioTap::windowFrames));
    for (int channel = 0; channel < 2; ++channel)
        for (int frame = 0; frame < buffer.getNumSamples(); ++frame)
            buffer.setSample (channel, frame, channel == 0 ? 0.25f : -0.25f);

    const auto context = te::PluginRenderContext (
        &buffer, juce::AudioChannelSet::stereo(), 0, buffer.getNumSamples(), nullptr, 0.0,
        { tracktion::TimePosition::fromSeconds (0.0),
          tracktion::TimeDuration::fromSeconds (buffer.getNumSamples() / 44100.0) },
        false, false, false, false);
    tap->armCapture (1);
    tap->applyToBuffer (context);
    DiagnosticAudioTap::Window sounding;
    if (! tap->popWindow (sounding) || sounding.capture != 1
        || sounding.frames != DiagnosticAudioTap::windowFrames
        || sounding.nonZero != 2 * DiagnosticAudioTap::windowFrames
        || std::abs (std::sqrt (sounding.sumSquares / sounding.samples) - 0.25) > 0.000001
        || buffer.getSample (0, 0) != 0.25f || buffer.getSample (1, 0) != -0.25f)
        return 50;

    buffer.clear();
    tap->armCapture (2);
    tap->applyToBuffer (context);
    DiagnosticAudioTap::Window silent;
    return tap->popWindow (silent) && silent.capture == 2 && silent.nonZero == 0
        && silent.peak == 0.0f && sounding.hash != silent.hash ? 0 : 51;
}

int runSelfTest()
{
    if (const auto octaveTest = runKeyboardOctaveTest(); octaveTest != 0)
        return octaveTest;
    if (const auto emptyProject = runEmptyProjectTest(); emptyProject != 0)
        return emptyProject;
    if (const auto instrumentLog = runInstrumentLogTest(); instrumentLog != 0)
        return instrumentLog;
    if (const auto audioTap = runAudioTapTest(); audioTap != 0)
        return audioTap;
    const auto scalesWork = noteIsInScale (60, 0, 0)
                         && ! noteIsInScale (61, 0, 0)
                         && noteIsInScale (63, 0, 1)
                         && ! noteIsInScale (64, 0, 1);
    if (! scalesWork)
        return 22;
    if (resizedTrackHeight (false, 10) != synthTrackMinimumHeight
        || resizedTrackHeight (true, 10) != drumTrackMinimumHeight
        || resizedTrackHeight (false, 999) != trackMaximumHeight)
        return 24;
    const auto moved = editNoteRange ({ 60, 15.0, 16.0 }, 2.0, false);
    const auto resized = editNoteRange ({ 60, 1.0, 2.0 }, -5.0, true);
    const auto editingWorks = std::abs (moved.startSeconds - 15.0) < 0.0001
                           && std::abs (moved.endSeconds - 16.0) < 0.0001
                           && std::abs (resized.endSeconds - 1.05) < 0.0001;
    if (! editingWorks)
        return 2;
    if (const auto roundTrip = runProjectRoundTripTest(); roundTrip != 0)
        return roundTrip;
    return runPresetAudioTest() ? 0 : 19;
}

class MusicBitMakerApplication final : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override
    {
        return getCommandLineParameters().contains ("--self-test")
            || getCommandLineParameters().contains ("--render-synth-demos");
    }

    void initialise (const juce::String& commandLine) override
    {
        if (commandLine.contains ("--render-synth-demos"))
        {
            setApplicationReturnValue (renderSynthDemos());
            quit();
            return;
        }
        if (commandLine.contains ("--self-test-keyboard-octave"))
        {
            setApplicationReturnValue (runKeyboardOctaveTest());
            quit();
            return;
        }
        if (commandLine.contains ("--self-test-instrument-log"))
        {
            setApplicationReturnValue (runInstrumentLogTest());
            quit();
            return;
        }
        if (commandLine.contains ("--self-test-audio-tap"))
        {
            setApplicationReturnValue (runAudioTapTest());
            quit();
            return;
        }
        if (commandLine.contains ("--self-test-empty-project"))
        {
            setApplicationReturnValue (runEmptyProjectTest());
            quit();
            return;
        }
        if (commandLine.contains ("--self-test"))
        {
            setApplicationReturnValue (runSelfTest());
            quit();
            return;
        }
        engine = std::make_unique<te::Engine> (getApplicationName());
        engine->getPluginManager().createBuiltInType<DiagnosticAudioTap>();
        mainWindow = std::make_unique<MainWindow> (getApplicationName() + " " + getApplicationVersion(), *engine);
    }

    void shutdown() override
    {
        mainWindow.reset();
        engine.reset();
    }

    void systemRequestedQuit() override { quit(); }
    void anotherInstanceStarted (const juce::String&) override
    {
        if (mainWindow != nullptr)
        {
            mainWindow->setMinimised (false);
            mainWindow->setVisible (true);
            mainWindow->toFront (true);
        }
    }

private:
    class MainWindow final : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, te::Engine& engine)
            : DocumentWindow (name, juce::Colour::fromRGB (20, 23, 31), allButtons)
        {
            setUsingNativeTitleBar (true);
            setResizable (true, true);
            setResizeLimits (820, 620, 1500, 1000);
            auto* content = new MainComponent (engine);
            setContentOwned (content, true);
            centreWithSize (1000, 720);
            setVisible (true);
            content->grabKeyboardFocus();
        }

        void closeButtonPressed() override
        {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<te::Engine> engine;
    std::unique_ptr<MainWindow> mainWindow;
};
}

START_JUCE_APPLICATION (MusicBitMakerApplication)
