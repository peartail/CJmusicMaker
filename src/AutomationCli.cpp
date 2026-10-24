#include "AutomationCli.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace musicbitmaker
{
namespace
{
namespace te = tracktion::engine;

constexpr int automationSchemaVersion = 1;

class HeadlessEngineBehaviour final : public te::EngineBehaviour
{
public:
    bool autoInitialiseDeviceManager() override { return false; }
    bool addSystemAudioIODeviceTypes() override { return false; }
};

struct ParsedCommand
{
    juce::String command;
    juce::StringPairArray options;
    juce::String error;
};

struct CommandResult
{
    int exitCode = 0;
    juce::var data { new juce::DynamicObject() };
    juce::String errorCode, errorMessage;
};

juce::File resolvePath (const juce::String& path)
{
    if (juce::File::isAbsolutePath (path))
        return juce::File (path);
    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

ParsedCommand parseCommand (const juce::StringArray& arguments)
{
    ParsedCommand parsed;
    const auto headlessIndex = arguments.indexOf ("--headless");
    if (headlessIndex < 0 || headlessIndex + 1 >= arguments.size())
    {
        parsed.error = "Expected --headless followed by a command.";
        return parsed;
    }

    parsed.command = arguments[headlessIndex + 1];
    if (parsed.command.startsWith ("--"))
    {
        parsed.error = "The headless command is missing.";
        return parsed;
    }

    for (int index = headlessIndex + 2; index < arguments.size(); index += 2)
    {
        const auto option = arguments[index];
        if (! option.startsWith ("--") || option.length() <= 2)
        {
            parsed.error = "Unexpected argument: " + option;
            return parsed;
        }
        if (index + 1 >= arguments.size())
        {
            parsed.error = "Missing value for " + option + ".";
            return parsed;
        }

        const auto name = option.substring (2);
        if (parsed.options.containsKey (name))
        {
            parsed.error = "Duplicate option: " + option;
            return parsed;
        }
        parsed.options.set (name, arguments[index + 1]);
    }
    return parsed;
}

bool optionsAreAllowed (const ParsedCommand& parsed, const juce::StringArray& allowed,
                        juce::String& error)
{
    for (const auto& key : parsed.options.getAllKeys())
        if (! allowed.contains (key))
        {
            error = "Unknown option for " + parsed.command + ": --" + key;
            return false;
        }
    return true;
}

CommandResult failure (int exitCode, juce::String code, juce::String message)
{
    CommandResult result;
    result.exitCode = exitCode;
    result.errorCode = std::move (code);
    result.errorMessage = std::move (message);
    return result;
}

juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

juce::String instrumentCategory (const te::AudioTrack& track)
{
    if (const auto* synth = track.pluginList.findFirstPluginOfType<te::FourOscPlugin>())
    {
        const auto drum = static_cast<int> (synth->state.getProperty ("musicBitMakerDrumIndex", -1));
        constexpr std::array<const char*, 4> drums { "kick", "snare", "closed-hat", "open-hat" };
        if (juce::isPositiveAndBelow (drum, static_cast<int> (drums.size())))
            return drums[static_cast<size_t> (drum)];
        return "synth";
    }
    return "none";
}

te::AudioTrack* findTrack (te::Edit& edit, const juce::String& itemID)
{
    for (auto* track : te::getAudioTracks (edit))
        if (track->itemID.toString() == itemID)
            return track;
    return nullptr;
}

CommandResult runProjectInfo (te::Edit& edit, const juce::File& projectFile)
{
    auto data = makeObject();
    auto* object = data.getDynamicObject();
    const auto loop = edit.getTransport().getLoopRange();
    object->setProperty ("project", projectFile.getFullPathName());
    object->setProperty ("bpm", edit.tempoSequence.getTempo (0)->getBpm());
    object->setProperty ("lengthSeconds", edit.getLength().inSeconds());
    object->setProperty ("trackCount", te::getAudioTracks (edit).size());
    object->setProperty ("loopEnabled", static_cast<bool> (edit.getTransport().looping));
    object->setProperty ("loopStartSeconds", loop.getStart().inSeconds());
    object->setProperty ("loopEndSeconds", loop.getEnd().inSeconds());
    return { 0, data };
}

CommandResult runListTracks (te::Edit& edit, const juce::File& projectFile)
{
    juce::Array<juce::var> tracksJson;
    const auto tracks = te::getAudioTracks (edit);
    for (int index = 0; index < tracks.size(); ++index)
    {
        auto trackJson = makeObject();
        auto* object = trackJson.getDynamicObject();
        object->setProperty ("id", tracks[index]->itemID.toString());
        object->setProperty ("index", index);
        object->setProperty ("name", tracks[index]->getName());
        object->setProperty ("instrument", instrumentCategory (*tracks[index]));
        object->setProperty ("clipCount", tracks[index]->getClips().size());
        tracksJson.add (trackJson);
    }

    auto data = makeObject();
    data.getDynamicObject()->setProperty ("project", projectFile.getFullPathName());
    data.getDynamicObject()->setProperty ("tracks", tracksJson);
    return { 0, data };
}

struct NoteDescription
{
    int pitch = 0, velocity = 0, clipIndex = 0;
    double startSeconds = 0.0, durationSeconds = 0.0;
};

CommandResult runListNotes (te::Edit& edit, const juce::File& projectFile,
                            const juce::String& trackID)
{
    auto* track = findTrack (edit, trackID);
    if (track == nullptr)
        return failure (5, "track_not_found", "No track has item ID " + trackID + ".");

    std::vector<NoteDescription> notes;
    int clipIndex = 0;
    for (auto* clip : track->getClips())
    {
        if (auto* midi = dynamic_cast<te::MidiClip*> (clip))
            for (auto* note : midi->getSequence().getNotes())
            {
                const auto start = note->getEditStartTime (*midi).inSeconds();
                const auto end = note->getEditEndTime (*midi).inSeconds();
                notes.push_back ({ note->getNoteNumber(), note->getVelocity(), clipIndex,
                                   start, end - start });
            }
        ++clipIndex;
    }
    std::sort (notes.begin(), notes.end(), [] (const auto& left, const auto& right)
    {
        if (left.startSeconds != right.startSeconds)
            return left.startSeconds < right.startSeconds;
        if (left.pitch != right.pitch)
            return left.pitch < right.pitch;
        return left.durationSeconds < right.durationSeconds;
    });

    juce::Array<juce::var> notesJson;
    for (size_t index = 0; index < notes.size(); ++index)
    {
        const auto& note = notes[index];
        auto noteJson = makeObject();
        auto* object = noteJson.getDynamicObject();
        object->setProperty ("index", static_cast<int> (index));
        object->setProperty ("clipIndex", note.clipIndex);
        object->setProperty ("pitch", note.pitch);
        object->setProperty ("name", juce::MidiMessage::getMidiNoteName (note.pitch, true, true, 4));
        object->setProperty ("startSeconds", note.startSeconds);
        object->setProperty ("durationSeconds", note.durationSeconds);
        object->setProperty ("velocity", note.velocity);
        notesJson.add (noteJson);
    }

    auto data = makeObject();
    auto* object = data.getDynamicObject();
    object->setProperty ("project", projectFile.getFullPathName());
    object->setProperty ("trackId", trackID);
    object->setProperty ("notes", notesJson);
    return { 0, data };
}

CommandResult executeCommand (te::Engine& engine, const ParsedCommand& parsed)
{
    juce::String optionError;
    if (parsed.command == "version")
    {
        if (! optionsAreAllowed (parsed, { "result-json" }, optionError))
            return failure (2, "invalid_argument", optionError);
        auto data = makeObject();
        data.getDynamicObject()->setProperty ("automationSchemaVersion", automationSchemaVersion);
        data.getDynamicObject()->setProperty ("tracktionVersion", te::Engine::getVersion());
        return { 0, data };
    }

    if (parsed.command != "project-info" && parsed.command != "list-tracks"
        && parsed.command != "list-notes")
        return failure (2, "unknown_command", "Unknown headless command: " + parsed.command + ".");

    auto allowed = juce::StringArray { "project", "result-json" };
    if (parsed.command == "list-notes")
        allowed.add ("track-id");
    if (! optionsAreAllowed (parsed, allowed, optionError))
        return failure (2, "invalid_argument", optionError);
    if (! parsed.options.containsKey ("project") || parsed.options["project"].isEmpty())
        return failure (2, "missing_project", "--project is required for " + parsed.command + ".");
    if (parsed.command == "list-notes"
        && (! parsed.options.containsKey ("track-id") || parsed.options["track-id"].isEmpty()))
        return failure (2, "missing_track_id", "--track-id is required for list-notes.");
    const auto projectFile = resolvePath (parsed.options["project"]);
    if (! projectFile.existsAsFile())
        return failure (3, "project_not_found", "Project file does not exist: "
                        + projectFile.getFullPathName());

    auto edit = te::loadEditFromFile (engine, projectFile, te::Edit::forEditing);
    if (edit == nullptr)
        return failure (4, "project_load_failed", "Could not load project: "
                        + projectFile.getFullPathName());
    if (parsed.command == "project-info")
        return runProjectInfo (*edit, projectFile);
    if (parsed.command == "list-tracks")
        return runListTracks (*edit, projectFile);
    return runListNotes (*edit, projectFile, parsed.options["track-id"]);
}

juce::var resultEnvelope (const ParsedCommand& parsed, const CommandResult& result)
{
    auto envelope = makeObject();
    auto* object = envelope.getDynamicObject();
    object->setProperty ("schemaVersion", automationSchemaVersion);
    object->setProperty ("ok", result.exitCode == 0);
    object->setProperty ("command", parsed.command);
    object->setProperty ("appVersion", JUCE_APPLICATION_VERSION_STRING);
    if (result.exitCode == 0)
    {
        object->setProperty ("data", result.data);
        object->setProperty ("warnings", juce::Array<juce::var>());
    }
    else
    {
        auto error = makeObject();
        error.getDynamicObject()->setProperty ("code", result.errorCode);
        error.getDynamicObject()->setProperty ("message", result.errorMessage);
        object->setProperty ("error", error);
    }
    return envelope;
}

bool writeJson (const juce::File& file, const juce::var& value)
{
    if (! file.getParentDirectory().isDirectory())
        return false;
    return file.replaceWithText (juce::JSON::toString (value, true) + juce::newLine);
}

juce::var readJson (const juce::File& file)
{
    juce::var parsed;
    return juce::JSON::parse (file.loadFileAsString(), parsed).wasOk() ? parsed : juce::var();
}
}

std::unique_ptr<te::EngineBehaviour> createHeadlessEngineBehaviour()
{
    return std::make_unique<HeadlessEngineBehaviour>();
}

int runHeadlessAutomation (te::Engine& engine, const juce::StringArray& arguments)
{
    auto parsed = parseCommand (arguments);
    juce::String resultPath;
    for (int index = 0; index + 1 < arguments.size(); ++index)
        if (arguments[index] == "--result-json")
            resultPath = arguments[index + 1];

    if (resultPath.isEmpty())
        return 2;

    auto result = parsed.error.isNotEmpty()
        ? failure (2, "invalid_argument", parsed.error)
        : executeCommand (engine, parsed);
    if (! writeJson (resolvePath (resultPath), resultEnvelope (parsed, result)))
        return 3;
    return result.exitCode;
}

int runAutomationCliSelfTest()
{
    te::Engine engine { "MusicBitMakerAutomationCliTest", nullptr, createHeadlessEngineBehaviour() };
    if (engine.getDeviceManager().deviceManager.getCurrentAudioDevice() != nullptr)
        return 81;

    const auto root = juce::File::getCurrentWorkingDirectory();
    const auto projectFile = root.getNonexistentChildFile ("MusicBitMakerAutomationTest", ".tracktionedit", false);
    const auto infoFile = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + "-info.json");
    const auto tracksFile = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + "-tracks.json");
    const auto notesFile = projectFile.getSiblingFile (projectFile.getFileNameWithoutExtension() + "-notes.json");

    auto edit = te::createEmptyEdit (engine, projectFile);
    edit->ensureNumberOfAudioTracks (1);
    auto* track = te::getAudioTracks (*edit)[0];
    track->setName ("CLI Lead");
    auto* clip = dynamic_cast<te::MidiClip*> (track->insertClipWithState (
        juce::ValueTree (te::IDs::MIDICLIP), "CLI notes", te::TrackItem::Type::midi,
        { tracktion::TimeRange (tracktion::TimePosition::fromSeconds (0.0),
                                tracktion::TimeDuration::fromSeconds (4.0)),
          tracktion::TimeDuration::fromSeconds (0.0) }, false, false));
    if (clip == nullptr)
        return 82;
    edit->tempoSequence.getTempo (0)->setBpm (96.0);
    clip->getSequence().addNote (60, tracktion::BeatPosition::fromBeats (1.0),
                                 tracktion::BeatDuration::fromBeats (2.0), 87, 0, nullptr);
    if (! te::EditFileOperations (*edit).save (false, true, false))
        return 83;
    const auto trackID = track->itemID.toString();
    edit.reset();

    const auto base = juce::StringArray { "--headless" };
    auto infoArgs = base;
    infoArgs.addArray ({ "project-info", "--project", projectFile.getFullPathName(),
                         "--result-json", infoFile.getFullPathName() });
    auto tracksArgs = base;
    tracksArgs.addArray ({ "list-tracks", "--project", projectFile.getFullPathName(),
                           "--result-json", tracksFile.getFullPathName() });
    auto notesArgs = base;
    notesArgs.addArray ({ "list-notes", "--project", projectFile.getFullPathName(),
                          "--track-id", trackID, "--result-json", notesFile.getFullPathName() });

    const auto commandsWorked = runHeadlessAutomation (engine, infoArgs) == 0
                             && runHeadlessAutomation (engine, tracksArgs) == 0
                             && runHeadlessAutomation (engine, notesArgs) == 0;
    const auto info = readJson (infoFile);
    const auto tracks = readJson (tracksFile);
    const auto notes = readJson (notesFile);
    const auto* infoObject = info.getDynamicObject();
    const auto* tracksObject = tracks.getDynamicObject();
    const auto* notesObject = notes.getDynamicObject();
    const auto infoOk = infoObject != nullptr && static_cast<bool> (infoObject->getProperty ("ok"));
    const auto tracksData = tracksObject != nullptr ? tracksObject->getProperty ("data") : juce::var();
    const auto notesData = notesObject != nullptr ? notesObject->getProperty ("data") : juce::var();
    const auto tracksArray = tracksData.getDynamicObject() != nullptr
        ? tracksData.getDynamicObject()->getProperty ("tracks") : juce::var();
    const auto notesArray = notesData.getDynamicObject() != nullptr
        ? notesData.getDynamicObject()->getProperty ("notes") : juce::var();
    const auto resultOk = commandsWorked && infoOk
                       && tracksArray.isArray() && tracksArray.getArray()->size() == 1
                       && notesArray.isArray() && notesArray.getArray()->size() == 1;

    projectFile.deleteFile();
    infoFile.deleteFile();
    tracksFile.deleteFile();
    notesFile.deleteFile();
    return resultOk ? 0 : 84;
}
}
