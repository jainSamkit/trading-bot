class MarkSession : public Session<MarkSession, DeltaWebsocketClient> {
    public:
        explicit MarkSession(DeltaWebsocketClient& client, SessionID sessionID)
            : Session<MarkSession, DeltaWebsocketClient>(client, sessionID) {}
    
        void onMessage(std::string_view) {}
        void onSubscribe() {}
        void onAuth() {}
    };