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
    size_t line_num = 1;

    while (std::getline(in, line)) {
        // Skip empty lines and comments
        if (line.empty() || line[0] == '#') {
            ++line_num;
            continue;
        }

        // Print the input line being processed
        std::cout << "\nLine " << line_num << ": " << line << std::endl;
        
        // Convert hex string to bytes
        auto bytes = hexLineToBytes(line);
        if (bytes.empty()) {
            std::cout << "  => No valid hex data found\n";
            ++line_num;
            continue;
        }

        // Print the raw bytes being processed
        std::cout << "  Raw bytes: ";
        for (auto b : bytes) {
            std::cout << std::hex << std::uppercase << std::setw(2) << std::setfill('0') 
                     << (int)b << " ";
        }
        std::cout << std::dec << std::endl;

        // Feed the bytes to the parser
        parser.feed(bytes.data(), bytes.size());
        auto results = parser.extractFrames();

        // Process and display results
        if (results.empty()) {
            std::cout << "  => No complete frames found (buffer may be waiting for more data)\n";
        } else {
            for (size_t i = 0; i < results.size(); ++i) {
                const auto& r = results[i];
                std::cout << "  Frame " << (i+1) << ": ";
                
                switch (r.status) {
                    case vdp::ParseStatus::Success: {
                        const auto& f = *r.frame;
                        std::cout << "SUCCESS  ";
                        std::cout << "ECU=0x" << std::hex << (int)f.ecu_id << " ";
                        std::cout << "CMD=0x" << (int)f.command << " ";
                        if (!f.data.empty()) {
                            std::cout << "DATA=[";
                            for (size_t j = 0; j < f.data.size(); ++j) {
                                std::cout << std::hex << std::setw(2) << std::setfill('0') 
                                         << (int)f.data[j];
                                if (j < f.data.size() - 1) std::cout << " ";
                            }
                            std::cout << "] ";
                        }
                        std::cout << std::dec;
                        break;
                    }
                    case vdp::ParseStatus::Incomplete:
                        std::cout << "INCOMPLETE: " << r.error;
                        break;
                    case vdp::ParseStatus::Invalid:
                        std::cout << "INVALID: " << r.error;
                        break;
                }
                std::cout << std::endl;
            }
        }
        
        ++line_num;
    }

    return 0;
}
