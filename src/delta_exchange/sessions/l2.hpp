

#pragma once
class L2UpdateSession : public Session<L2UpdateSession, DeltaWebsocketClient> {
    public:
        struct L2Level {
            double price;
            double size;
        }
        
        struct L2Update {
            L2Level asks[32];
            L2Level bids[32];
            uint64_t sequence_no;
            uint64_t timestamp;
            uint32_t checksum;
            uint32_t product_id;
            bool isSnapshot;
        }

        explicit L2UpdateSession(DeltaWebsocketClient& client, SessionID sessionID)
            : Session<L2UpdateSession, DeltaWebsocketClient>(client, sessionID) {}
    
        void onMessage(std::string_view msg) {

            client_.parsser.
            if (msg.find(R"("type":"heartbeat")")!= std::string_view::npos) {
                std::cout<<"Got heartbeat"<<std::endl;
    
                arm_timer_ms(DeltaWebsocketClient::HEARTBEAT_TIMEOUT_MS);
                return;
            }
            std::cout << msg << "\n\n\n";
        }
    
        std::string subscriptionMsg() {
            std::string msg = "";
            size_t i = 0;
    
            for(;;) {
                msg = msg + R"(")" + symbols[i] + R"(")";
                if(i==symbols.size()-1) break;
                msg += R"(,)";
                i+=1;
            }
    
            return msg;
        } 
    
        void onSubscribe() {
            std::string msg =
            R"({"type":"subscribe","payload":{"channels":[{"name":")"
            + channel
            + R"(","symbols":[)"
            + subscriptionMsg()
            + R"(]}]}})";
    
            client_.ws_send(ctx_.ssl_, msg);
            client_.ws_send(ctx_.ssl_, R"({"type":"enable_heartbeat"})");
    
            std::cout<<"Heartbeat enabled"<<"\n\n";
            arm_timer_ms(DeltaWebsocketClient::HEARTBEAT_TIMEOUT_MS);
            std::cout<<"Timer armed"<<"\n\n";
        }
        
        private:
            std::string channel{"l2_updates"};
            std::vector<std::string>symbols{"ADAUSD", "SOLUSD"};
    };