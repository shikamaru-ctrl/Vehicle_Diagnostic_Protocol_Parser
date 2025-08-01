#include "vdp_parser.h"

#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <cctype>
#include <iomanip>

std::vector<uint8_t> hexLineToBytes(const std::string& line_in) {
    std::string line = line_in;
    auto comment_pos = line.find('#');
    if (comment_pos != std::string::npos)
        line.resize(comment_pos);

    std::string hex_digits;
    for (char c : line) {
        if (std::isxdigit(static_cast<unsigned char>(c)))
            hex_digits.push_back(c);
    }

    if (hex_digits.size() < 2) return {};

    if (hex_digits.size() % 2 != 0)
        hex_digits.pop_back();  // drop stray nibble

    std::vector<uint8_t> bytes;
    bytes.reserve(hex_digits.size() / 2);
    for (size_t i = 0; i + 1 < hex_digits.size(); i += 2) {
        auto hexVal = [](char c) {
            return std::isdigit(c) ? c - '0' : std::toupper(c) - 'A' + 10;
        };
        uint8_t hi = hexVal(hex_digits[i]);
        uint8_t lo = hexVal(hex_digits[i + 1]);
        bytes.push_back((hi << 4) | lo);
    }
    return bytes;
}

int main(int argc, char* argv[]) {
    std::string path = (argc >= 2) ? argv[1] : "sample_frames.hex";
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Failed to open file: " << path << "\n";
        return 1;
    }

    vdp::VdpParser parser;
    std::string line;

    while (std::getline(in, line)) {
        auto bytes = hexLineToBytes(line);
        if (bytes.empty()) {
            continue;
        }

        parser.feed(bytes.data(), bytes.size());
        auto results = parser.extractFrames();

        if (!results.empty()) {
            for (const auto& r : results) {
                std::cout << "Raw bytes: ";
                for (auto b : r.raw_bytes) {
                    std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0')
                              << (int)b << " ";
                }
                std::cout << std::dec << std::endl;

                std::cout << "Status: ";
                switch (r.status) {
                case vdp::ParseStatus::Success: {
                    std::cout << "Valid frame" << std::endl;
                    break;
                }
                case vdp::ParseStatus::Invalid: {
                    std::cout << "ERROR. Reason: " << r.error << std::endl;
                    break;
                }
                default: {
                    // Other statuses like Incomplete are not expected here based on current parser logic
                    break;
                }
                }
                std::cout << std::endl; // Add a blank line for readability
            }
        }
    }

    return 0;
}
