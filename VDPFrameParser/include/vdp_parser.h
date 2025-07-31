//
// Created by prane on 27-07-2025.
//

#ifndef VDP_PARSER_H
#define VDP_PARSER_H

#include <cstdint>
#include <deque>
#include <vector>
#include <optional>
#include <string>
#include <chrono>
#include <map>
#include <functional>
#include <atomic>
#include <mutex>

namespace vdp {
    // Frame format: [0x7E][LEN][ECU_ID][CMD][DATA...][CHECKSUM][0x7F]
    // - LEN includes all bytes (min: 7, max: 255)
    // - CHECKSUM is XOR of all bytes between START and CHECKSUM (exclusive)
    struct VdpFrame {
        uint8_t ecu_id;             // Target ECU identifier (0x01-0x7F)
        uint8_t command;            // Command type
        std::vector<uint8_t> data;  // Command-specific data (0-247 bytes)
    };

    // Status of parse
    enum class ParseStatus {
        Success,    // complete valid frame
        Incomplete, // need more bytes
        Invalid,    // malformed frame
        Timeout,    // response timeout
        Nack,       // negative acknowledgment
        Error       // general error
    };

    // Response status codes
    enum class ResponseStatus : uint8_t {
        Success = 0x00,      // Operation successful
        InvalidCommand = 0x01, // Invalid command
        InvalidData = 0x02,  // Invalid data
        EcuBusy = 0x03,     // ECU busy
        GeneralError = 0xFF, // General error
        InvalidStatus = 0x80 // Invalid status code
    };

    // Constants for response frame handling
    static constexpr uint8_t RESPONSE_ECU_ID_MASK = 0x80; // Bitmask for response ECU_ID
    static constexpr uint8_t MIN_STATUS_CODE = 0x00;
    static constexpr uint8_t MAX_STATUS_CODE = 0xFF;

    // Command types
    enum class CommandType : uint8_t {
        // Standard VDP commands
        ReadData = 0x10,       // READ_DATA - Read diagnostic data
        WriteData = 0x20,      // WRITE_DATA - Write configuration
        ClearCodes = 0x30,     // CLEAR_CODES - Clear error codes
        EcuReset = 0x40,       // ECU_RESET - Reset ECU
        KeepAlive = 0x50,      // KEEP_ALIVE - Maintain connection
        
        // Protocol control commands
        Acknowledge = 0x06,    // ACK
        NegativeAck = 0x15     // NAK
    };
    
    /**
     * @brief Check if a command byte is valid
     * @param command The command byte to validate
     * @return true if the command is valid, false otherwise
     */
    static bool isValidCommand(uint8_t command) {
        switch (static_cast<CommandType>(command)) {
            case CommandType::ReadData:
            case CommandType::WriteData:
            case CommandType::ClearCodes:
            case CommandType::EcuReset:
            case CommandType::KeepAlive:
            case CommandType::Acknowledge:
            case CommandType::NegativeAck:
                return true;
            default:
                return false;
        }
    }

    // Result of parsing: status + optional frame + error message
    struct ParseResult {
        ParseStatus status;
        std::optional<VdpFrame> frame;
        std::string error;
        std::vector<uint8_t> raw_bytes;
        std::chrono::system_clock::time_point timestamp;
        
        // Default constructor with current timestamp
        ParseResult() : status(ParseStatus::Invalid), timestamp(std::chrono::system_clock::now()) {}
        
        // Constructor with parameters
        ParseResult(ParseStatus s, const std::optional<VdpFrame>& f, const std::string& e, const std::vector<uint8_t>& rb)
            : status(s), frame(f), error(e), raw_bytes(rb), timestamp(std::chrono::system_clock::now()) {}
    };
    
    // Response handler function type
    using ResponseHandler = std::function<void(const ParseResult&)>;
    
    // Send callback type
    using SendCallback = std::function<void(const uint8_t* data, size_t len)>;
    
    // Pending request information
    struct PendingRequest {
        VdpFrame request_frame;
        ResponseHandler handler;
        std::chrono::system_clock::time_point timeout_time;
        std::atomic<bool> completed{false};
        
        // Default constructor
        PendingRequest() = default;
        
        // Move constructor
        PendingRequest(PendingRequest&& other) noexcept
            : request_frame(std::move(other.request_frame))
            , handler(std::move(other.handler))
            , timeout_time(other.timeout_time)
            , completed(other.completed.load()) {
        }
        
        // Move assignment
        PendingRequest& operator=(PendingRequest&& other) noexcept {
            if (this != &other) {
                request_frame = std::move(other.request_frame);
                handler = std::move(other.handler);
                timeout_time = other.timeout_time;
                completed.store(other.completed.load());
            }
            return *this;
        }
        
        // Delete copy constructor and assignment
        PendingRequest(const PendingRequest&) = delete;
        PendingRequest& operator=(const PendingRequest&) = delete;
    };

    class VdpParser {
    public:
        // Default constructor with default timeout of 1 second
        explicit VdpParser(std::chrono::milliseconds default_timeout = std::chrono::seconds(1));
        
        // Feed raw incoming bytes (maybe partial or batched)
        void feed(const uint8_t* data, size_t len);

        // Attempt to parse as many frames as possible
        std::vector<ParseResult> extractFrames();

        // Clear internal buffer (e.g. on reset)
        void reset();
        
        // Send a frame and set up response handling
        // @param frame The frame to send
        // @param handler Callback function to handle the response
        // @param timeout_ms Optional timeout in milliseconds (0 for default)
        void sendFrame(const VdpFrame& frame, ResponseHandler handler, 
                      std::chrono::milliseconds timeout_ms = std::chrono::milliseconds(0));
                      
        // Send a frame and wait for response (blocking)
        // @param frame The frame to send
        // @param timeout_ms Maximum time to wait for response
        // @return ParseResult with the response or error
        ParseResult sendAndWait(const VdpFrame& frame, 
                               std::chrono::milliseconds timeout_ms = std::chrono::milliseconds(1000));
        
        // Set default timeout for all requests
        void setDefaultTimeout(std::chrono::milliseconds timeout);
        
        // Process any pending timeouts
        void checkTimeouts();
        
        // Generate an ACK frame for the given frame
        VdpFrame createAckFrame(const VdpFrame& frame);
        
        // Generate a NAK frame for the given frame with optional error code
        VdpFrame createNakFrame(const VdpFrame& frame, uint8_t error_code = 0x01);
        
        // Helper to find the next start byte in the buffer
        size_t findNextStartByte() const;
        
        // Helper to check if frame is taking too long
        bool isFrameTakingTooLong() const;
        
        // Helper to reset frame parsing state
        void resetFrameState();
        
        /**
         * @brief Serialize a VdpFrame into a byte vector
         * @param frame The frame to serialize
         * @param out Output vector for the serialized data
         */
        void serializeFrame(const VdpFrame& frame, std::vector<uint8_t>& out) const;
        
        /**
         * @brief Set the send callback for sending responses
         * @param callback The callback function to use for sending data
         */
        void setSendCallback(SendCallback callback) { send_callback_ = std::move(callback); }

    private:
        // Internal buffer for incoming data
        std::deque<uint8_t> buffer_;
        
        // Mutex for thread safety
        std::mutex mutex_;
        
        // Default timeout for requests
        std::chrono::milliseconds default_timeout_;
        
        // Map of pending requests by sequence number or command ID
        std::map<uint8_t, PendingRequest> pending_requests_;
        
        // Last used sequence number
        std::atomic<uint8_t> last_sequence_{0};
        
        // Callback for sending data
        SendCallback send_callback_;
        // Frame parsing state
        bool frame_started_ = false;
        std::chrono::steady_clock::time_point last_frame_start_;
        
        // Process a received frame and match it with pending requests
        void processReceivedFrame(const VdpFrame& frame);
        
        // Handle ACK/NAK frames
        void handleAckNak(const VdpFrame& frame, bool is_ack);
        
        // Generate a new sequence number
        uint8_t getNextSequence();
        
        // Find a pending request that matches the response frame
        std::map<uint8_t, PendingRequest>::iterator findMatchingRequest(const VdpFrame& response_frame);

        // Frame format constants
        static constexpr uint8_t START_BYTE = 0x7E;
        static constexpr uint8_t END_BYTE   = 0x7F;
        static constexpr size_t MIN_FRAME   = 7;    // [7E][05][01][01][XX][7F] (min frame with empty data)
        static constexpr size_t MAX_FRAME   = 253;  // Maximum frame size including start/end bytes
        static constexpr size_t HEADER_SIZE = 4;    // [7E][LEN][ECU][CMD]
        static constexpr size_t FOOTER_SIZE = 2;    // [CHK][7F]

        // Try parse one frame; return {status, optional frame, error}.
        ParseResult parseOne();
        
        // Verify checksum for a complete frame
        // @param frame The frame to verify
        // @param debugOutput Output parameter for debug information
        // @return true if checksum is valid, false otherwise
        bool verifyChecksum(const std::vector<uint8_t>& frame, std::string& debugOutput) const;
    };
} // namespace vdp
#endif //VDP_PARSER_H
