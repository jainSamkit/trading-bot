#include "parser/delta_parser.hpp"

bool FeedParser::parse(const char* data, std::size_t len, FeedMessage& msg)
{
    (void)data;
    (void)len;
    (void)msg;
    return false;
}

bool FeedParser::validateChecksum(FeedMessage const& msg) const
{
    (void)msg;
    return true;
}

Action FeedParser::parseAction(const char* str) const
{
    (void)str;
    return Action::Snapshot;
}

bool FeedParser::checkSequence(uint64_t seq_no)
{
    (void)seq_no;
    return true;
}
