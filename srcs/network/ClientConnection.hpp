#include "../../include/Webserv.hpp"
#include <map>

enum ConnState { READING_HEADERS, READING_BODY, READY };
enum BodyType { BODY_NONE, BODY_FIXED, BODY_CHUNKED };
enum ChunkState { CHUNK_READ_SIZE, CHUNK_READ_DATA, CHUNK_READ_CRLF, CHUNK_COMPLETE };

class ClientConnection {
public:
    int fd;
    int listenFd;             // parent listening socket fd
    std::string buffer;       // raw incoming buffer
    time_t lastActivity;      // last activity timestamp
    bool isReading;           // connection state flag (unused for now)

    // Parsed request state
    ConnState state;
    bool headersParsed;
    std::string method;
    std::string uri;
    std::string version;
    std::map<std::string, std::string> headers;

    BodyType bodyType;
    size_t contentLength;
    size_t bodyReceived;
    std::string body;

    // Chunked decoding state
    std::string chunkBuffer;  // staging buffer for chunked stream
    ChunkState chunkState;
    size_t currentChunkSize;

    // Outgoing write buffering
    std::string outBuffer;    // full HTTP response to send
    size_t outOffset;         // bytes already sent
    bool hasResponse;         // whether a response is ready to write

    ClientConnection()
        : fd(-1), listenFd(-1), lastActivity(0), isReading(true), state(READING_HEADERS), headersParsed(false),
          bodyType(BODY_NONE), contentLength(0), bodyReceived(0), chunkState(CHUNK_READ_SIZE),
          currentChunkSize(0), outOffset(0), hasResponse(false) {}
};
