//
// VDP Frame Parser Tests
// Frame format: [0x7E][LEN][ECU_ID][CMD][DATA...][CHECKSUM][0x7F]
//
#define CATCH_CONFIG_MAIN
#include "catch2/catch_all.hpp"
#include "vdp_parser.h"
#include <chrono>
#include <iomanip>
#include <iostream>
#include <thread>

using namespace std;
using namespace vdp;

static vector<ParseResult> feedAll(VdpParser& p, const vector<uint8_t>& bytes) {
    p.feed(bytes.data(), bytes.size());
    return p.extractFrames();
}

static vector<uint8_t> makeFrame(uint8_t ecu_id, uint8_t cmd, const vector<uint8_t>& data = {}) {
    // 1. Assemble the frame with placeholders for length and checksum.
    vector<uint8_t> frame = {0x7E, 0x00, ecu_id, cmd};
    frame.insert(frame.end(), data.begin(), data.end());
    frame.push_back(0x00); // Checksum placeholder
    frame.push_back(0x7F); // End byte

    // 2. Set the correct length byte, which is the total size of the frame.
    frame[1] = static_cast<uint8_t>(frame.size());

    // 3. Calculate the checksum over the correct range (from length byte to pre-checksum byte).
    uint8_t checksum = 0;
    for (size_t i = 1; i < frame.size() - 2; ++i) {
        checksum ^= frame[i];
    }

    // 4. Set the final checksum.
    frame[frame.size() - 2] = checksum;

    // Debug output
    std::cout << "Constructed frame: ";
    for (auto b : frame) {
        std::cout << std::hex << std::setw(2) << std::setfill('0') << (int)b << " ";
    }
    std::cout << std::dec << std::endl;

    return frame;
}

// Basic frame parsing tests
TEST_CASE("Basic frame parsing") {
    VdpParser p;

    // Test minimal valid frame (6 bytes)
    auto frame1 = makeFrame(0x81, 0x10, {}); // No data, just ECU and CMD
    REQUIRE(frame1.size() == 6);
    auto res1 = feedAll(p, frame1);
    REQUIRE(res1.size() == 1);
    REQUIRE(res1[0].status == ParseStatus::Success);
    REQUIRE(res1[0].frame->ecu_id == 0x81);
    REQUIRE(res1[0].frame->command == 0x10);
    REQUIRE(res1[0].frame->data.empty());

    // Test frame with data
    auto frame2 = makeFrame(0x82, 0x20, {0x12, 0x34, 0x56});
    auto res2 = feedAll(p, frame2);
    REQUIRE(res2.size() == 1);
    REQUIRE(res2[0].status == ParseStatus::Success);
    REQUIRE(res2[0].frame->data == vector<uint8_t>{0x12, 0x34, 0x56});
}

// Special byte handling tests
TEST_CASE("Special bytes in payload") {
    VdpParser p;
    // Test with 0x7E and 0x7F in data. The parser should handle this correctly.
    auto frame = makeFrame(0x83, 0x30, {0x7E, 0x7F, 0x01});
    auto res = feedAll(p, frame);
    REQUIRE(res.size() == 1);
    REQUIRE(res[0].status == ParseStatus::Success);
    REQUIRE(res[0].frame->data == vector<uint8_t>{0x7E, 0x7F, 0x01});
}

// Error handling tests
TEST_CASE("Error handling") {
    // Test 1: Invalid checksum
    {
        VdpParser p;
        // Manually construct a frame that is valid in every way except the checksum.
        // We use a valid command (0x10) to ensure we pass command validation.
        vector<uint8_t> frame = {0x7E, 0x08, 0x84, 0x10, 0x11, 0x22, 0x00, 0x7F};
        // The correct checksum for the content {0x08, 0x84, 0x10, 0x11, 0x22} is 0xAF.
        // We are intentionally providing 0x00 to trigger the failure.
        auto results = feedAll(p, frame);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status == ParseStatus::Invalid);
        REQUIRE(results[0].error.find("Checksum verification failed") != string::npos);
    }

    // Test 2: Incomplete frame - should produce no results
    {
        VdpParser p;
        auto frame = makeFrame(0x84, 0x10, {0x11, 0x22});
        // Feed all but the last byte
        auto results = feedAll(p, {frame.begin(), frame.end() - 1});
        REQUIRE(results.empty()); // No frame should be parsed
    }

    // Test 3: Invalid start byte (garbage before frame)
    {
        VdpParser p;
        vector<uint8_t> garbage = {0x01, 0x02, 0x03};
        auto frame = makeFrame(0x84, 0x10, {});
        garbage.insert(garbage.end(), frame.begin(), frame.end());
        auto results = feedAll(p, garbage);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status == ParseStatus::Success);
    }

    // Test 4: Invalid end byte
    {
        VdpParser p;
        auto frame = makeFrame(0x84, 0x10, {0x11, 0x22});
        frame.back() = 0x7D; // Corrupt end byte
        auto results = feedAll(p, frame);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status == ParseStatus::Invalid);
        REQUIRE(results[0].error.find("End marker not found") != string::npos);
    }

    // Test 5: Invalid length (too short)
    {
        VdpParser p;
        vector<uint8_t> frame = {0x7E, 0x05, 0x01, 0x10, 0x14, 0x7F}; // Length 5 is invalid.
        auto results = feedAll(p, frame);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status == ParseStatus::Invalid);
        REQUIRE(results[0].error.find("Invalid frame length") != string::npos);
    }
}

// Test resynchronization after various kinds of garbage data
TEST_CASE("Resynchronization after garbage") {
    VdpParser p;
    auto valid_frame = makeFrame(0x01, 0x30, {0x31});

    // Test 1: Simple garbage bytes before a valid frame
    {
        p.reset();
        vector<uint8_t> input = {0xDE, 0xAD, 0xBE, 0xEF};
        input.insert(input.end(), valid_frame.begin(), valid_frame.end());
        auto results = feedAll(p, input);
        REQUIRE(results.size() == 1);
        REQUIRE(results[0].status == ParseStatus::Success);
        REQUIRE(results[0].frame->ecu_id == 0x01);
    }

    // Test 2: A malformed frame (bad length) followed by a valid frame
    {
        p.reset();
        vector<uint8_t> malformed = {0x7E, 0x03, 0x01, 0x02, 0x03, 0x7F}; // Invalid length 3
        vector<uint8_t> input = malformed;
        input.insert(input.end(), valid_frame.begin(), valid_frame.end());
        auto results = feedAll(p, input);
        REQUIRE(results.size() == 2);
        REQUIRE(results[0].status == ParseStatus::Invalid);
        REQUIRE(results[0].error == "Invalid frame length: 3");
        REQUIRE(results[1].status == ParseStatus::Success);
        REQUIRE(results[1].frame->ecu_id == 0x01);
    }
    
    // Test 3: A malformed frame (bad checksum) followed by a valid frame
    {
        p.reset();
        auto malformed = makeFrame(0xFF, 0xFF, {});
        malformed[malformed.size() - 2] ^= 0xFF; // Corrupt checksum
        vector<uint8_t> input = malformed;
        input.insert(input.end(), valid_frame.begin(), valid_frame.end());
        auto results = feedAll(p, input);
        REQUIRE(results.size() == 2);
        REQUIRE(results[0].status == ParseStatus::Invalid);
        REQUIRE(results[0].error.find("Checksum verification failed") != string::npos);
        REQUIRE(results[1].status == ParseStatus::Success);
        REQUIRE(results[1].frame->ecu_id == 0x01);
    }
}

TEST_CASE("Back-to-back frames parse correctly") {
    VdpParser p;
    auto frame1 = makeFrame(0x01, 0x30, {0x31});
    auto frame2 = makeFrame(0x02, 0x40, {0x41});

    vector<uint8_t> input;
    input.insert(input.end(), frame1.begin(), frame1.end());
    input.insert(input.end(), frame2.begin(), frame2.end());

    auto res = feedAll(p, input);

    REQUIRE(res.size() == 2);
    REQUIRE(res[0].status == ParseStatus::Success);
    REQUIRE(res[1].status == ParseStatus::Success);
    REQUIRE(res[0].frame->ecu_id == 0x01);
    REQUIRE(res[1].frame->ecu_id == 0x02);
}

TEST_CASE("Partial frame handling (streaming)") {
    VdpParser p;
    auto frame = makeFrame(0x81, 0x10, {0x12, 0x34, 0x56});

    // Feed frame one byte at a time
    for (size_t i = 0; i < frame.size() - 1; ++i) {
        auto res = feedAll(p, {frame[i]});
        REQUIRE(res.empty()); // Should not produce any result until frame is complete
    }

    // Feed the last byte to complete the frame
    auto final_res = feedAll(p, {frame.back()});
    REQUIRE(final_res.size() == 1);
    REQUIRE(final_res[0].status == ParseStatus::Success);
    REQUIRE(final_res[0].frame->ecu_id == 0x81);
}

TEST_CASE("Partial frame across buffer boundaries") {
    VdpParser p;
    auto frame = makeFrame(0x82, 0x25, {0xAA, 0xBB, 0xCC, 0xDD, 0xEE});

    // Test 1: Split at different positions
    for (size_t split_pos = 1; split_pos < frame.size(); ++split_pos) {
        p.reset();

        // Feed first part
        vector<uint8_t> part1(frame.begin(), frame.begin() + split_pos);
        auto res1 = feedAll(p, part1);
        REQUIRE(res1.empty());

        // Feed second part
        vector<uint8_t> part2(frame.begin() + split_pos, frame.end());
        auto res2 = feedAll(p, part2);
        REQUIRE(res2.size() == 1);
        REQUIRE(res2[0].status == ParseStatus::Success);
        REQUIRE(res2[0].frame->ecu_id == 0x82);
    }
}

TEST_CASE("Frame with incorrect end marker") {
    VdpParser p;
    auto frame = makeFrame(0x01, 0x10, {});
    frame.back() = 0x7D; // Incorrect end marker

    auto valid_frame = makeFrame(0x02, 0x20, {});
    
    vector<uint8_t> input = frame;
    input.insert(input.end(), valid_frame.begin(), valid_frame.end());

    auto results = feedAll(p, input);
    REQUIRE(results.size() == 2);
    REQUIRE(results[0].status == ParseStatus::Invalid);
    REQUIRE(results[0].error.find("End marker not found") != string::npos);
    REQUIRE(results[1].status == ParseStatus::Success);
    REQUIRE(results[1].frame->ecu_id == 0x02);
}

// New test cases for improved robustness

TEST_CASE("extractFrames is idempotent") {
    VdpParser p;
    auto frame = makeFrame(0x99, 0x99, {0x01, 0x02});

    // Feed the frame and extract it
    auto res1 = feedAll(p, frame);
    REQUIRE(res1.size() == 1);
    REQUIRE(res1[0].status == ParseStatus::Success);

    // Calling extract again with no new data should yield nothing
    auto res2 = p.extractFrames();
    REQUIRE(res2.empty());
}

TEST_CASE("Explicit KeepAlive frame") {
    VdpParser p;
    // A standard KeepAlive frame is typically ECU 0, CMD 0, no data.
    // Frame: 7E LEN ECU CMD ... CHK 7F
    // Content for checksum: LEN, ECU, CMD -> 0x06, 0x00, 0x00
    // Checksum: 0x06 ^ 0x00 ^ 0x00 = 0x06
    vector<uint8_t> keep_alive = {0x7E, 0x06, 0x00, 0x00, 0x06, 0x7F};
    auto res = feedAll(p, keep_alive);
    REQUIRE(res.size() == 1);
    REQUIRE(res[0].status == ParseStatus::Success);
    REQUIRE(res[0].frame->ecu_id == 0x00);
    REQUIRE(res[0].frame->command == 0x00);
    REQUIRE(res[0].frame->data.empty());
}

TEST_CASE("Maximum size frame") {
    VdpParser p;
    // Max frame size is 253. Header/footer is 6 bytes, so max payload is 247.
    vector<uint8_t> big_payload(247, 0xAB);
    auto frame = makeFrame(0xFF, 0x01, big_payload); // 0xFF is ECU ID
    REQUIRE(frame.size() == 253);

    auto res = feedAll(p, frame);
    REQUIRE(res.size() == 1);
    REQUIRE(res[0].status == ParseStatus::Success);
    REQUIRE(res[0].frame->data.size() == 247);
    REQUIRE(res[0].frame->data == big_payload);
}

TEST_CASE("Truncated frame") {
    VdpParser p;
    auto frame = makeFrame(0x01, 0x10, {0x12, 0x34, 0x56});
    frame.pop_back(); // Remove the last byte (end marker)
    auto res = feedAll(p, frame);
    REQUIRE(res.empty()); // Should not produce any result
}

TEST_CASE("Thread safety with concurrent feed and extract") {
    VdpParser parser;
    std::vector<VdpFrame> produced_frames;
    std::vector<VdpFrame> consumed_frames;
    const int num_frames_to_test = 50;

    // Create some frames to test with
    for (int i = 0; i < num_frames_to_test; ++i) {
        produced_frames.push_back({(uint8_t)i, (uint8_t)(i+1), {(uint8_t)(i+2), (uint8_t)(i+3)}});
    }

    std::thread producer([&]() {
        for (const auto& f : produced_frames) {
            auto frame_bytes = makeFrame(f.ecu_id, f.command, f.data);
            parser.feed(frame_bytes.data(), frame_bytes.size());
            // Small delay to allow consumer to run
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
    });

    std::thread consumer([&]() {
        int frames_found = 0;
        while (frames_found < num_frames_to_test) {
            auto results = parser.extractFrames();
            for (const auto& res : results) {
                if (res.status == ParseStatus::Success) {
                    consumed_frames.push_back(*res.frame);
                    frames_found++;
                }
            }
            // Small delay to allow producer to run
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    });

    producer.join();
    consumer.join();

    REQUIRE(consumed_frames.size() == num_frames_to_test);

    // Verify data integrity
    for (int i = 0; i < num_frames_to_test; ++i) {
        REQUIRE(consumed_frames[i].ecu_id == produced_frames[i].ecu_id);
        REQUIRE(consumed_frames[i].command == produced_frames[i].command);
        REQUIRE(consumed_frames[i].data == produced_frames[i].data);
    }
}
