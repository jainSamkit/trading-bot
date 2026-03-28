enum class SessionStatus : uint8_t {
    CONNECTED,
    DISCONNECTED,
    RECONNECTING
};

enum class SessionID : uint16_t {
    L2Update,
    Mark,
    Ticker
};

enum class Kind: uint8_t {
    Socket, 
    Timer,
    Eventfd
};

/** Per-session connection state; fd/ssl owned here, not on WebSocketClient. */
struct SessionCtx {
    SSL* ssl_ = nullptr;
    int fd_ = -1;
    SessionID id = SessionID::L2Update;
    int tfd_ = -1;
    SessionStatus status = SessionStatus::DISCONNECTED;

};

struct EpollSlot {
    SessionCtx* ctx = nullptr;
    Kind        kind = Kind::Socket;
    int         shutdownEfd = -1;
    void*       session_opaque = nullptr;
    void (*socket_ready)(void* client, void* base, EpollSlot* slot) = nullptr;
};