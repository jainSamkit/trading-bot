#include "latency/clock.hpp"                                         
  #include "latency/histogram.hpp"                                                                                                                                                                                                                                                               
  #include "latency/registry.hpp"                                                                                                                                                                                                                                                                
  #include <cstdio>                                                                                                                                                                                                                                                                              
                                                                                                                                                                                                                                                                                                 
  int main() {                                                                                                                                                                                                                                                                                   
      latency::calibrate();                                                                                                                                                                                                                                                                      
      latency::Registry::init(latency::TagSet::Venue::Delta);                   
                                                                                                                                                                                                                                                                                                 
      auto* h = latency::Registry::get_or_create({                     
          .event_type = latency::TagSet::EventType::JsonParse,        
          .msg_type   = latency::TagSet::MsgType::L2
      });                                                                                                                                                                                                                                                                                        
                                                                      
      // Same tags → same histogram                                                                                                                                                                                                                                                              
      auto* h2 = latency::Registry::get_or_create({                             
          .event_type = latency::TagSet::EventType::JsonParse,
          .msg_type   = latency::TagSet::MsgType::L2                                                                                                                                                                                                                                             
      });                                                             
                                                                                                                                                                                                                                                                                                 
      std::printf("h == h2 ? %s\n", h == h2 ? "yes" : "NO (bug)");                                                                                                                                                                                                                               
      return 0;                                                       
  }      