#include "vdp_parser.h"
#include <stdexcept>
#include <iostream>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <condition_variable>

using namespace vdp;

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
    bool found_frame = false;

    while (buffer_.size() >= 2) {  // Need at least [7E][LEN] to proceed
        // 1) Find the next start byte (0x7E)
        if (buffer_[0] != START_BYTE) {
            // If first byte is not a start byte, remove it and try again
            buffer_.pop_front();
            if (found_frame) {  // Only report garbage if we've already processed a frame
                results.push_back({
                    ParseStatus::Invalid,
                    {},
                    "Garbage data at start of buffer, resyncing"
                });
            }
            found_frame = false;
            continue;
        }

        // 2) Check if we have at least 2 bytes ([7E][LEN])
        if (buffer_.size() < 2) {
            break;  // Need at least 2 bytes to proceed
        }

        // 3) Get frame length (includes all bytes in frame)
        uint8_t frame_length = buffer_[1];

        // 4) Validate frame length (minimum is 6: [7E][LEN][ECU][CMD][CHK][7F])
        if (frame_length < 6 || frame_length > MAX_FRAME) {
            // Invalid length, remove the start byte and try again
            buffer_.pop_front();
            results.push_back({
                ParseStatus::Invalid,
                {},
                "Invalid frame length " + std::to_string(frame_length) +
                " (expected 6-" + std::to_string(MAX_FRAME) + ")"
            });
            continue;
        }

        // 5) Check if we have enough data for the complete frame
        if (buffer_.size() < frame_length) {
            // Not enough data yet, wait for more
            break;
        }

        // 6) Extract the complete frame
        std::vector<uint8_t> frame(buffer_.begin(), buffer_.begin() + frame_length);

        // 7) Verify end byte (must be at position frame_length-1)
        if (frame.back() != END_BYTE) {
            // Invalid end byte, remove the frame and try again
            buffer_.erase(buffer_.begin(), buffer_.begin() + frame_length);
            results.push_back({
                ParseStatus::Invalid,
                {},
                "Invalid end byte, expected 0x7F"
            });
            found_frame = true;
            continue;
        }

        // 8) Verify checksum
        std::string checksumDebug;
        if (!verifyChecksum(frame, checksumDebug)) {
            // Checksum failed, remove the frame and try again
            buffer_.erase(buffer_.begin(), buffer_.begin() + frame_length);
            results.push_back({
                ParseStatus::Invalid,
                {},
                checksumDebug
            });
            found_frame = true;
            continue;
        }

        // 9) Parse the frame
        VdpFrame vdp_frame;
        vdp_frame.ecu_id = frame[2];  // After LEN
        vdp_frame.command = frame[3]; // After ECU_ID
        
        // Extract data (between CMD and CHECKSUM)
        if (frame_length > 6) {  // If there's data between CMD and CHECKSUM
            vdp_frame.data.assign(frame.begin() + 4, frame.end() - 2);
        }

        // 10) Remove the processed frame from the buffer
        buffer_.erase(buffer_.begin(), buffer_.begin() + frame_length);
        found_frame = true;

        // 11) Process the received frame with response handling
        processReceivedFrame(vdp_frame);
        
        // 12) Add success result
        results.push_back({
            ParseStatus::Success, 
            vdp_frame, 
            ""
        });
    }

    // Check for timeouts on pending requests
    checkTimeouts();
    
    // Resynchronize buffer to next start byte (0x7E)
    while (!buffer_.empty() && buffer_[0] != START_BYTE) {
        buffer_.pop_front();
    }

    // After processing all possible frames, check for partial frame
    if (buffer_.size() >= 2 && buffer_[0] == START_BYTE) {
        uint8_t frame_length = buffer_[1];
        if (frame_length >= 6 && frame_length <= MAX_FRAME && buffer_.size() < frame_length) {
            int missing = frame_length - buffer_.size();
            results.push_back({
                ParseStatus::Incomplete,
                {},
                "Partial frame, waiting for " + std::to_string(missing) + " more bytes"
            });
        }
    }

    return results;
}

// Constructor
VdpParser::VdpParser(std::chrono::milliseconds default_timeout) 
    : default_timeout_(default_timeout) {
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
        return {ParseStatus::Timeout, std::nullopt, "Response timeout"};
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
    
    // Try to find a matching request
    auto it = findMatchingRequest(frame);
    if (it != pending_requests_.end()) {
        // Found a matching request, call its handler
        ParseResult result{ParseStatus::Success, frame, ""};
        it->second.handler(result);
        it->second.completed = true;
        pending_requests_.erase(it);
    }
    
    // TODO: Handle unsolicited frames if needed
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
            result = {ParseStatus::Success, frame, "ACK received"};
        } else {
            std::string error = "NAK received";
            if (frame.data.size() > 1) {
                error += ", error code: " + std::to_string(frame.data[1]);
            }
            result = {ParseStatus::Nack, frame, error};
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
            ParseResult result{ParseStatus::Timeout, std::nullopt, "Request timed out"};
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
