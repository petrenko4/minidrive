#include <iostream>
#include <regex>
#include <string>
#include "minidrive/version.hpp"
#include <asio.hpp>

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

bool parse_connection_string(const std::string& connection, std::string& ip, std::string& port) {
    std::regex pattern(R"((?:[^@]+@)?([^:]+):(\d+))");
    std::smatch matches;

    if (std::regex_match(connection, matches, pattern)) {
        ip = matches[1].str();
        port = matches[2].str();
        return true;
    }

    return false;
}

void interactive_shell(asio::ip::tcp::socket& socket) {
    std::cout << "Enter messages to send to the server. Type 'exit' to quit.\n";

    while (true) {
        try {
            std::string message;
            std::cout << "> ";
            std::getline(std::cin, message);

            if (message == "exit") {
                std::cout << "Exiting interactive shell.\n";
                break;
            }

            // Send the message to the server
            asio::write(socket, asio::buffer(message + "\n"));

            // Wait for the server's response
            asio::streambuf response;
            asio::read_until(socket, response, '\n');

            // Print the server's response
            std::istream response_stream(&response);
            std::string response_message;
            std::getline(response_stream, response_message);
            std::cout << "Server: " << response_message << "\n";
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            break;
        }
    }
}

void attempt_connection(const std::string& ip, const std::string& port) {
    try {
        asio::io_context io_context;
        asio::ip::tcp::resolver resolver(io_context);
        asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(ip, port);

        asio::ip::tcp::socket socket(io_context);
        asio::connect(socket, endpoints);

        std::cout << "Successfully connected to " << ip << ":" << port << "\n";

        // Start the interactive shell
        interactive_shell(socket);
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect: " << e.what() << "\n";
    }
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

    std::string ip, port;
    if (!parse_connection_string(connection, ip, port)) {
        std::cerr << "Error: Invalid connection string format. Expected: [username@]<server_ip>:<port>\n";
        return 1;
    }

    attempt_connection(ip, port);

    std::cout << "MiniDrive client stub (version " << minidrive::version() << ")" << std::endl;
    std::cout << "Interactive shell and networking are not yet implemented." << std::endl;
    return 0;
}