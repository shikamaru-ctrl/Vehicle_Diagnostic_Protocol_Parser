//
// Created by prane on 01-08-2025.
//

#ifndef TYPES_H
#define TYPES_H
#ifndef VDPFRAMEPROCESSOR_TYPES_H
#define VDPFRAMEPROCESSOR_TYPES_H

#include <cstdint>
#include <vector>
#include <string>
#include <functional>

namespace vdp {
    namespace protocol {

        // Represents a high-level data frame, independent of VDP specifics.
        struct Frame {
            uint8_t ecu_id;
            uint8_t command;
            std::vector<uint8_t> data;
        };

        // Represents the status of a response.
        enum class Status {
            Success,
            Error,
            Timeout
        };

        // Represents a response to a sent frame.
        struct Response {
            Status status;
            Frame frame;
            std::string error_message;
        };

        // Callbacks for asynchronous operations.
        using ResponseCallback = std::function<void(const Response&)>;
        using ErrorCallback = std::function<void(const std::string&)>;

    } // namespace protocol
} // namespace vdp

#endif //VDPFRAMEPROCESSOR_TYPES_H
#endif //TYPES_H
