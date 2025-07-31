#define CATCH_CONFIG_MAIN
#include "catch2/catch_all.hpp"
#include "mobile_bridge_impl.h"
#include "protocol_engine.h"
#include "transport_interface.h"
#include <chrono>
#include <thread>

using namespace carly::protocol;
using namespace vdp::transport;
using namespace vdp::protocol;

// Test fixture for protocol engine tests
class ProtocolEngineTestFixture {
protected:
    std::unique_ptr<MobileBridgeImpl> bridge;
    
    void SetUp() {
        // Use mock transport for testing
        bridge = std::make_unique<MobileBridgeImpl>(TransportFactory::Type::MOCK);
        REQUIRE(bridge->initialize("mock://test"));
    }
    
    void TearDown() {
        if (bridge) {
            bridge->disconnect();
        }
    }
    
    Frame createTestFrame(uint8_t ecu_id, uint8_t command, const std::vector<uint8_t>& data = {}) {
        Frame frame(ecu_id, command);
        frame.data = data;
        return frame;
    }
};

TEST_CASE_METHOD(ProtocolEngineTestFixture, "Mobile Bridge Initialization") {
    SetUp();
    
    SECTION("Successful initialization") {
        REQUIRE(bridge->isConnected());
        REQUIRE(bridge->getLastError().empty());
    }
    
    SECTION("Initialization with invalid parameters") {
        auto test_bridge = std::make_unique<MobileBridgeImpl>(TransportFactory::Type::MOCK);
        REQUIRE_FALSE(test_bridge->initialize(""));
        REQUIRE_FALSE(test_bridge->isConnected());
    }
    
    TearDown();
}

TEST_CASE_METHOD(ProtocolEngineTestFixture, "Synchronous Frame Sending") {
    SetUp();
    
    SECTION("Send valid frame") {
        auto frame = createTestFrame(0x01, 0x10, {0x12, 0x34});
        auto response = bridge->sendFrame(frame, 1000);
        
        // Mock transport should return success
        REQUIRE(response.isSuccess());
        REQUIRE(response.frame.ecu_id == (0x01 | 0x80)); // Response ECU ID
    }
    
    SECTION("Send frame with timeout") {
        auto frame = createTestFrame(0x01, 0x10);
        auto response = bridge->sendFrame(frame, 10); // Very short timeout
        
        // Should timeout if no response configured
        REQUIRE(response.status == Status::TIMEOUT);
    }
    
    TearDown();
}

TEST_CASE_METHOD(ProtocolEngineTestFixture, "Asynchronous Frame Sending") {
    SetUp();
    
    SECTION("Async send with success callback") {
        auto frame = createTestFrame(0x02, 0x20, {0xAA, 0xBB});
        
        bool callback_called = false;
        Status received_status = Status::GENERAL_ERROR;
        
        bridge->sendFrameAsync(frame,
            [&](const Response& response) {
                callback_called = true;
                received_status = response.status;
            },
            [&](const std::string& error) {
                FAIL("Error callback should not be called: " + error);
            }
        );
        
        // Give some time for async processing
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        REQUIRE(callback_called);
        REQUIRE(received_status == Status::SUCCESS);
    }
    
    SECTION("Async send with error callback") {
        // Disconnect to force error
        bridge->disconnect();
        
        auto frame = createTestFrame(0x03, 0x30);
        
        bool error_callback_called = false;
        std::string error_message;
        
        bridge->sendFrameAsync(frame,
            [&](const Response& response) {
                FAIL("Success callback should not be called");
            },
            [&](const std::string& error) {
                error_callback_called = true;
                error_message = error;
            }
        );
        
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        
        REQUIRE(error_callback_called);
        REQUIRE_FALSE(error_message.empty());
    }
    
    TearDown();
}

TEST_CASE_METHOD(ProtocolEngineTestFixture, "Raw Data Interface") {
    SetUp();
    
    SECTION("Send raw data") {
        std::vector<uint8_t> raw_data = {0x7E, 0x07, 0x01, 0x10, 0x12, 0x34, 0x7F};
        auto response_data = bridge->sendRawData(raw_data);
        
        // Mock should echo or return appropriate response
        REQUIRE_FALSE(response_data.empty());
    }
    
    SECTION("Send invalid raw data") {
        std::vector<uint8_t> invalid_data = {0xFF, 0xFF}; // Invalid frame
        auto response_data = bridge->sendRawData(invalid_data);
        
        // Should handle gracefully
        // Response depends on implementation - could be empty or error frame
    }
    
    TearDown();
}

TEST_CASE("Mock Transport Functionality") {
    MockTransport transport;
    
    SECTION("Basic connection") {
        REQUIRE(transport.initialize("mock://test"));
        REQUIRE(transport.isConnected());
        REQUIRE(transport.getLastError().empty());
    }
    
    SECTION("Data sending and receiving") {
        REQUIRE(transport.initialize("mock://test"));
        
        std::vector<uint8_t> received_data;
        transport.setDataCallback([&](const uint8_t* data, size_t length) {
            received_data.assign(data, data + length);
        });
        
        // Send data
        std::vector<uint8_t> test_data = {0x01, 0x02, 0x03};
        REQUIRE(transport.send(test_data.data(), test_data.size()));
        
        // Verify data was captured
        auto sent_data = transport.getLastSentData();
        REQUIRE(sent_data == test_data);
        
        // Simulate incoming data
        std::vector<uint8_t> incoming = {0x04, 0x05, 0x06};
        transport.simulateIncomingData(incoming);
        
        REQUIRE(received_data == incoming);
    }
    
    SECTION("Error simulation") {
        REQUIRE(transport.initialize("mock://test"));
        
        std::string received_error;
        transport.setErrorCallback([&](const std::string& error) {
            received_error = error;
        });
        
        transport.simulateError("Test error");
        REQUIRE(received_error == "Test error");
    }
    
    SECTION("Auto-response feature") {
        REQUIRE(transport.initialize("mock://test"));
        
        std::vector<uint8_t> received_data;
        transport.setDataCallback([&](const uint8_t* data, size_t length) {
            received_data.assign(data, data + length);
        });
        
        // Set up auto-response
        std::vector<uint8_t> auto_response = {0x7E, 0x07, 0x81, 0x10, 0x00, 0x12, 0x7F};
        transport.setAutoResponse(true, auto_response);
        
        // Send request - using valid 7-byte frame
        // Frame format: [7E][07][ECU][CMD][STATUS][CHK][7F] (7 bytes total, length=7)
        std::vector<uint8_t> request = {0x7E, 0x07, 0x01, 0x10, 0x01, 0x17, 0x7F};
        REQUIRE(transport.send(request.data(), request.size()));
        
        // Should automatically receive response
        REQUIRE(received_data == auto_response);
    }
}

TEST_CASE("C Interface Compatibility") {
    SECTION("Factory functions") {
        auto* engine = createProtocolEngine();
        REQUIRE(engine != nullptr);
        
        // Test basic functionality
        REQUIRE(engine->initialize("mock://test"));
        REQUIRE(engine->isConnected());
        
        // Cleanup
        destroyProtocolEngine(engine);
    }
    
    SECTION("Transport type selection") {
        auto* engine = createProtocolEngineWithTransport(0); // MOCK transport
        REQUIRE(engine != nullptr);
        
        REQUIRE(engine->initialize("mock://test"));
        REQUIRE(engine->isConnected());
        
        destroyProtocolEngine(engine);
    }
}

TEST_CASE("Thread Safety") {
    auto bridge = std::make_unique<MobileBridgeImpl>(TransportFactory::Type::MOCK);
    REQUIRE(bridge->initialize("mock://test"));
    
    SECTION("Concurrent frame sending") {
        const int num_threads = 5;
        const int frames_per_thread = 10;
        std::vector<std::thread> threads;
        std::atomic<int> success_count{0};
        std::atomic<int> error_count{0};
        
        for (int t = 0; t < num_threads; ++t) {
            threads.emplace_back([&, t]() {
                for (int i = 0; i < frames_per_thread; ++i) {
                    auto frame = Frame(0x01 + t, 0x10 + i);
                    frame.data = {static_cast<uint8_t>(t), static_cast<uint8_t>(i)};
                    
                    bridge->sendFrameAsync(frame,
                        [&](const Response& response) {
                            if (response.isSuccess()) {
                                success_count++;
                            } else {
                                error_count++;
                            }
                        },
                        [&](const std::string& error) {
                            error_count++;
                        }
                    );
                }
            });
        }
        
        // Wait for all threads
        for (auto& thread : threads) {
            thread.join();
        }
        
        // Give time for async callbacks
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // Verify all operations completed
        REQUIRE(success_count + error_count == num_threads * frames_per_thread);
    }
}

TEST_CASE("Error Handling and Recovery") {
    auto bridge = std::make_unique<MobileBridgeImpl>(TransportFactory::Type::MOCK);
    
    SECTION("Recovery after disconnect") {
        REQUIRE(bridge->initialize("mock://test"));
        REQUIRE(bridge->isConnected());
        
        // Disconnect
        bridge->disconnect();
        REQUIRE_FALSE(bridge->isConnected());
        
        // Reconnect
        REQUIRE(bridge->initialize("mock://test"));
        REQUIRE(bridge->isConnected());
        
        // Should work normally after reconnection
        auto frame = Frame(0x01, 0x10);
        auto response = bridge->sendFrame(frame, 1000);
        REQUIRE(response.isSuccess());
    }
    
    SECTION("Error propagation") {
        // Don't initialize - should fail
        auto frame = Frame(0x01, 0x10);
        auto response = bridge->sendFrame(frame, 1000);
        
        REQUIRE_FALSE(response.isSuccess());
        REQUIRE_FALSE(bridge->getLastError().empty());
    }
}
