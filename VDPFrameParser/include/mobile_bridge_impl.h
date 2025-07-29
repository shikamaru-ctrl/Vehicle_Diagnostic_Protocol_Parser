#pragma once

#include "mobile_bridge.h"
#include "protocol_engine.h"
#include "transport_interface.h"
#include <memory>
#include <mutex>

namespace carly {
namespace protocol {

/**
 * @brief Implementation of IProtocolEngine that bridges to the VDP engine
 * 
 * This class implements the mobile_bridge.h interface and delegates to the
 * internal VDP protocol engine. It provides:
 * - Thread-safe operations for mobile platforms
 * - Error handling and status mapping
 * - Resource management
 */
class MobileBridgeImpl : public IProtocolEngine {
public:
    /**
     * @brief Constructor with transport type selection
     * @param transport_type Type of transport to use
     */
    explicit MobileBridgeImpl(vdp::transport::TransportFactory::Type transport_type = 
                             vdp::transport::TransportFactory::Type::SERIAL);
    
    ~MobileBridgeImpl() override;
    
    // IProtocolEngine implementation
    bool initialize(const std::string& device_path) override;
    Response sendFrame(const Frame& frame, uint32_t timeout_ms = 1000) override;
    void sendFrameAsync(const Frame& frame, 
                       ResponseCallback on_response,
                       ErrorCallback on_error) override;
    std::vector<uint8_t> sendRawData(const std::vector<uint8_t>& data) override;
    void processIncomingData(const uint8_t* data, size_t length) override;
    bool isConnected() const override;
    void disconnect() override;
    std::string getLastError() const override;

private:
    std::unique_ptr<vdp::protocol::VDPEngine> engine_;
    mutable std::mutex mutex_;
    std::string last_error_;
    
    // Utility methods
    void setLastError(const std::string& error);
    Status mapVdpStatusToMobile(vdp::ParseStatus vdp_status);
    vdp::VdpFrame convertToVdpFrame(const Frame& mobile_frame);
    Frame convertFromVdpFrame(const vdp::VdpFrame& vdp_frame);
};

/**
 * @brief Mock transport implementation for testing
 * 
 * This transport simulates communication without requiring actual hardware.
 * Useful for unit testing and mobile app development.
 */
class MockTransport : public vdp::transport::ITransport {
public:
    MockTransport();
    ~MockTransport() override = default;
    
    // ITransport implementation
    bool initialize(const std::string& connection_string) override;
    bool send(const uint8_t* data, size_t length) override;
    void setDataCallback(DataCallback callback) override;
    void setErrorCallback(ErrorCallback callback) override;
    bool isConnected() const override;
    void disconnect() override;
    std::string getLastError() const override;
    
    // Mock-specific methods for testing
    void simulateIncomingData(const std::vector<uint8_t>& data);
    void simulateError(const std::string& error);
    std::vector<uint8_t> getLastSentData() const;
    void setAutoResponse(bool enabled, const std::vector<uint8_t>& response = {});

private:
    DataCallback data_callback_;
    ErrorCallback error_callback_;
    bool connected_{false};
    std::string last_error_;
    std::vector<uint8_t> last_sent_data_;
    bool auto_response_enabled_{false};
    std::vector<uint8_t> auto_response_data_;
    mutable std::mutex mutex_;
};

} // namespace protocol
} // namespace carly

// C interface implementation for mobile platforms
extern "C" {
    carly::protocol::IProtocolEngine* createProtocolEngine();
    void destroyProtocolEngine(carly::protocol::IProtocolEngine* engine);
    
    // Additional C interface for transport selection (optional)
    carly::protocol::IProtocolEngine* createProtocolEngineWithTransport(int transport_type);
}
