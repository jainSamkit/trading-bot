
#pragma once
#include <cstddef>
namespace cfg {                                                                                                                                                                                                                                                                                
    inline constexpr size_t MAX_FEED_LEVELS      = 16;
    inline constexpr size_t SNAPSHOT_DEPTH       = 10;
    inline constexpr size_t TRADE_RING_SIZE      = 1024;                                                                                                                                                                                                                                            
    inline constexpr size_t FEED_RING_SIZE       = 1024;
    inline constexpr size_t MAX_INSTRUMENTS      = 64;
    inline constexpr size_t OMS_RING_SIZE        = 64;
}


namespace delta_exchange::testnet {                                                                                                                                                                                                                                                            
    inline constexpr const char* REST_HOST  = "cdn-ind.testnet.deltaex.org";
    inline constexpr const char* WS_HOST    = "socket-ind-pub.testnet.deltaex.org";  

    inline constexpr const char* API_KEY    = "JiV80pv3OJKlUyisMN2x4BHqnKONW5";                                                                                                                                                                                                                
    inline constexpr const char* API_SECRET = "ELUujrDwVJrK5UiJBkQyBKTJNdVvOOy8OKUuEsjxaeeLWXai6emZYsCFtt40";                                                                                                                                                                                  
}                                                                                                                                                                                                                                                                                              
                                                                                                                                                                                                                                                                                               
namespace delta_exchange::prod {                                                                                                                                                                                                                                                               
    inline constexpr const char* REST_HOST  = "api.india.delta.exchange";
    inline constexpr const char* WS_HOST    = "public-socket.india.delta.exchange";
    inline constexpr const char* API_KEY    = "sbr2TG7ui7GxpUN6lzuoYJzcLuIlVp";                                                                                                                                                                                                                
    inline constexpr const char* API_SECRET = "sk2zVY2nNAUTDZwmrzH449KCNRCUmWafVvvrVdraTN5js8wvGYuAMy0rsw8C";                                                                                                                                                                                  
} 