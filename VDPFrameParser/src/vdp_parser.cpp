#include "vdp_parser.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <condition_variable>

using namespace vdp;

// Helper function to convert a byte to a 2-character hex string
static std::string to_hex(uint8_t byte) {
    std::stringstream ss;
    ss << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return ss.str();
}

void VdpParser::serializeFrame(const VdpFrame& frame, std::vector<uint8_t>& out) const {
    // Clear output vector
    out.clear();
    
    // Start with the frame start byte
    out.push_back(START_BYTE);
    
    // Calculate frame length (ECU_ID + CMD + DATA + CHK)
    size_t data_length = 2 + frame.data.size(); // ECU_ID (1) + CMD (1) + DATA (n)
    if (data_length > 255 - 4) { // 4 = START(1) + LEN(1) + CHK(1) + END(1)
        throw std::runtime_error("Frame data too large");
    }
    
    // Add length byte (including all bytes except START and END)
    out.push_back(static_cast<uint8_t>(data_length + 2)); // +2 for CHK and LEN itself
    
    // Add ECU ID and command
    out.push_back(frame.ecu_id);
    out.push_back(frame.command);
    
    // Add data if present
    if (!frame.data.empty()) {
        out.insert(out.end(), frame.data.begin(), frame.data.end());
    }
    
    // Calculate and add checksum (XOR of all bytes after START)
    uint8_t checksum = 0;
    for (size_t i = 1; i < out.size(); ++i) {
        checksum ^= out[i];
    }
    out.push_back(checksum);
    
    // Add end byte
    out.push_back(END_BYTE);
}

bool VdpParser::verifyChecksum(const std::vector<uint8_t>& frame, std::string& debugOutput) const {
    // Frame must have at least: [7E][LEN][ECU][CMD][CHK][7F] (6 bytes)
    if (frame.size() < 6) {
        std::stringstream ss;
        ss << "Frame too short for checksum verification (size: " << frame.size() << ")";
        debugOutput = ss.str();
        return false;
    }
    
    uint8_t calculated_checksum = 0;
    // XOR all bytes from LEN until the checksum byte
    for (size_t i = 1; i < frame.size() - 2; ++i) {
        calculated_checksum ^= frame[i];
    }
    
    // Get the expected checksum (byte before the end byte)
    uint8_t expected_checksum = frame[frame.size() - 2];
    
    if (calculated_checksum != expected_checksum) {
        std::stringstream ss;
        ss << "Checksum verification failed: " 
           << "calculated=0x" << std::hex << (int)calculated_checksum 
           << ", expected=0x" << (int)expected_checksum << std::dec;
        debugOutput = ss.str();
        return false;
    }
    
    return true;
}

std::vector<ParseResult> VdpParser::extractFrames() {
    std::vector<ParseResult> results;

    while (true) {
        // 1. Find the next start byte and discard any garbage before it.
        // This is the key to resynchronization after an error.
        size_t start_byte_pos = 0;
        while (start_byte_pos < buffer_.size() && buffer_[start_byte_pos] != START_BYTE) {
            start_byte_pos++;
        }

        if (start_byte_pos > 0) {
            buffer_.erase(buffer_.begin(), buffer_.begin() + start_byte_pos);
        }

        // If we don't have enough data for a header, we're done for now.
        if (buffer_.size() < 2) {
            break;
        }

        // At this point, buffer_[0] is START_BYTE.

        // 2. Get frame length and validate.
        uint8_t frame_length = buffer_[1];
        if (frame_length < MIN_FRAME_LEN || frame_length > MAX_FRAME_LEN) {
            std::vector<uint8_t> invalid_data(buffer_.begin(), buffer_.begin() + 2);
            results.push_back({ParseStatus::Invalid, {}, "Invalid frame length: " + std::to_string(frame_length), invalid_data});
            buffer_.pop_front(); // Discard the bad 0x7E and rescan.
            continue;
        }

        // 3. Check if the full frame is in the buffer.
        if (buffer_.size() < frame_length) {
            // Not enough data yet. Stop processing and wait for more to arrive.
            break;
        }

        // 4. Check for the end marker.
        if (buffer_[frame_length - 1] != END_BYTE) {
            std::vector<uint8_t> invalid_data(buffer_.begin(), buffer_.begin() + frame_length);
            results.push_back({ParseStatus::Invalid, {}, "End marker not found at position: " + std::to_string(frame_length - 1), invalid_data});
            buffer_.pop_front(); // Discard the bad 0x7E and rescan.
            continue;
        }

        // 5. Extract the frame and verify checksum.
        std::vector<uint8_t> frame(buffer_.begin(), buffer_.begin() + frame_length);
        std::string checksumDebug;
        if (!verifyChecksum(frame, checksumDebug)) {
            results.push_back({ParseStatus::Invalid, {}, checksumDebug, frame});
            buffer_.pop_front(); // Discard the bad 0x7E and rescan.
            continue;
        }

        // 6. Process the valid frame.
        VdpFrame vdp_frame;
        vdp_frame.ecu_id = frame[2];
        vdp_frame.command = frame[3];
        vdp_frame.data.assign(frame.begin() + 4, frame.end() - 2);
        results.push_back({ParseStatus::Success, vdp_frame, "", frame});

        // 7. Remove the processed frame from the buffer.
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_length);
    }

    checkTimeouts();
    return results;
}

// Constructor
VdpParser::VdpParser(std::chrono::milliseconds default_timeout)
    : default_timeout_(default_timeout), frame_started_(false) {
    // Initialize frame timing state
    resetFrameState();
}

// Helper to check if frame is taking too long
bool VdpParser::isFrameTakingTooLong() const {
    if (!frame_started_) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - last_frame_start_);
    
    // Consider a frame taking too long if it's been more than 2x the default timeout
    return elapsed > (default_timeout_ * 2);
}

void VdpParser::resetFrameState() {
    frame_started_ = false;
    last_frame_start_ = std::chrono::steady_clock::now();
}

// Feed implementation with mutex protection
void VdpParser::feed(const uint8_t* data, size_t len) {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.insert(buffer_.end(), data, data + len);
}

// Reset implementation with mutex protection
void VdpParser::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    buffer_.clear();
    resetFrameState();
}

// Send frame with response handler
void VdpParser::sendFrame(const VdpFrame& frame, ResponseHandler handler, 
                         std::chrono::milliseconds timeout_ms) {
    if (timeout_ms == std::chrono::milliseconds(0)) {
        timeout_ms = default_timeout_;
    }
    
    auto now = std::chrono::system_clock::now();
    auto timeout_time = now + timeout_ms;
    
    // Add sequence number to the frame if needed
    uint8_t sequence = getNextSequence();
    
    // Store the pending request
    PendingRequest request;
    request.request_frame = frame;
    request.handler = std::move(handler);
    request.timeout_time = timeout_time;
    request.completed = false;
    
    {
        std::lock_guard<std::mutex> lock(mutex_);
        pending_requests_[sequence] = std::move(request);
    }
    
    // TODO: Implement actual frame sending logic
    // sendRawFrame(frame);
}

// Send and wait for response
ParseResult VdpParser::sendAndWait(const VdpFrame& frame, 
                                  std::chrono::milliseconds timeout_ms) {
    std::mutex mtx;
    std::condition_variable cv;
    ParseResult result;
    bool response_received = false;
    
    auto handler = [&](const ParseResult& res) {
        std::unique_lock<std::mutex> lock(mtx);
        result = res;
        response_received = true;
        cv.notify_one();
    };
    
    sendFrame(frame, handler, timeout_ms);
    
    std::unique_lock<std::mutex> lock(mtx);
    if (cv.wait_for(lock, timeout_ms, [&] { return response_received; })) {
        return result;
    } else {
        return {ParseStatus::Timeout, std::nullopt, "Response timeout", {}};
    }
}

// Process received frame and match with pending requests
void VdpParser::processReceivedFrame(const VdpFrame& frame) {
    // Handle ACK/NAK frames first
    if (frame.command == static_cast<uint8_t>(CommandType::Acknowledge)) {
        handleAckNak(frame, true);
        return;
    } else if (frame.command == static_cast<uint8_t>(CommandType::NegativeAck)) {
        handleAckNak(frame, false);
        return;
    }
    
    // Check if this is a response frame (ECU_ID has 0x80 bit set)
    bool is_response = (frame.ecu_id & RESPONSE_ECU_ID_MASK) != 0;
    
    // For response frames, validate status code
    if (is_response && !frame.data.empty()) {
        uint8_t status = frame.data[0];
        
        // Special handling for invalid status codes
        if (status == 0x00 || status == 0x80) {
            // Create and send NAK for invalid status code
            VdpFrame nak_frame;
            nak_frame.ecu_id = frame.ecu_id & ~RESPONSE_ECU_ID_MASK; // Clear response bit
            nak_frame.command = static_cast<uint8_t>(CommandType::NegativeAck);
            nak_frame.data.push_back(frame.command);
            nak_frame.data.push_back(static_cast<uint8_t>(ResponseStatus::InvalidStatus));
            
            std::vector<uint8_t> nak_bytes;
            serializeFrame(nak_frame, nak_bytes);
            if (send_callback_) {
                send_callback_(nak_bytes.data(), nak_bytes.size());
            }
            return;
        }
    }
    
    // Validate the command
    if (!isValidCommand(frame.command)) {
        // Create and send NAK for invalid command
        VdpFrame nak_frame;
        nak_frame.ecu_id = frame.ecu_id & ~RESPONSE_ECU_ID_MASK; // Clear response bit if set
        nak_frame.command = static_cast<uint8_t>(CommandType::NegativeAck);
        nak_frame.data.push_back(frame.command); // Include the invalid command
        nak_frame.data.push_back(static_cast<uint8_t>(ResponseStatus::InvalidCommand));
        
        std::vector<uint8_t> nak_bytes;
        serializeFrame(nak_frame, nak_bytes);
        if (send_callback_) {
            send_callback_(nak_bytes.data(), nak_bytes.size());
        }
        return;
    }

    // Try to find a matching request
    auto it = findMatchingRequest(frame);
    if (it != pending_requests_.end()) {
        // Found a matching request, call its handler
        ParseResult result{ParseStatus::Success, frame, "", {}};
        it->second.handler(result);
        it->second.completed = true;
        pending_requests_.erase(it);
    } else {
        // Handle unsolicited frames (no matching request)
        // TODO: Add handling for unsolicited frames if needed
        if (frame.command != static_cast<uint8_t>(CommandType::KeepAlive)) {
            // Log or handle unexpected frame (except keep-alive which is always unsolicited)
        }
    }
}

// Helper to get status string from status code
static std::string getStatusString(uint8_t status_code) {
    switch (static_cast<ResponseStatus>(status_code)) {
        case ResponseStatus::Success: return "Success";
        case ResponseStatus::InvalidCommand: return "Invalid Command";
        case ResponseStatus::InvalidData: return "Invalid Data";
        case ResponseStatus::EcuBusy: return "ECU Busy";
        case ResponseStatus::GeneralError: return "General Error";
        case ResponseStatus::InvalidStatus: return "Invalid Status";
        default: return "Unknown Status";
    }
}

// Validate response frame and extract status
static bool validateResponseFrame(const VdpFrame& frame, uint8_t& out_status) {
    // Check if this is a response frame (ECU_ID has 0x80 bit set)
    if ((frame.ecu_id & RESPONSE_ECU_ID_MASK) == 0) {
        return false; // Not a response frame
    }

    // Response must have at least one data byte for status
    if (frame.data.empty()) {
        return false;
    }

    out_status = frame.data[0];
    return true;
}

// Handle ACK/NAK frames
void VdpParser::handleAckNak(const VdpFrame& frame, bool is_ack) {
    if (frame.data.size() < 1) {
        // Invalid ACK/NAK - no sequence number
        return;
    }
    
    uint8_t sequence = frame.data[0];
    auto it = pending_requests_.find(sequence);
    if (it != pending_requests_.end()) {
        ParseResult result;
        if (is_ack) {
            if (frame.data.size() > 1) {
                // Check for special status codes in ACK
                uint8_t status = frame.data[1];
                if (status == static_cast<uint8_t>(ResponseStatus::InvalidStatus) || 
                    status == 0x00) {
                    // Treat as error
                    result = {
                        ParseStatus::Error, 
                        frame, 
                        "ACK with invalid status code: 0x" + to_hex(status),
                        {}
                    };
                } else {
                    result = {ParseStatus::Success, frame, "ACK received", {}};
                }
            } else {
                result = {ParseStatus::Success, frame, "ACK received", {}};
            }
        } else {
            // NAK handling
            std::string error = "NAK received";
            if (frame.data.size() > 1) {
                uint8_t error_code = frame.data[1];
                error += ": " + getStatusString(error_code) + " (0x" + to_hex(error_code) + ")";
                
                // Special handling for 0x00 and 0x80
                if (error_code == 0x00 || error_code == 0x80) {
                    error += " - Invalid status code";
                }
            }
            result = {ParseStatus::Nack, frame, error, {}};
        }
        it->second.handler(result);
        it->second.completed = true;
        pending_requests_.erase(it);
    }
}

// Generate a new sequence number
uint8_t VdpParser::getNextSequence() {
    // Simple incrementing sequence number (wraps around at 255)
    return ++last_sequence_;
}

// Find a pending request that matches the response frame
std::map<uint8_t, PendingRequest>::iterator VdpParser::findMatchingRequest(const VdpFrame& response_frame) {
    // Simple implementation: match by command type and ECU ID
    // You might want to implement more sophisticated matching logic
    for (auto it = pending_requests_.begin(); it != pending_requests_.end(); ++it) {
        const auto& request = it->second;
        if (!request.completed && 
            request.request_frame.command == response_frame.command &&
            request.request_frame.ecu_id == response_frame.ecu_id) {
            return it;
        }
    }
    return pending_requests_.end();
}

// Check for and handle timeouts
void VdpParser::checkTimeouts() {
    auto now = std::chrono::system_clock::now();
    
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto it = pending_requests_.begin(); it != pending_requests_.end(); ) {
        if (now > it->second.timeout_time) {
            // Request timed out
            ParseResult result{ParseStatus::Timeout, std::nullopt, "Request timed out", {}};
            it->second.handler(result);
            it = pending_requests_.erase(it);
        } else {
            ++it;
        }
    }
}

// Set default timeout for all requests
void VdpParser::setDefaultTimeout(std::chrono::milliseconds timeout) {
    default_timeout_ = timeout;
}

// Find the next start byte in the buffer
size_t VdpParser::findNextStartByte() const {
    if (buffer_.empty()) {
        return std::string::npos;
    }
    
    // Start from the second byte since we know the first one is not a start byte
    for (size_t i = 1; i < buffer_.size(); ++i) {
        if (buffer_[i] == START_BYTE) {
            return i;
        }
    }
    
    // No start byte found
    return std::string::npos;
}

// Create ACK frame
VdpFrame VdpParser::createAckFrame(const VdpFrame& frame) {
    VdpFrame ack_frame;
    ack_frame.ecu_id = frame.ecu_id;
    ack_frame.command = static_cast<uint8_t>(CommandType::Acknowledge);
    ack_frame.data.push_back(frame.command); // Include the command being acknowledged
    return ack_frame;
}

// Create NAK frame
VdpFrame VdpParser::createNakFrame(const VdpFrame& frame, uint8_t error_code) {
    VdpFrame nak_frame;
    nak_frame.ecu_id = frame.ecu_id;
    nak_frame.command = static_cast<uint8_t>(CommandType::NegativeAck);
    nak_frame.data.push_back(frame.command); // Include the command being NAK'ed
    nak_frame.data.push_back(error_code);    // Include error code if provided
    return nak_frame;
}
