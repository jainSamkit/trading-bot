#pragma once
#include <httplib.h>
#include <memory>
#include <string>

class TcpClient {
public:
    explicit TcpClient(const char* host, int port = 443)
        : host_(host), port_(port) {}

    virtual ~TcpClient() = default;

    bool connect() {
        client_ = std::make_unique<httplib::SSLClient>(host_, port_);
        client_->set_connection_timeout(5);
        client_->set_read_timeout(10);
        return true;
    }

    bool is_connected() const { return client_ != nullptr; }
    void disconnect()         { client_.reset(); }

protected:
    // query is the raw query string e.g. "product_id=27&state=open"
    // empty string means no query params
    httplib::Result get(const std::string& path,
                        const std::string& query,
                        const httplib::Headers& headers = {}) {
        std::string full = query.empty() ? path : path + "?" + query;
        return client_->Get(full, headers);
    }

    httplib::Result post(const std::string& path,
                         const std::string& body,
                         const httplib::Headers& headers = {}) {
        return client_->Post(path, headers, body, "application/json");
    }

    httplib::Result put(const std::string& path,
                        const std::string& body,
                        const httplib::Headers& headers = {}) {
        return client_->Put(path, headers, body, "application/json");
    }

    httplib::Result del(const std::string& path,
                        const std::string& query,
                        const httplib::Headers& headers = {}) {
        std::string full = query.empty() ? path : path + "?" + query;
        return client_->Delete(full, headers);
    }

    const char* host_;
    int         port_;

private:
    std::unique_ptr<httplib::SSLClient> client_;
};
