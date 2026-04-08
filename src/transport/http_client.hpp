#pragma once
#include <httplib.h>
#include <memory>
#include <string>

class TcpClient {
public:
    explicit TcpClient(const char* host, int port = 443)
        : host_(host), port_(port) {}

    virtual ~TcpClient() = default;

    bool connect();

    bool is_connected() const { return client_ != nullptr; }
    void disconnect()         { client_.reset(); }

protected:
    // query is the raw query string e.g. "product_id=27&state=open"
    // empty string means no query params
    httplib::Result get(const std::string& path,
                        const std::string& query,
                        const httplib::Headers& headers = {});

    httplib::Result post(const std::string& path,
                         const std::string& body,
                         const httplib::Headers& headers = {});

    httplib::Result put(const std::string& path,
                        const std::string& body,
                        const httplib::Headers& headers = {});

    httplib::Result del(const std::string& path,
                        const std::string& query,
                        const httplib::Headers& headers = {});

    const char* host_;
    int         port_;

private:
    std::unique_ptr<httplib::SSLClient> client_;
};
