#include <iostream>
#include <asio.hpp>
#include <string>
#include <signal.h>
#include <filesystem>
#include <fstream>
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

bool send_response(asio::ip::tcp::socket& socket, const std::string& status, const std::string& message = "", int code = 0, const json& data = json::object()) {
    try {
        json response;
        response["status"] = status;
        response["code"] = code;
        response["message"] = message;
        response["data"] = data;
        asio::write(socket, asio::buffer(response.dump() + "\n"));
        return true;
    } catch (const std::exception& e) {
        std::cerr << "Failed to send response: " << e.what() << "\n";
        return false;
    }
}

void log_debug(const std::string& message) {
    std::cout << "[DEBUG] " << message << "\n";
}

void handle_upload(const std::string& username, const std::string& root_path, asio::ip::tcp::socket& socket, const json& args) {
    try {
        log_debug("Handling UPLOAD command");

        // Extract file paths from the arguments
        std::string filename = args.at("filename").get<std::string>();
        std::string user_directory = root_path + "/" + username;
        std::string file_path = user_directory + "/" + filename;

        // Check if the file already exists
        if (std::filesystem::exists(file_path)) {
            send_response(socket, "error", "File already exists: " + file_path);
            log_debug("File already exists: " + file_path);
            return;
        }

        log_debug("Preparing to receive file: " + filename);

        // Check if the server is ready to receive the file
        if (!send_response(socket, "ready", "Server is ready to receive the file.")) {
            log_debug("Failed to send ready response to client.");
            return;
        }

        // Open the file for writing
        std::ofstream output_file(file_path, std::ios::binary);
        if (!output_file.is_open()) {
            throw std::runtime_error("Failed to open file for writing: " + file_path);
        }

        log_debug("File opened for writing: " + file_path);

        // Receive the file size
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream input_stream(&buffer);
        std::string file_size_str;
        std::getline(input_stream, file_size_str);
        size_t file_size = std::stoull(file_size_str);

        log_debug("Expecting file size: " + std::to_string(file_size));

        // Receive the file data
        size_t bytes_received = 0;
        while (bytes_received < file_size) {
            char data[1024];
            size_t len = socket.read_some(asio::buffer(data, std::min(file_size - bytes_received, sizeof(data))));
            output_file.write(data, len);
            bytes_received += len;

            log_debug("Received " + std::to_string(bytes_received) + " of " + std::to_string(file_size) + " bytes.");
        }

        output_file.close();
        log_debug("File received and saved to: " + file_path);

        // Send acknowledgment to the client
        send_response(socket, "success", "File uploaded successfully.");
        log_debug("Acknowledgment sent to client.");
    } catch (const std::exception& e) {
        std::cerr << "Error handling upload: " << e.what() << "\n";
        send_response(socket, "error", e.what());
        log_debug("Error during upload: " + std::string(e.what()));
    }
}

void handle_list(const std::string& username, const std::string& root_path, asio::ip::tcp::socket& socket, const json& args) {
    try {
        log_debug("Handling LIST command");
        log_debug("Root path: " + root_path + ", Username: " + username);

        // Determine the directory to list by appending the client's path to the user's directory
        std::filesystem::path user_directory = std::filesystem::path(root_path) / username;
        std::string path_arg = args.value("path", ".");
        std::filesystem::path resolved_path = user_directory / path_arg;

        // Normalize the resolved path
        resolved_path = resolved_path.lexically_normal();
        std::string path = resolved_path.string();

        // Security check: ensure resolved path is under the user's directory
        if (resolved_path.string().rfind(user_directory.string(), 0) != 0) {
            send_response(socket, "error", "Access denied: path outside user directory.");
            log_debug("Access denied to path: " + path);
            return;
        }

        log_debug("Listing directory: " + path);

        // Check if the directory exists
        if (!std::filesystem::exists(path) || !std::filesystem::is_directory(path)) {
            send_response(socket, "error", "Directory does not exist: " + path);
            log_debug("Directory does not exist: " + path);
            return;
        }

        // Collect directory entries
        json data;
        data["entries"] = json::array();
        size_t total_entries = 0;

        for (const auto& entry : std::filesystem::directory_iterator(path)) {
            json file_info;
            file_info["name"] = entry.path().filename().string();
            file_info["type"] = entry.is_directory() ? "directory" : "file";
            data["entries"].push_back(file_info);
            ++total_entries;
        }

        // Add total entries count to the response
        data["total_entries"] = total_entries;

        // Send the response
        send_response(socket, "OK", "Directory listed successfully.", 0, data);
        log_debug("Directory listing sent successfully with " + std::to_string(total_entries) + " entries.");
    } catch (const std::exception& e) {
        std::cerr << "Error handling LIST command: " << e.what() << "\n";
        send_response(socket, "error", e.what());
        log_debug("Error during LIST command: " + std::string(e.what()));
    }
}

void handle_delete(const std::string& username, const std::string& root_path, asio::ip::tcp::socket& socket, const json& args) {
    try {
        log_debug("Handling DELETE command");

        // Determine the file to delete by appending the client's path to the user's directory
        std::filesystem::path user_directory = std::filesystem::path(root_path) / username;
        std::string file_arg = args.value("path", "");
        std::filesystem::path resolved_path = user_directory / file_arg;

        // Normalize the resolved path
        resolved_path = resolved_path.lexically_normal();
        std::string path = resolved_path.string();

        // Security check: ensure resolved path is under the user's directory
        if (resolved_path.string().rfind(user_directory.string(), 0) != 0) {
            send_response(socket, "error", "Access denied: path outside user directory.");
            log_debug("Access denied to path: " + path);
            return;
        }

        log_debug("Deleting file: " + path);

        // Check if the file exists
        if (!std::filesystem::exists(path)) {
            send_response(socket, "error", "File does not exist: " + path);
            log_debug("File does not exist: " + path);
            return;
        }

        // Attempt to delete the file
        if (std::filesystem::is_regular_file(path)) {
            std::filesystem::remove(path);
            send_response(socket, "OK", "File deleted successfully.");
            log_debug("File deleted successfully: " + path);
        } else {
            send_response(socket, "error", "Path is not a file: " + path);
            log_debug("Path is not a file: " + path);
        }
    } catch (const std::exception& e) {
        std::cerr << "Error handling DELETE command: " << e.what() << "\n";
        send_response(socket, "error", e.what());
        log_debug("Error during DELETE command: " + std::string(e.what()));
    }
}

void handle_download(const std::string& username, const std::string& root_path, asio::ip::tcp::socket& socket, const json& args) {
    try {
        log_debug("Handling DOWNLOAD command");

        // Determine the file to download by appending the client's path to the user's directory
        std::filesystem::path user_directory = std::filesystem::path(root_path) / username;
        std::string file_arg = args.value("remote_path", "");
        std::filesystem::path resolved_path = user_directory / file_arg;

        // Normalize the resolved path
        resolved_path = resolved_path.lexically_normal();
        std::string path = resolved_path.string();

        // Security check: ensure resolved path is under the user's directory
        if (resolved_path.string().rfind(user_directory.string(), 0) != 0) {
            send_response(socket, "error", "Access denied: path outside user directory.");
            log_debug("Access denied to path: " + path);
            return;
        }

        log_debug("Preparing to send file: " + path);

        // Check if the file exists
        if (!std::filesystem::exists(path)) {
            send_response(socket, "error", "File does not exist: " + path);
            log_debug("File does not exist: " + path);
            return;
        }

        // Check if the path is a regular file
        if (!std::filesystem::is_regular_file(path)) {
            send_response(socket, "error", "Path is not a file: " + path);
            log_debug("Path is not a file: " + path);
            return;
        }

        // Open the file for reading
        std::ifstream input_file(path, std::ios::binary);
        if (!input_file.is_open()) {
            send_response(socket, "error", "Failed to open file: " + path);
            log_debug("Failed to open file: " + path);
            return;
        }

        // Get the file size
        input_file.seekg(0, std::ios::end);
        size_t file_size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);

        log_debug("File size: " + std::to_string(file_size));

        // Send the file size to the client
        send_response(socket, "ready", "File is ready to be downloaded.", 0, { {"file_size", file_size} });

        // Send the file data
        char buffer[1024];
        size_t bytes_sent = 0;
        while (bytes_sent < file_size) {
            input_file.read(buffer, sizeof(buffer));
            std::streamsize bytes_read = input_file.gcount();
            asio::write(socket, asio::buffer(buffer, bytes_read));
            bytes_sent += bytes_read;

            log_debug("Sent " + std::to_string(bytes_sent) + " of " + std::to_string(file_size) + " bytes.");
        }

        input_file.close();
        log_debug("File sent successfully: " + path);
    } catch (const std::exception& e) {
        std::cerr << "Error handling DOWNLOAD command: " << e.what() << "\n";
        send_response(socket, "error", e.what());
        log_debug("Error during DOWNLOAD command: " + std::string(e.what()));
    }
}

void handle_command(const std::string& username, const std::string& root_path, asio::ip::tcp::socket& socket, const json& json_message) {
    try {
        // Extract the command and arguments
        std::string command = json_message.at("cmd").get<std::string>();
        auto args = json_message.value("args", json::object());

        std::cout << "Command: " << command << "\n";
        std::cout << "Arguments: " << args.dump() << "\n";

        if (command == "UPLOAD") {
            handle_upload(username, root_path, socket, args);
        } else if (command == "LIST") {
            handle_list(username, root_path, socket, args);
        } else if (command == "DELETE") {
            handle_delete(username, root_path, socket, args);
        } else if (command == "DOWNLOAD") {
            handle_download(username, root_path, socket, args);
        } else {
            // Placeholder for other commands
            send_response(socket, "success", "Command received: " + command);
        }
    } catch (const json::exception& e) {
        std::cerr << "Invalid JSON command: " << e.what() << "\n";
        send_response(socket, "error", "Invalid JSON command format.");
    } catch (const std::exception& e) {
        std::cerr << "Error handling command: " << e.what() << "\n";
        send_response(socket, "error", e.what());
    }
}

void handle_client(asio::ip::tcp::socket& socket, const std::string& root_path) {
    try {
        log_debug("New client connected: " + socket.remote_endpoint().address().to_string());

        // Read the username from the client
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream input_stream(&buffer);
        std::string username;
        std::getline(input_stream, username);

        if (username.empty()) {
            log_debug("No username provided by client.");
            return;
        }

        log_debug("Username received: " + username);

        // Create a directory for the user if it doesn't exist
        create_user_directory(root_path, username);

        // Send a welcome message to the client
        //send_response(socket, "success", "Welcome, " + username + "!");
        log_debug("Welcome message sent to: " + username);

        while (true) {
            // Read a message from the client
            asio::streambuf buffer;
            asio::read_until(socket, buffer, '\n');

            // Extract the message
            std::istream input_stream(&buffer);
            std::string message;
            std::getline(input_stream, message);

            if (message.empty()) {
                log_debug("Empty message received, continuing.");
                continue;
            }

            log_debug("Received message: " + message);

            try {
                // Parse the JSON message
                auto json_message = json::parse(message);

                // Handle the command
                handle_command(username, root_path, socket, json_message);
            } catch (const json::exception& e) {
                std::cerr << "Invalid JSON received: " << e.what() << "\n";
                send_response(socket, "error", "Invalid JSON format.");
                log_debug("Invalid JSON format: " + std::string(e.what()));
            }
        }
    } catch (const std::exception& e) {
        std::cerr << "Client disconnected or error: " << e.what() << "\n";
        log_debug("Client disconnected or error: " + std::string(e.what()));
    }
}

void run_server(const std::string& host, const std::string& port, std::string& root_path) {
    try {
        asio::io_context io_context;

        asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::make_address(host), std::stoi(port)));
        std::cout << "Server is running on " << host << ":" << port << "\n";

        while (true) {
            asio::ip::tcp::socket socket(io_context);
            acceptor.accept(socket);

            std::cout << "New connection from " << socket.remote_endpoint() << "\n";

            // Handle the client connection
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
