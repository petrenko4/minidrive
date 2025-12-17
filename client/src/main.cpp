#include <iostream>
#include <regex>
#include <string>
#include "minidrive/version.hpp"
#include <asio.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>

using json = nlohmann::json;

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

void print_available_commands() {
    std::cout << "Available commands:\n";
    std::cout << "  LIST [path]         - Lists files and folders in the given path. If no path is given, lists the current directory.\n";
    std::cout << "  UPLOAD <local_path> [remote_path] - Uploads a file from the client’s local file system to the server. If remote_path is omitted, the same name is used.\n";
    std::cout << "  DOWNLOAD <remote_path> [local_path] - Downloads a file from the server to the client. If local_path is omitted, the current directory with the filename from remote is used.\n";
    std::cout << "  DELETE <path>       - Deletes a file on the server.\n";
    std::cout << "  CD <path>           - Changes the current directory to the specified path.\n";
    std::cout << "  MKDIR <path>        - Creates a new folder on the server.\n";
    std::cout << "  RMDIR <path>        - Removes a folder on the server (recursive).\n";
    std::cout << "  MOVE <src> <dst>    - Moves or renames a file or folder on the server.\n";
    std::cout << "  COPY <src> <dst>    - Copies a file or folder on the server.\n";
    std::cout << "  HELP                - Prints a list of available commands.\n";
    std::cout << "  EXIT                - Closes the connection and terminates the client.\n";
}

bool validate_command(const std::string& input) {
    std::istringstream iss(input);
    std::string command;
    iss >> command;

    if (command == "LIST") {
        // LIST can optionally have one argument
        std::string path;
        if (iss >> path) {
            return true;
        }
        return true; // No argument is also valid
    } else if (command == "UPLOAD") {
        // UPLOAD requires at least one argument (local_path)
        std::string local_path;
        if (iss >> local_path) {
            return true;
        }
        return false;
    } else if (command == "DOWNLOAD") {
        // DOWNLOAD requires at least one argument (remote_path)
        std::string remote_path;
        if (iss >> remote_path) {
            return true;
        }
        return false;
    } else if (command == "DELETE") {
        // DELETE requires exactly one argument (path)
        std::string path;
        if (iss >> path) {
            return true;
        }
        return false;
    } else if (command == "CD" || command == "MKDIR" || command == "RMDIR") {
        // CD, MKDIR, RMDIR require exactly one argument (path)
        std::string path;
        if (iss >> path) {
            return true;
        }
        return false;
    } else if (command == "MOVE" || command == "COPY") {
        // MOVE, COPY require two arguments (src and dst)
        std::string src, dst;
        if (iss >> src >> dst) {
            return true;
        }
        return false;
    } else if (command == "HELP" || command == "EXIT") {
        // HELP and EXIT require no arguments
        return true;
    }

    return false; // Unknown command
}

std::string create_json_command(const std::string& input) {
    std::istringstream iss(input);
    std::string command;
    iss >> command;

    json json_command;
    json_command["cmd"] = command;

    if (command == "LIST") {
        std::string path;
        if (iss >> path) {
            json_command["args"]["path"] = path;
        } else {
            json_command["args"]["path"] = "."; // Default to current directory
        }
    } else if (command == "UPLOAD" || command == "DOWNLOAD") {
        std::string first_arg, second_arg;
        if (iss >> first_arg) {
            json_command["args"][command == "UPLOAD" ? "local_path" : "remote_path"] = first_arg;
            if (iss >> second_arg) {
                json_command["args"][command == "UPLOAD" ? "remote_path" : "local_path"] = second_arg;
            }
        }
    } else if (command == "DELETE" || command == "CD" || command == "MKDIR" || command == "RMDIR") {
        std::string path;
        if (iss >> path) {
            json_command["args"]["path"] = path;
        }
    } else if (command == "MOVE" || command == "COPY") {
        std::string src, dst;
        if (iss >> src >> dst) {
            json_command["args"]["src"] = src;
            json_command["args"]["dst"] = dst;
        }
    }

    return json_command.dump();
}

void log_debug(const std::string& message) {
    std::cout << "[DEBUG] " << message << "\n";
}

void upload_file(asio::ip::tcp::socket& socket, const std::string& local_path, const std::string& remote_path) {
    try {
        log_debug("Preparing to upload file: " + local_path + " as " + remote_path);

        // Create the JSON command
        json command;
        command["cmd"] = "UPLOAD";
        command["args"]["filename"] = remote_path;

        // Send the command to the server
        asio::write(socket, asio::buffer(command.dump() + "\n"));
        log_debug("Upload command sent: " + command.dump());

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();
        log_debug("Server response status: " + status);

        if (status != "ready") {
            std::cerr << "Server is not ready: " << response.at("message").get<std::string>() << "\n";
            log_debug("Server not ready message: " + response.at("message").get<std::string>());
            return;
        }

        log_debug("Server is ready to receive the file.");

        // Open the file for reading
        std::ifstream input_file(local_path, std::ios::binary);
        if (!input_file.is_open()) {
            throw std::runtime_error("Failed to open file: " + local_path);
        }

        // Get the file size
        input_file.seekg(0, std::ios::end);
        size_t file_size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);

        log_debug("File size: " + std::to_string(file_size));

        // Send the file size to the server
        asio::write(socket, asio::buffer(std::to_string(file_size) + "\n"));
        log_debug("File size sent to server.");

        // Send the file data
        char buffer_data[1024];
        size_t bytes_sent = 0;
        while (bytes_sent < file_size) {
            input_file.read(buffer_data, sizeof(buffer_data));
            std::streamsize bytes_read = input_file.gcount();
            asio::write(socket, asio::buffer(buffer_data, bytes_read));
            bytes_sent += bytes_read;

            log_debug("Sent " + std::to_string(bytes_sent) + " of " + std::to_string(file_size) + " bytes.");
        }

        input_file.close();
        log_debug("File upload completed: " + local_path);

        // Wait for the server's acknowledgment
        asio::streambuf ack_buffer;
        asio::read_until(socket, ack_buffer, '\n');
        std::istream ack_stream(&ack_buffer);
        std::string ack_message;
        std::getline(ack_stream, ack_message);
        auto ack_response = json::parse(ack_message);
        log_debug("Server acknowledgment: " + ack_response.dump());
        std::cout << "Server response: " << ack_response.dump() << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error during file upload: " << e.what() << "\n";
        log_debug("Error during upload: " + std::string(e.what()));
    }
}

void process_list_command(asio::ip::tcp::socket& socket, const std::string& input) {
    try {
        // Create JSON command for LIST
        std::string json_command = create_json_command(input);

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command + "\n"));
        std::cout << "LIST command sent to server: " << json_command << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status == "OK") {
            auto data = response.value("data", json::object());
            if (data.contains("entries")) {
                auto entries = data.at("entries");
                if (entries.is_array()) {
                    std::cout << "Files and directories received from server:\n";
                    for (const auto& entry : entries) {
                        std::string name = entry.value("name", "<unknown>");
                        std::string type = entry.value("type", "<unknown>");
                        std::cout << "  [" << type << "] " << name << "\n";
                    }
                }
            }

            // Display total entries
            if (data.contains("total_entries")) {
                size_t total_entries = data.at("total_entries").get<size_t>();
                std::cout << "Total entries: " << total_entries << "\n";
            }
        } else {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing LIST command: " << e.what() << "\n";
    }
}

void process_delete_command(asio::ip::tcp::socket& socket, const std::string& input) {
    try {
        // Create JSON command for DELETE
        std::string json_command = create_json_command(input);

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command + "\n"));
        std::cout << "DELETE command sent to server: " << json_command << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status == "OK") {
            std::cout << "File deleted successfully.\n";
        } else {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing DELETE command: " << e.what() << "\n";
    }
}

void process_download_command(asio::ip::tcp::socket& socket, const std::string& input) {
    try {
        // Parse the input to extract remote and local paths
        std::istringstream iss(input);
        std::string command, remote_path, local_path;
        iss >> command >> remote_path >> local_path;

        if (remote_path.empty()) {
            std::cerr << "DOWNLOAD command requires at least a remote path.\n";
            return;
        }

        if (local_path.empty()) {
            // Default to the same name as the remote file
            local_path = remote_path;
        }

        // Check if the local file already exists
        if (std::filesystem::exists(local_path)) {
            std::cerr << "File already exists: " << local_path << ". Please delete it or choose a different name.\n";
            return;
        }

        // Try to open the local file for writing
        std::ofstream output_file(local_path, std::ios::binary);
        if (!output_file.is_open()) {
            std::cerr << "Failed to open file for writing: " << local_path << "\n";
            return;
        }

        // Create JSON command for DOWNLOAD
        json json_command;
        json_command["cmd"] = "DOWNLOAD";
        json_command["args"]["remote_path"] = remote_path;

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command.dump() + "\n"));
        std::cout << "DOWNLOAD command sent to server: " << json_command.dump() << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status != "ready") {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
            return;
        }

        // Get the file size from the server's response
        size_t file_size = response.at("data").at("file_size").get<size_t>();
        std::cout << "File size: " << file_size << " bytes.\n";

        // Receive the file data
        size_t bytes_received = 0;
        char data[1024];
        while (bytes_received < file_size) {
            size_t len = socket.read_some(asio::buffer(data, sizeof(data)));
            output_file.write(data, len);
            bytes_received += len;

            std::cout << "Received " << bytes_received << " of " << file_size << " bytes.\n";
        }

        output_file.close();
        std::cout << "File downloaded successfully to: " << local_path << "\n";
    } catch (const std::exception& e) {
        std::cerr << "Error processing DOWNLOAD command: " << e.what() << "\n";
    }
}

void process_cd_command(asio::ip::tcp::socket& socket, const std::string& input) {
    try {
        // Parse the input to extract the path
        std::istringstream iss(input);
        std::string command, path;
        iss >> command >> path;

        if (path.empty()) {
            std::cerr << "CD command requires a path.\n";
            return;
        }

        // Create JSON command for CD
        json json_command;
        json_command["cmd"] = "CD";
        json_command["args"]["path"] = path;

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command.dump() + "\n"));
        std::cout << "CD command sent to server: " << json_command.dump() << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status == "OK") {
            std::string new_path = response.at("data").value("current_directory", "<unknown>");
            std::cout << "Directory changed to: " << new_path << "\n";
        } else {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
        }
    } catch (const std::exception& e) {
        std::cerr << "Error processing CD command: " << e.what() << "\n";
    }
}

void interactive_shell(asio::ip::tcp::socket& socket) {
    std::cout << "Enter commands. Type 'exit' to quit.\n";

    while (true) {
        try {
            std::string input;
            std::cout << "> ";
            std::getline(std::cin, input);

            if (input == "exit" || input == "EXIT") {
                std::cout << "Exiting interactive shell.\n";
                break;
            }

            if (input == "HELP") {
                print_available_commands();
                continue;
            }

            if (validate_command(input)) {
                std::istringstream iss(input);
                std::string command;
                iss >> command;

                if (command == "UPLOAD") {
                    std::string local_path, remote_path;
                    iss >> local_path >> remote_path;

                    if (local_path.empty()) {
                        std::cerr << "UPLOAD command requires at least a local path.\n";
                        continue;
                    }

                    if (remote_path.empty()) {
                        remote_path = local_path; // Default to the same name on the server
                    }

                    upload_file(socket, local_path, remote_path);
                } else if (command == "LIST") {
                    process_list_command(socket, input);
                } else if (command == "DELETE") {
                    process_delete_command(socket, input);
                } else if (command == "DOWNLOAD") {
                    process_download_command(socket, input);
                } else if (command == "CD") {
                    process_cd_command(socket, input);
                } else {
                    // Create JSON command for other commands
                    std::string json_command = create_json_command(input);

                    // Send the JSON command to the server
                    asio::write(socket, asio::buffer(json_command + "\n"));
                    std::cout << "Command sent to server: " << json_command << "\n";
                }
            } else {
                std::cout << "Invalid command or missing arguments.\n";
                print_available_commands();
            }
        } catch (const std::exception& e) {
            std::cerr << "Error: " << e.what() << "\n";
            break;
        }
    }
}

std::tuple<std::string, std::string, std::string> parse_client_arguments(const std::string& arg) {
    std::regex pattern(R"((\w+)@([\d\.]+):(\d+))");
    std::smatch match;

    if (std::regex_match(arg, match, pattern) && match.size() == 4) {
        std::string username = match[1];
        std::string ip = match[2];
        std::string port = match[3];
        return {username, ip, port};
    }

    throw std::invalid_argument("Invalid argument format. Expected: username@<server_ip>:<port>");
}

void attempt_connection(const std::string& arg) {
    try {
        auto [username, ip, port] = parse_client_arguments(arg);

        asio::io_context io_context;
        asio::ip::tcp::resolver resolver(io_context);
        asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(ip, port);

        asio::ip::tcp::socket socket(io_context);
        asio::connect(socket, endpoints);

        // Send the username to the server
        asio::write(socket, asio::buffer(username + "\n"));

        std::cout << "Successfully connected to " << ip << ":" << port << " as user " << username << "\n";

        // Start the interactive shell
        interactive_shell(socket);
    } catch (const std::exception& e) {
        std::cerr << "Failed to connect: " << e.what() << "\n";
    }
}



int main(int argc, char* argv[]) {
    try {
        if (argc != 2) {
            throw std::invalid_argument("Usage: ./client username@<server_ip>:<port>");
        }

        std::string arg = argv[1];
        attempt_connection(arg);
    } catch (const std::exception& e) {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}