#include "transport/http_client.hpp"

bool TcpClient::connect() {
    client_ = std::make_unique<httplib::SSLClient>(host_, port_);
    client_->set_connection_timeout(5);
    client_->set_read_timeout(10);
    return true;
}

httplib::Result TcpClient::get(const std::string& path,
                                const std::string& query,
                                const httplib::Headers& headers) {
    std::string full = query.empty() ? path : path + "?" + query;
    return client_->Get(full, headers);
}

httplib::Result TcpClient::post(const std::string& path,
                                 const std::string& body,
                                 const httplib::Headers& headers) {
    return client_->Post(path, headers, body, "application/json");
}

httplib::Result TcpClient::put(const std::string& path,
                                const std::string& body,
                                const httplib::Headers& headers) {
    return client_->Put(path, headers, body, "application/json");
}

httplib::Result TcpClient::del(const std::string& path,
                                const std::string& query,
                                const httplib::Headers& headers) {
    std::string full = query.empty() ? path : path + "?" + query;
    return client_->Delete(full, headers);
}
