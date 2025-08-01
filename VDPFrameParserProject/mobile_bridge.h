#pragma once

#include <vector>
#include <string>
#include <functional>
#include <cstdint>

namespace carly {
namespace protocol {

// Status codes that match VDP specification
enum class Status : uint8_t {
    SUCCESS = 0x00,
    INVALID_COMMAND = 0x01,
    INVALID_DATA = 0x02,
    ECU_BUSY = 0x03,
    TIMEOUT = 0xFE,
    GENERAL_ERROR = 0xFF
};

// Parsed frame representation
struct Frame {
    uint8_t ecu_id;
    uint8_t command;
    std::vector<uint8_t> data;
    
    Frame() = default;
    Frame(uint8_t ecu, uint8_t cmd) : ecu_id(ecu), command(cmd) {}
};

// Response frame with status
struct Response {
    Status status;
    Frame frame;
    
    bool isSuccess() const { return status == Status::SUCCESS; }
};

// Callback types for async operations
using ResponseCallback = std::function<void(const Response&)>;
using ErrorCallback = std::function<void(const std::string&)>;

// Main interface exposed to mobile platforms
class IProtocolEngine {
public:
    virtual ~IProtocolEngine() = default;
    
    // Initialize the engine with device path
    virtual bool initialize(const std::string& device_path) = 0;
    
    // Send a frame and get response (blocking)
    // Mobile platforms may call this from any thread
    virtual Response sendFrame(const Frame& frame, uint32_t timeout_ms = 100) = 0;
    
    // Send a frame asynchronously  
    // Callbacks may be invoked from any thread
    virtual void sendFrameAsync(const Frame& frame, 
                               ResponseCallback on_response,
                               ErrorCallback on_error) = 0;
    
    // Raw data interface (for debugging/testing)
    virtual std::vector<uint8_t> sendRawData(const std::vector<uint8_t>& data) = 0;
    
    // Buffer management - implementation defined
    virtual void processIncomingData(const uint8_t* data, size_t length) = 0;
    
    // Connection management
    virtual bool isConnected() const = 0;
    virtual void disconnect() = 0;
    
    // Get last error for debugging
    virtual std::string getLastError() const = 0;
};

// Factory function to create engine instance
// Note: Implementation should be provided by the C++ library
extern "C" IProtocolEngine* createProtocolEngine();
extern "C" void destroyProtocolEngine(IProtocolEngine* engine);

} // namespace protocol
} // namespace carly