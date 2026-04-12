
#pragma once
class TickerSession : public Session<TickerSession, DeltaWebsocketClient> {
    public:
        static constexpr SessionType session_type = SessionType::Public;
        explicit TickerSession(DeltaWebsocketClient& client, SessionID sessionID)
            : Session<TickerSession, DeltaWebsocketClient>(client, sessionID) {}
    
        void onMessage(std::string_view) {}
        void onSubscribe() {}
        void onAuth() {}
    };
    