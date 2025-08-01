#pragma once

#include <functional>
#include <memory>
#include <string>
#include <cstdint>

namespace vdp {
namespace transport {

/**
 * @brief Transport layer interface for different communication channels
 * 
 * This interface abstracts the underlying transport mechanism (CAN, DoIP, Bluetooth, etc.)
 * allowing the protocol engine to work with any transport implementation.
 */
class ITransport {
public:
    virtual ~ITransport() = default;
    
    // Callback type for received data
    using DataCallback = std::function<void(const uint8_t* data, size_t length)>;
    using ErrorCallback = std::function<void(const std::string& error)>;
    
    /**
     * @brief Initialize the transport with connection parameters
     * @param connection_string Transport-specific connection string (e.g., device path, IP:port)
     * @return true if initialization successful, false otherwise
     */
    virtual bool initialize(const std::string& connection_string) = 0;
    
    /**
     * @brief Send raw data through the transport
     * @param data Pointer to data buffer
     * @param length Number of bytes to send
     * @return true if send successful, false otherwise
     */
    virtual bool send(const uint8_t* data, size_t length) = 0;
    
    /**
     * @brief Set callback for received data
     * @param callback Function to call when data is received
     */
    virtual void setDataCallback(DataCallback callback) = 0;
    
    /**
     * @brief Set callback for transport errors
     * @param callback Function to call when transport error occurs
     */
    virtual void setErrorCallback(ErrorCallback callback) = 0;
    
    /**
     * @brief Check if transport is connected and ready
     * @return true if connected, false otherwise
     */
    virtual bool isConnected() const = 0;
    
    /**
     * @brief Disconnect and cleanup transport
     */
    virtual void disconnect() = 0;
    
    /**
     * @brief Get last error message for debugging
     * @return Error message string
     */
    virtual std::string getLastError() const = 0;
};

/**
 * @brief Factory for creating transport instances
 */
class TransportFactory {
public:
    enum class Type {
        MOCK,      // For testing
        SERIAL,    // Serial/USB
        CAN,       // CAN bus
        DOIP,      // Diagnostic over IP
        BLUETOOTH  // Bluetooth
    };
    
    static std::unique_ptr<ITransport> create(Type type);
};

} // namespace transport
} // namespace vdp
