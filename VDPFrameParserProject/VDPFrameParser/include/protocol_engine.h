#pragma once

#include "vdp_parser.h"
#include "transport_interface.h"
#include "types.h"
#include <memory>
#include <atomic>
#include <thread>
#include <queue>
#include <condition_variable>

namespace vdp {
namespace protocol {

/**
 * @brief Abstract base class for protocol engines
 *
 * Provides common functionality for all protocol implementations:
 * - Transport management
 * - Thread safety
 * - Error handling
 * - Lifecycle management
 */
class ProtocolEngineBase {
public:
    explicit ProtocolEngineBase(std::unique_ptr<transport::ITransport> transport);
    virtual ~ProtocolEngineBase();

    // Non-copyable, movable
    ProtocolEngineBase(const ProtocolEngineBase&) = delete;
    ProtocolEngineBase& operator=(const ProtocolEngineBase&) = delete;
    ProtocolEngineBase(ProtocolEngineBase&&) = default;
    ProtocolEngineBase& operator=(ProtocolEngineBase&&) = default;

    /**
     * @brief Initialize the protocol engine
     * @param connection_string Transport-specific connection parameters
     * @return true if successful, false otherwise
     */
    bool initialize(const std::string& connection_string);

    /**
     * @brief Check if engine is connected and operational
     */
    bool isConnected() const;

    /**
     * @brief Disconnect and cleanup
     */
    void disconnect();

    /**
     * @brief Get last error message
     */
    std::string getLastError() const;

protected:
    // Template method pattern - subclasses implement protocol-specific logic
    virtual void onFrameReceived(const VdpFrame& frame) = 0;
    virtual void onParseError(const std::string& error) = 0;
    virtual void onTransportError(const std::string& error) = 0;

    // Protected methods for subclasses
    bool sendRawData(const uint8_t* data, size_t length);
    void setLastError(const std::string& error);

private:
    std::unique_ptr<transport::ITransport> transport_;
    std::unique_ptr<VdpParser> parser_;

    // Thread safety
    mutable std::mutex mutex_;
    std::atomic<bool> connected_{false};
    std::string last_error_;

    // Data processing
    void onTransportDataReceived(const uint8_t* data, size_t length);
    void onTransportErrorReceived(const std::string& error);
    void processParserResults(const std::vector<ParseResult>& results);
};

/**
 * @brief VDP-specific protocol engine implementation
 *
 * Implements VDP protocol logic on top of the base engine:
 * - Frame validation and processing
 * - Response matching
 * - Timeout handling
 */
class VDPEngine : public ProtocolEngineBase {
public:
    explicit VDPEngine(std::unique_ptr<transport::ITransport> transport);
    ~VDPEngine() override = default;

    /**
     * @brief Send a VDP frame and wait for response (blocking)
     * @param frame Frame to send
     * @param timeout_ms Timeout in milliseconds
     * @return Response with status and data
     */
    protocol::Response sendFrame(const protocol::Frame& frame,
                                       uint32_t timeout_ms = 1000);

    /**
     * @brief Send a VDP frame asynchronously
     * @param frame Frame to send
     * @param on_response Callback for successful response
     * @param on_error Callback for errors/timeouts
     */
    void sendFrameAsync(const protocol::Frame& frame,
                       protocol::ResponseCallback on_response,
                       protocol::ErrorCallback on_error);

    /**
     * @brief Send raw data for debugging/testing
     * @param data Raw bytes to send
     * @return Response data (if any)
     */
    std::vector<uint8_t> sendRawData(const std::vector<uint8_t>& data);

protected:
    // ProtocolEngineBase overrides
    void onFrameReceived(const VdpFrame& frame) override;
    void onParseError(const std::string& error) override;
    void onTransportError(const std::string& error) override;

private:
    // Request tracking for async operations
    struct PendingRequest {
        uint32_t request_id;
        protocol::ResponseCallback response_callback;
        protocol::ErrorCallback error_callback;
        std::chrono::steady_clock::time_point timeout_time;
        protocol::Frame original_frame;
    };

    std::atomic<uint32_t> next_request_id_{1};
    std::map<uint32_t, PendingRequest> pending_requests_;
    std::mutex requests_mutex_;

    // Timeout management
    std::thread timeout_thread_;
    std::atomic<bool> stop_timeout_thread_{false};
    std::condition_variable timeout_cv_;

    void timeoutWorker();
    void checkTimeouts();
    uint32_t generateRequestId();

    // Frame conversion utilities
    VdpFrame convertToVdpFrame(const protocol::Frame& frame);
    protocol::Frame convertFromVdpFrame(const VdpFrame& frame);
    protocol::Response createResponse(protocol::Status status,
                                           const VdpFrame& frame);
};

} // namespace protocol
} // namespace vdp
