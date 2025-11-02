#include <iostream>
#include <regex>
#include <string>
#include "minidrive/version.hpp"

bool parse_arguments(int argc, char* argv[], std::string& connection, std::string& log_file) {
    if (argc < 2 || argc > 4) {
        std::cerr << "Usage: " << argv[0] << " [username@]<server_ip>:<port> [--log <log_file>]\n";
        return false;
    }

    connection = argv[1];

    if (argc == 4) {
        if (std::string(argv[2]) != "--log") {
            std::cerr << "Invalid option: " << argv[2] << "\n";
            std::cerr << "Usage: " << argv[0] << " [username@]<server_ip>:<port> [--log <log_file>]\n";
            return false;
        }
        log_file = argv[3];
    }

    return true;
}

int main(int argc, char* argv[]) {
    std::string connection;
    std::string log_file;

    if (!parse_arguments(argc, argv, connection, log_file)) {
        std::cerr << "Error: Invalid arguments.\n";
        return 1;
    }

    std::cout << "Connection: " << connection << "\n";
    if (!log_file.empty()) {
        std::cout << "Log file: " << log_file << "\n";
    }

    std::cout << "MiniDrive client stub (version " << minidrive::version() << ")" << std::endl;
    std::cout << "Interactive shell and networking are not yet implemented." << std::endl;
    return 0;
}