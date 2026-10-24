#include "LiveControl.h"

#include <atomic>
#include <process.h>
#include <thread>

namespace musicbitmaker
{
namespace
{
constexpr int liveSchemaVersion = 1;
constexpr size_t maximumMessageBytes = 64 * 1024;
constexpr std::uint32_t liveMessageMagic = 0x4d424d31;

juce::var makeObject()
{
    return juce::var (new juce::DynamicObject());
}

juce::File liveSessionFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("MusicBitMaker").getChildFile ("live-session.json");
}

juce::File resolvePath (const juce::String& path)
{
    if (juce::File::isAbsolutePath (path))
        return juce::File (path);
    return juce::File::getCurrentWorkingDirectory().getChildFile (path);
}

juce::MemoryBlock jsonMessage (const juce::var& value)
{
    const auto text = juce::JSON::toString (value, false);
    return juce::MemoryBlock (text.toRawUTF8(), static_cast<size_t> (text.getNumBytesAsUTF8()));
}

juce::var parseJsonMessage (const juce::MemoryBlock& message)
{
    if (message.getSize() == 0 || message.getSize() > maximumMessageBytes)
        return {};
    juce::var parsed;
    const auto text = juce::String::fromUTF8 (static_cast<const char*> (message.getData()),
                                               static_cast<int> (message.getSize()));
    return juce::JSON::parse (text, parsed).wasOk() ? parsed : juce::var();
}

bool writeJson (const juce::File& file, const juce::var& value)
{
    if (! file.getParentDirectory().isDirectory())
        return false;
    return file.replaceWithText (juce::JSON::toString (value, true) + juce::newLine);
}

juce::var localErrorEnvelope (const juce::String& command, const juce::String& code,
                              const juce::String& message)
{
    auto envelope = makeObject();
    auto* object = envelope.getDynamicObject();
    object->setProperty ("schemaVersion", liveSchemaVersion);
    object->setProperty ("ok", false);
    object->setProperty ("command", command);
    object->setProperty ("appVersion", JUCE_APPLICATION_VERSION_STRING);
    auto error = makeObject();
    error.getDynamicObject()->setProperty ("code", code);
    error.getDynamicObject()->setProperty ("message", message);
    object->setProperty ("error", error);
    return envelope;
}

class ServerConnection final : public juce::InterprocessConnection
{
public:
    ServerConnection (LiveCommandHandler handlerToUse, juce::String tokenToUse)
        : InterprocessConnection (true, liveMessageMagic),
          handler (std::move (handlerToUse)), token (std::move (tokenToUse))
    {
    }

    ~ServerConnection() override
    {
        stopping = true;
        disconnect (2000, Notify::no);
    }

    bool listen (const juce::String& name)
    {
        pipeName = name;
        return createPipe (pipeName, 5000, true);
    }

    void stop()
    {
        stopping = true;
        disconnect (2000, Notify::no);
    }

    void connectionMade() override {}

    void connectionLost() override
    {
        if (! stopping)
        {
            disconnect (2000, Notify::no);
            createPipe (pipeName, 5000, true);
        }
    }

    void messageReceived (const juce::MemoryBlock& message) override
    {
        auto request = parseJsonMessage (message);
        auto* requestObject = request.getDynamicObject();
        const auto requestID = requestObject != nullptr
            ? requestObject->getProperty ("requestId").toString() : juce::String();
        const auto command = requestObject != nullptr
            ? requestObject->getProperty ("command").toString() : juce::String();

        LiveCommandResult result;
        if (requestObject == nullptr)
            result = liveCommandFailure ("invalid_json", "The live request is not valid JSON.");
        else if (static_cast<int> (requestObject->getProperty ("schemaVersion")) != liveSchemaVersion)
            result = liveCommandFailure ("protocol_mismatch", "Unsupported live protocol version.");
        else if (requestID.isEmpty())
            result = liveCommandFailure ("missing_request_id", "requestId is required.");
        else if (requestObject->getProperty ("token").toString() != token)
            result = liveCommandFailure ("invalid_token", "The live session token is invalid.");
        else if (command.isEmpty())
            result = liveCommandFailure ("missing_command", "command is required.");
        else
            result = handler (command, requestObject->getProperty ("arguments"));

        auto response = makeObject();
        auto* responseObject = response.getDynamicObject();
        responseObject->setProperty ("schemaVersion", liveSchemaVersion);
        responseObject->setProperty ("requestId", requestID);
        responseObject->setProperty ("ok", result.ok);
        responseObject->setProperty ("command", command);
        responseObject->setProperty ("appVersion", JUCE_APPLICATION_VERSION_STRING);
        if (result.ok)
        {
            responseObject->setProperty ("data", result.data);
        }
        else
        {
            auto error = makeObject();
            error.getDynamicObject()->setProperty ("code", result.errorCode);
            error.getDynamicObject()->setProperty ("message", result.errorMessage);
            responseObject->setProperty ("error", error);
        }
        sendMessage (jsonMessage (response));
    }

private:
    LiveCommandHandler handler;
    juce::String token, pipeName;
    bool stopping = false;
};

class ClientConnection final : public juce::InterprocessConnection
{
public:
    ClientConnection() : InterprocessConnection (false, liveMessageMagic) {}
    ~ClientConnection() override { disconnect (2000, Notify::no); }

    void connectionMade() override {}
    void connectionLost() override { responseReady.signal(); }
    void messageReceived (const juce::MemoryBlock& message) override
    {
        const juce::ScopedLock lock (responseLock);
        response = parseJsonMessage (message);
        responseReady.signal();
    }

    juce::var waitForResponse (int timeoutMilliseconds)
    {
        if (! responseReady.wait (timeoutMilliseconds))
            return {};
        const juce::ScopedLock lock (responseLock);
        return response;
    }

private:
    juce::CriticalSection responseLock;
    juce::WaitableEvent responseReady;
    juce::var response;
};

struct ClientCommand
{
    juce::String command, resultPath, trackID, outputPath, error;
    int timeoutMilliseconds = 5000;
};

ClientCommand parseClientCommand (const juce::StringArray& arguments)
{
    ClientCommand parsed;
    const auto liveIndex = arguments.indexOf ("--live");
    if (liveIndex < 0 || liveIndex + 1 >= arguments.size())
    {
        parsed.error = "Expected --live followed by a command.";
        return parsed;
    }
    parsed.command = arguments[liveIndex + 1];
    if (parsed.command.startsWith ("--"))
    {
        parsed.error = "The live command is missing.";
        return parsed;
    }

    juce::StringPairArray options;
    for (int index = liveIndex + 2; index < arguments.size(); index += 2)
    {
        const auto option = arguments[index];
        if (! option.startsWith ("--") || option.length() <= 2 || index + 1 >= arguments.size())
        {
            parsed.error = "Invalid live command option: " + option;
            return parsed;
        }
        const auto name = option.substring (2);
        if (options.containsKey (name))
        {
            parsed.error = "Duplicate option: " + option;
            return parsed;
        }
        options.set (name, arguments[index + 1]);
    }

    auto allowed = juce::StringArray { "result-json", "timeout-ms" };
    if (parsed.command == "list-notes")
        allowed.add ("track-id");
    if (parsed.command == "capture-ui")
        allowed.add ("output");
    for (const auto& key : options.getAllKeys())
        if (! allowed.contains (key))
        {
            parsed.error = "Unknown option for " + parsed.command + ": --" + key;
            return parsed;
        }

    parsed.resultPath = options["result-json"];
    parsed.trackID = options["track-id"];
    parsed.outputPath = options["output"];
    if (parsed.resultPath.isEmpty())
        parsed.error = "--result-json is required for live commands.";
    else if (options.containsKey ("timeout-ms"))
    {
        const auto timeout = options["timeout-ms"];
        if (! timeout.containsOnly ("0123456789"))
            parsed.error = "--timeout-ms must be an integer.";
        else
            parsed.timeoutMilliseconds = juce::jlimit (250, 30000, timeout.getIntValue());
    }
    if (parsed.command == "list-notes" && parsed.trackID.isEmpty())
        parsed.error = "--track-id is required for list-notes.";
    if (parsed.command == "capture-ui" && parsed.outputPath.isEmpty())
        parsed.error = "--output is required for capture-ui.";
    return parsed;
}

int exitCodeForResponse (const juce::var& response)
{
    auto* object = response.getDynamicObject();
    if (object == nullptr)
        return 9;
    if (static_cast<bool> (object->getProperty ("ok")))
        return 0;
    const auto error = object->getProperty ("error");
    const auto code = error.getDynamicObject() != nullptr
        ? error.getDynamicObject()->getProperty ("code").toString() : juce::String();
    if (code == "track_not_found")
        return 5;
    if (code == "capture_failed")
        return 3;
    if (code == "protocol_mismatch" || code == "invalid_token")
        return 9;
    return 2;
}

int runClientWithSessionFile (const juce::StringArray& arguments, const juce::File& sessionFile)
{
    const auto parsed = parseClientCommand (arguments);
    if (parsed.resultPath.isEmpty())
        return 2;
    const auto resultFile = resolvePath (parsed.resultPath);
    const auto outputFile = parsed.outputPath.isNotEmpty()
        ? resolvePath (parsed.outputPath) : juce::File();
    if (parsed.error.isNotEmpty())
        return writeJson (resultFile, localErrorEnvelope (parsed.command, "invalid_argument", parsed.error)) ? 2 : 3;
    if (outputFile != juce::File() && outputFile == resultFile)
        return writeJson (resultFile, localErrorEnvelope (parsed.command, "invalid_argument",
                          "--output and --result-json must use different files.")) ? 2 : 3;

    juce::var session;
    if (! sessionFile.existsAsFile()
        || juce::JSON::parse (sessionFile.loadFileAsString(), session).failed())
        return writeJson (resultFile, localErrorEnvelope (parsed.command, "app_not_running",
                          "No running MusicBitMaker live session was found.")) ? 7 : 3;
    auto* sessionObject = session.getDynamicObject();
    if (sessionObject == nullptr
        || static_cast<int> (sessionObject->getProperty ("schemaVersion")) != liveSchemaVersion)
        return writeJson (resultFile, localErrorEnvelope (parsed.command, "protocol_mismatch",
                          "The live session descriptor is incompatible.")) ? 9 : 3;

    const auto pipeName = sessionObject->getProperty ("pipeName").toString();
    const auto token = sessionObject->getProperty ("token").toString();
    if (pipeName.isEmpty() || token.isEmpty())
        return writeJson (resultFile, localErrorEnvelope (parsed.command, "protocol_mismatch",
                          "The live session descriptor is incomplete.")) ? 9 : 3;

    ClientConnection connection;
    const auto connectionTimeout = juce::jmin (2000, parsed.timeoutMilliseconds);
    if (! connection.connectToPipe (pipeName, connectionTimeout))
        return writeJson (resultFile, localErrorEnvelope (parsed.command, "app_not_running",
                          "The MusicBitMaker live pipe is not available.")) ? 7 : 3;

    const auto requestID = juce::Uuid().toString();
    auto argumentsObject = makeObject();
    if (parsed.trackID.isNotEmpty())
        argumentsObject.getDynamicObject()->setProperty ("trackId", parsed.trackID);
    if (outputFile != juce::File())
        argumentsObject.getDynamicObject()->setProperty ("output", outputFile.getFullPathName());
    auto request = makeObject();
    auto* requestObject = request.getDynamicObject();
    requestObject->setProperty ("schemaVersion", liveSchemaVersion);
    requestObject->setProperty ("requestId", requestID);
    requestObject->setProperty ("token", token);
    requestObject->setProperty ("command", parsed.command);
    requestObject->setProperty ("arguments", argumentsObject);
    const auto requestMessage = jsonMessage (request);
    if (requestMessage.getSize() > maximumMessageBytes || ! connection.sendMessage (requestMessage))
        return writeJson (resultFile, localErrorEnvelope (parsed.command, "connection_failed",
                          "Could not send the live request.")) ? 8 : 3;

    auto response = connection.waitForResponse (parsed.timeoutMilliseconds);
    auto* responseObject = response.getDynamicObject();
    if (responseObject == nullptr)
        response = localErrorEnvelope (parsed.command, "timeout", "The live command timed out.");
    else if (static_cast<int> (responseObject->getProperty ("schemaVersion")) != liveSchemaVersion
             || responseObject->getProperty ("requestId").toString() != requestID)
        response = localErrorEnvelope (parsed.command, "protocol_mismatch",
                                       "The live response did not match the request.");

    if (! writeJson (resultFile, response))
        return 3;
    return responseObject == nullptr ? 8 : exitCodeForResponse (response);
}
}

struct LiveControlServer::Impl
{
    explicit Impl (LiveCommandHandler handlerToUse, juce::File sessionFileToUse = liveSessionFile())
        : handler (std::move (handlerToUse)), sessionFile (std::move (sessionFileToUse))
    {
    }

    bool start()
    {
        stop();
        token = juce::Uuid().toString();
        pipeName = "MusicBitMaker.Live." + juce::String (_getpid()) + "." + juce::Uuid().toString();
        connection = std::make_unique<ServerConnection> (handler, token);
        if (! connection->listen (pipeName))
        {
            connection.reset();
            return false;
        }
        if (! sessionFile.getParentDirectory().createDirectory())
        {
            connection.reset();
            return false;
        }

        auto session = makeObject();
        auto* object = session.getDynamicObject();
        object->setProperty ("schemaVersion", liveSchemaVersion);
        object->setProperty ("appVersion", JUCE_APPLICATION_VERSION_STRING);
        object->setProperty ("pid", _getpid());
        object->setProperty ("pipeName", pipeName);
        object->setProperty ("token", token);
        object->setProperty ("startedAt", juce::Time::getCurrentTime().toISO8601 (true));
        if (! writeJson (sessionFile, session))
        {
            connection.reset();
            return false;
        }
        return true;
    }

    void stop()
    {
        if (connection != nullptr)
            connection->stop();
        connection.reset();

        juce::var session;
        if (sessionFile.existsAsFile()
            && juce::JSON::parse (sessionFile.loadFileAsString(), session).wasOk()
            && session.getDynamicObject() != nullptr
            && session.getDynamicObject()->getProperty ("token").toString() == token)
            sessionFile.deleteFile();
        token.clear();
        pipeName.clear();
    }

    LiveCommandHandler handler;
    juce::File sessionFile;
    juce::String token, pipeName;
    std::unique_ptr<ServerConnection> connection;
};

LiveCommandResult liveCommandSuccess (juce::var data)
{
    return { true, std::move (data), {}, {} };
}

LiveCommandResult liveCommandFailure (juce::String code, juce::String message)
{
    return { false, makeObject(), std::move (code), std::move (message) };
}

LiveControlServer::LiveControlServer (LiveCommandHandler handler)
    : impl (std::make_unique<Impl> (std::move (handler)))
{
}

LiveControlServer::LiveControlServer (LiveCommandHandler handler, juce::File sessionFile)
    : impl (std::make_unique<Impl> (std::move (handler), std::move (sessionFile)))
{
}

LiveControlServer::~LiveControlServer()
{
    stop();
}

bool LiveControlServer::start()
{
    return impl->start();
}

void LiveControlServer::stop()
{
    impl->stop();
}

int runLiveControlClient (const juce::StringArray& arguments)
{
    return runClientWithSessionFile (arguments, liveSessionFile());
}

int runLiveControlSelfTest()
{
    const auto root = juce::File::getCurrentWorkingDirectory();
    const auto sessionFile = root.getNonexistentChildFile ("MusicBitMakerLiveTest", ".json", false);
    const auto resultFile = sessionFile.getSiblingFile (
        sessionFile.getFileNameWithoutExtension() + "-result.json");
    const auto captureFile = sessionFile.getSiblingFile (
        sessionFile.getFileNameWithoutExtension() + "-capture.png");
    std::atomic<bool> handledOnMessageThread { false };
    std::atomic<bool> receivedCapturePath { false };
    LiveControlServer server (
        [&handledOnMessageThread, &receivedCapturePath, &captureFile]
        (const juce::String& command, const juce::var& arguments)
        {
            handledOnMessageThread = juce::MessageManager::getInstance()->isThisTheMessageThread();
            if (command == "capture-ui" && arguments.getDynamicObject() != nullptr)
                receivedCapturePath = arguments.getDynamicObject()->getProperty ("output").toString()
                    == captureFile.getFullPathName();
            auto data = makeObject();
            data.getDynamicObject()->setProperty ("reply", command);
            return liveCommandSuccess (data);
        }, sessionFile);
    if (! server.start())
        return 91;

    const juce::StringArray arguments {
        "--live", "ping", "--result-json", resultFile.getFullPathName(), "--timeout-ms", "3000"
    };
    const auto runRequest = [&] (const juce::StringArray& requestArguments)
    {
        std::atomic<bool> finished { false };
        int result = -1;
        std::thread client ([&]
        {
            result = runClientWithSessionFile (requestArguments, sessionFile);
            finished = true;
        });
        while (! finished)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (20);
        client.join();
        juce::MessageManager::getInstance()->runDispatchLoopUntil (50);
        return result;
    };

    const auto firstResult = runRequest (arguments);
    juce::var session;
    const auto sessionParsed = juce::JSON::parse (sessionFile.loadFileAsString(), session).wasOk()
                            && session.getDynamicObject() != nullptr;
    const auto validToken = sessionParsed
        ? session.getDynamicObject()->getProperty ("token").toString() : juce::String();
    if (sessionParsed)
    {
        session.getDynamicObject()->setProperty ("token", "invalid-test-token");
        writeJson (sessionFile, session);
    }
    const auto invalidTokenResult = sessionParsed ? runRequest (arguments) : -1;
    if (sessionParsed)
    {
        session.getDynamicObject()->setProperty ("token", validToken);
        writeJson (sessionFile, session);
    }
    const auto secondResult = sessionParsed ? runRequest (arguments) : -1;
    const juce::StringArray captureArguments {
        "--live", "capture-ui", "--output", captureFile.getFullPathName(),
        "--result-json", resultFile.getFullPathName(), "--timeout-ms", "3000"
    };
    const auto captureResult = sessionParsed ? runRequest (captureArguments) : -1;
    server.stop();

    juce::var response;
    const auto parsed = resultFile.existsAsFile()
        && juce::JSON::parse (resultFile.loadFileAsString(), response).wasOk();
    const auto* responseObject = response.getDynamicObject();
    const auto ok = firstResult == 0 && invalidTokenResult == 9 && secondResult == 0
                 && captureResult == 0 && receivedCapturePath
                 && parsed && responseObject != nullptr
                 && static_cast<bool> (responseObject->getProperty ("ok"))
                 && handledOnMessageThread;
    sessionFile.deleteFile();
    resultFile.deleteFile();
    captureFile.deleteFile();
    return ok ? 0 : 92;
}
}
