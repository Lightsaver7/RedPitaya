#ifndef NET_LIB_ASIO_SOCKET_DMA_H
#define NET_LIB_ASIO_SOCKET_DMA_H

#include "asio_common.h"
#include "asio_service.h"
#include "data_lib/buffers_cached.h"
#include "data_lib/signal.hpp"

using namespace DataLib;

namespace net_lib {

class CAsioSocketDMA {
   public:
    using Ptr = std::shared_ptr<CAsioSocketDMA>;

    static Ptr create(std::string host, uint16_t port, CBuffersCached::Ptr buffers);

    CAsioSocketDMA(std::string host, uint16_t port, CBuffersCached::Ptr buffers);
    ~CAsioSocketDMA();

    auto initServer() -> void;
    auto initClient() -> void;
    auto stopReceive() -> void;
    auto cancelSocket() -> void;
    auto closeSocket() -> void;
    auto isConnected() -> bool;
    auto sendSyncBuffer(DataLib::CDataBuffersPackDMA::Ptr _buffer) -> bool;

    sigslot::signal<std::string&> connectServerNotify;
    sigslot::signal<std::string&> disconnectServerNotify;

    sigslot::signal<std::string&> connectClientNotify;
    sigslot::signal<std::string&> disconnectClientNotify;

    sigslot::signal<std::error_code> errorServerNotify;
    sigslot::signal<std::error_code> errorClientNotify;

    sigslot::signal<std::error_code, size_t> sendNotify;
    sigslot::signal<std::error_code, DataLib::CDataBuffersPackDMA::Ptr> recivedNotify;

   private:
    CAsioSocketDMA(const CAsioSocketDMA&) = delete;
    CAsioSocketDMA(CAsioSocketDMA&&) = delete;
    CAsioSocketDMA& operator=(const CAsioSocketDMA&) = delete;
    CAsioSocketDMA& operator=(const CAsioSocketDMA&&) = delete;

    auto handlerAcceptFromClient(const asio::error_code& _error) -> void;
    auto handlerConnectToServer(const asio::error_code& _error) -> void;
    auto handlerSend(const asio::error_code& _error, size_t _bytesTransferred) -> void;
    auto handlerReceive(const asio::error_code& ErrorCode, size_t bytes_transferred) -> void;

    auto getBuffer() -> void;
    auto unlockBuffer() -> void;

    net_lib::EMode m_mode;
    std::string m_host;
    uint16_t m_port;

    std::shared_ptr<asio::ip::tcp::socket> m_tcp_socket;
    std::shared_ptr<asio::ip::tcp::acceptor> m_tcp_acceptor;
    asio::ip::tcp::endpoint m_tcp_endpoint;
    asio::ip::tcp::resolver::results_type m_endpoints;

    std::mutex m_mtx;
    CAsioService* m_asio;
    uint64_t m_bufferSize;
    CBuffersCached::Ptr m_buffers;
    CDataBuffersPackDMA::Ptr m_currentBuffer;
    bool m_isStopReceive;
};

}  // namespace net_lib

#endif
