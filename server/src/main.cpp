#include <iostream>
#include <asio.hpp>
#include <string>
#include <signal.h>
#include <filesystem>
#include <nlohmann/json.hpp>

#include "minidrive/version.hpp"

using json = nlohmann::json;

void handle_client(asio::ip::tcp::socket &socket, const std::string& root_path);

void create_user_directory(const std::string& root_path, const std::string& username) {
    try {
        std::string user_folder = root_path + "/" + username;
        if (!std::filesystem::exists(user_folder)) {
            std::filesystem::create_directories(user_folder);
            std::cout << "User directory created at: " << user_folder << "\n";
        } else {
            std::cout << "User directory already exists at: " << user_folder << "\n";
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to create user directory: " + std::string(e.what()));
    }
}

void handle_client(asio::ip::tcp::socket &socket, const std::string& root_path) {
    try {
        std::cout << "Client connected: " << socket.remote_endpoint() << "\n";

        // Read the username from the client
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream input_stream(&buffer);
        std::string username;
        std::getline(input_stream, username);

        if (username.empty()) {
            std::cerr << "No username provided by client." << std::endl;
            return;
        }

        std::cout << "Username received: " << username << "\n";

        // Create a directory for the user if it doesn't exist
        create_user_directory(root_path, username);

        // Send a welcome message to the client
        std::string welcome_message = "Welcome, " + username + "!\n";
        asio::write(socket, asio::buffer(welcome_message));

        while (true) {
            // Read a message from the client
            asio::streambuf buffer;
            asio::read_until(socket, buffer, '\n');

            // Extract the message
            std::istream input_stream(&buffer);
            std::string message;
            std::getline(input_stream, message);

            if (message.empty()) {
                continue;
            }

            std::cout << "Received: " << message << "\n";

            try {
                // Parse the JSON message
                auto json_message = json::parse(message);

                // Extract the command and arguments
                std::string command = json_message.at("cmd").get<std::string>();
                auto args = json_message.value("args", json::object());

                std::cout << "Command: " << command << "\n";
                std::cout << "Arguments: " << args.dump() << "\n";

                // Handle the command (placeholder for actual implementation)
                std::string response = "Command received: " + command + "\n";
                asio::write(socket, asio::buffer(response));
            } catch (const json::exception& e) {
                std::cerr << "Invalid JSON received: " << e.what() << "\n";
                std::string error_message = "Error: Invalid JSON format\n";
                asio::write(socket, asio::buffer(error_message));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Client disconnected or error: " << e.what() << "\n";
    }
}

void run_server(const std::string& host, const std::string& port, std::string root_path) {
    try {
        asio::io_context io_context;

        asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::make_address(host), std::stoi(port)));
        std::cout << "Server is running on " << host << ":" << port << "\n";

        while (true) {
            asio::ip::tcp::socket socket(io_context);
            acceptor.accept(socket);

            std::cout << "New connection from " << socket.remote_endpoint() << "\n";

            handle_client(socket, root_path);
        }
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << "\n";
    }
}

void handle_sigint(int) {
    std::cout << "Server shutting down gracefully...\n";
    exit(0);
}

void parse_arguments(int argc, char* argv[], std::string& port, std::string& root_path) {
    if (argc != 5) {
        throw std::invalid_argument("Usage: ./server --port <PORT> --root <ROOT_PATH>");
    }

    for (int i = 1; i < argc; i += 2) {
        std::string arg = argv[i];
        if (arg == "--port" && i + 1 < argc) {
            port = argv[i + 1];
        } else if (arg == "--root" && i + 1 < argc) {
            root_path = argv[i + 1];
        } else {
            throw std::invalid_argument("Invalid arguments. Usage: ./server --port <PORT> --root <ROOT_PATH>");
        }
    }

    if (port.empty() || root_path.empty()) {
        throw std::invalid_argument("Both --port and --root arguments are required.");
    }
}

void create_root_directory(const std::string& root_path) {
    try {
        if (!std::filesystem::exists(root_path)) {
            std::filesystem::create_directories(root_path);
            std::cout << "Root directory created at: " << root_path << "\n";
        } else {
            std::cout << "Root directory already exists at: " << root_path << "\n";
        }

        // Create the "public" folder inside the root directory
        std::string public_folder = root_path + "/public";
        if (!std::filesystem::exists(public_folder)) {
            std::filesystem::create_directories(public_folder);
            std::cout << "Public folder created at: " << public_folder << "\n";
        } else {
            std::cout << "Public folder already exists at: " << public_folder << "\n";
        }
    } catch (const std::exception& e) {
        throw std::runtime_error("Failed to create root or public directory: " + std::string(e.what()));
    }
}

int main(int argc, char* argv[]) {
    try {
        std::string port;
        std::string root_path;

        // Parse command-line arguments
        parse_arguments(argc, argv, port, root_path);

        // Create the root directory
        create_root_directory(root_path);

        std::cout << "Server starting on port: " << port << " with root path: " << root_path << "\n";

        run_server("0.0.0.0", port, root_path);

    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}
