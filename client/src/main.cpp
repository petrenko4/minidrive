#include <iostream>
#include <regex>
#include <string>
#include "minidrive/version.hpp"
#include <asio.hpp>
#include <fstream>
#include <nlohmann/json.hpp>
#include <filesystem>
#include "minidrive/fnv1a_hash.hpp"

using json = nlohmann::json;

bool parse_arguments(int argc, char *argv[], std::string &connection, std::string &log_file)
{
    if (argc < 2 || argc > 4)
    {
        std::cerr << "Usage: " << argv[0] << " [username@]<server_ip>:<port> [--log <log_file>]\n";
        return false;
    }

    connection = argv[1];

    if (argc == 4)
    {
        if (std::string(argv[2]) != "--log")
        {
            std::cerr << "Invalid option: " << argv[2] << "\n";
            std::cerr << "Usage: " << argv[0] << " [username@]<server_ip>:<port> [--log <log_file>]\n";
            return false;
        }
        log_file = argv[3];
    }

    return true;
}

bool parse_connection_string(const std::string &connection, std::string &ip, std::string &port)
{
    std::regex pattern(R"(^(?:([^@]+)@)?([^:]+):(\d+)$)");

    std::smatch matches;

    if (std::regex_match(connection, matches, pattern))
    {
        ip = matches[2].str();
        port = matches[3].str();
        return true;
    }

    return false;
}

void print_available_commands()
{
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

bool validate_command(const std::string &input)
{
    std::istringstream iss(input);
    std::string command;
    iss >> command;

    if (command == "LIST")
    {
        // LIST can optionally have one argument
        std::string path;
        if (iss >> path)
        {
            return true;
        }
        return true; // No argument is also valid
    }
    else if (command == "UPLOAD")
    {
        // UPLOAD requires at least one argument (local_path)
        std::string local_path;
        if (iss >> local_path)
        {
            return true;
        }
        return false;
    }
    else if (command == "DOWNLOAD")
    {
        // DOWNLOAD requires at least one argument (remote_path)
        std::string remote_path;
        if (iss >> remote_path)
        {
            return true;
        }
        return false;
    }
    else if (command == "DELETE")
    {
        // DELETE requires exactly one argument (path)
        std::string path;
        if (iss >> path)
        {
            return true;
        }
        return false;
    }
    else if (command == "CD" || command == "MKDIR" || command == "RMDIR")
    {
        // CD, MKDIR, RMDIR require exactly one argument (path)
        std::string path;
        if (iss >> path)
        {
            return true;
        }
        return false;
    }
    else if (command == "MOVE" || command == "COPY")
    {
        // MOVE, COPY require two arguments (src and dst)
        std::string src, dst;
        if (iss >> src >> dst)
        {
            return true;
        }
        return false;
    }
    else if (command == "SYNC")
    {
        std::string src, dst;
        if (iss >> src >> dst)
        {
            return true;
        }
        return false;
    }
    else if (command == "HELP" || command == "EXIT")
    {
        // HELP and EXIT require no arguments
        return true;
    }

    return false; // Unknown command
}

std::string create_json_command(const std::string &input)
{
    std::istringstream iss(input);
    std::string command;
    iss >> command;

    json json_command;
    json_command["cmd"] = command;

    if (command == "LIST")
    {
        std::string path;
        if (iss >> path)
        {
            json_command["args"]["path"] = path;
        }
        else
        {
            json_command["args"]["path"] = "."; // Default to current directory
        }
    }
    else if (command == "UPLOAD" || command == "DOWNLOAD")
    {
        std::string first_arg, second_arg;
        if (iss >> first_arg)
        {
            json_command["args"][command == "UPLOAD" ? "local_path" : "remote_path"] = first_arg;
            if (iss >> second_arg)
            {
                json_command["args"][command == "UPLOAD" ? "remote_path" : "local_path"] = second_arg;
            }
        }
    }
    else if (command == "DELETE" || command == "CD" || command == "MKDIR" || command == "RMDIR")
    {
        std::string path;
        if (iss >> path)
        {
            json_command["args"]["path"] = path;
        }
    }
    else if (command == "MOVE" || command == "COPY")
    {
        std::string src, dst;
        if (iss >> src >> dst)
        {
            json_command["args"]["src"] = src;
            json_command["args"]["dst"] = dst;
        }
    }

    return json_command.dump();
}

void log_debug(const std::string &message)
{
    std::cout << "[DEBUG] " << message << "\n";
}

void upload_file(asio::ip::tcp::socket &socket, const std::string &local_path, const std::string &remote_path)
{
    try
    {
        // Commented out debug lines
        // log_debug("Preparing to upload file: " + local_path + " as " + remote_path);

        // Create the JSON command
        json command;
        command["cmd"] = "UPLOAD";
        command["args"]["filename"] = remote_path;

        // Send the command to the server
        asio::write(socket, asio::buffer(command.dump() + "\n"));
        // log_debug("Upload command sent: " + command.dump());

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();
        // log_debug("Server response status: " + status);

        if (status != "ready")
        {
            std::cerr << "Server is not ready: " << response.at("message").get<std::string>() << "\n";
            // log_debug("Server not ready message: " + response.at("message").get<std::string>());
            return;
        }

        // log_debug("Server is ready to receive the file.");

        // Open the file for reading
        std::ifstream input_file(local_path, std::ios::binary);
        if (!input_file.is_open())
        {
            throw std::runtime_error("Failed to open file: " + local_path);
        }

        // Get the file size
        input_file.seekg(0, std::ios::end);
        size_t file_size = input_file.tellg();
        input_file.seekg(0, std::ios::beg);

        // log_debug("File size: " + std::to_string(file_size));

        // Send the file size to the server
        asio::write(socket, asio::buffer(std::to_string(file_size) + "\n"));
        // log_debug("File size sent to server.");

        // Send the file data
        char buffer_data[1024];
        size_t bytes_sent = 0;
        while (bytes_sent < file_size)
        {
            input_file.read(buffer_data, sizeof(buffer_data));
            std::streamsize bytes_read = input_file.gcount();
            asio::write(socket, asio::buffer(buffer_data, bytes_read));
            bytes_sent += bytes_read;

            // log_debug("Sent " + std::to_string(bytes_sent) + " of " + std::to_string(file_size) + " bytes.");
        }

        input_file.close();
        // log_debug("File upload completed: " + local_path);

        // Wait for the server's acknowledgment
        asio::streambuf ack_buffer;
        asio::read_until(socket, ack_buffer, '\n');
        std::istream ack_stream(&ack_buffer);
        std::string ack_message;
        std::getline(ack_stream, ack_message);
        auto ack_response = json::parse(ack_message);
        // log_debug("Server acknowledgment: " + ack_response.dump());
        // std::cout << "Server response: " << ack_response.dump() << "\n";
        std::cout << "File uploaded successfully: " << remote_path << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error during file upload: " << e.what() << "\n";
        // log_debug("Error during upload: " + std::string(e.what()));
    }
}

void process_list_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    try
    {
        // Create JSON command for LIST
        std::string json_command = create_json_command(input);

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command + "\n"));
        // std::cout << "LIST command sent to server: " << json_command << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status == "OK")
        {
            auto data = response.value("data", json::object());
            if (data.contains("entries"))
            {
                auto entries = data.at("entries");
                if (entries.is_array())
                {
                    std::cout << "Files and directories received from server:\n";
                    for (const auto &entry : entries)
                    {
                        std::string name = entry.value("name", "<unknown>");
                        std::string type = entry.value("type", "<unknown>");
                        std::cout << "  [" << type << "] " << name << "\n";
                    }
                }
            }

            // Display total entries
            if (data.contains("total_entries"))
            {
                size_t total_entries = data.at("total_entries").get<size_t>();
                std::cout << "Total entries: " << total_entries << "\n";
            }
        }
        else
        {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error processing LIST command: " << e.what() << "\n";
    }
}

void process_delete_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    try
    {
        // Create JSON command for DELETE
        std::string json_command = create_json_command(input);

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command + "\n"));
        // std::cout << "DELETE command sent to server: " << json_command << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status == "OK")
        {
            std::cout << "File deleted successfully.\n";
        }
        else
        {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error processing DELETE command: " << e.what() << "\n";
    }
}

void process_download_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    try
    {
        // Parse the input to extract remote and local paths
        std::istringstream iss(input);
        std::string command, remote_path, local_path;
        iss >> command >> remote_path >> local_path;

        if (remote_path.empty())
        {
            std::cerr << "DOWNLOAD command requires at least a remote path.\n";
            return;
        }

        if (local_path.empty())
        {
            // Default to the same name as the remote file
            local_path = remote_path;
        }

        // Check if the local file already exists
        if (std::filesystem::exists(local_path))
        {
            std::cerr << "File already exists: " << local_path << ". Please delete it or choose a different name.\n";
            return;
        }

        // Try to open the local file for writing
        std::ofstream output_file(local_path, std::ios::binary);
        if (!output_file.is_open())
        {
            std::cerr << "Failed to open file for writing: " << local_path << "\n";
            return;
        }

        // Create JSON command for DOWNLOAD
        json json_command;
        json_command["cmd"] = "DOWNLOAD";
        json_command["args"]["remote_path"] = remote_path;

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command.dump() + "\n"));
        // std::cout << "DOWNLOAD command sent to server: " << json_command.dump() << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status != "ready")
        {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
            return;
        }

        // Get the file size from the server's response
        size_t file_size = response.at("data").at("file_size").get<size_t>();
        std::cout << "File size: " << file_size << " bytes.\n";

        // Receive the file data
        size_t bytes_received = 0;
        char data[1024];
        while (bytes_received < file_size)
        {
            size_t len = socket.read_some(asio::buffer(data, sizeof(data)));
            output_file.write(data, len);
            bytes_received += len;

            std::cout << "Received " << bytes_received << " of " << file_size << " bytes.\n";
        }

        output_file.close();
        std::cout << "File downloaded successfully to: " << local_path << "\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error processing DOWNLOAD command: " << e.what() << "\n";
    }
}

void process_cd_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    try
    {
        // Parse the input to extract the path
        std::istringstream iss(input);
        std::string command, path;
        iss >> command >> path;

        if (path.empty())
        {
            std::cerr << "CD command requires a path.\n";
            return;
        }

        // Create JSON command for CD
        json json_command;
        json_command["cmd"] = "CD";
        json_command["args"]["path"] = path;

        // Send the JSON command to the server
        asio::write(socket, asio::buffer(json_command.dump() + "\n"));
        // std::cout << "CD command sent to server: " << json_command.dump() << "\n";

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status == "OK")
        {
            std::string new_path = response.at("data").value("current_directory", "<unknown>");
            std::cout << "Directory changed to: " << new_path << "\n";
        }
        else
        {
            std::cerr << "Error from server: " << response.at("message").get<std::string>() << "\n";
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error processing CD command: " << e.what() << "\n";
    }
}

void process_mkdir_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    // Parse the input to extract the path
    std::istringstream iss(input);
    std::string command, path;
    iss >> command >> path;

    if (path.empty())
    {
        std::cerr << "MKDIR command requires a path.\n";
        return;
    }

    // Create JSON command for MKDIR
    json json_command;
    json_command["cmd"] = "MKDIR";
    json_command["args"]["path"] = path;

    // Send the command to the server
    asio::write(socket, asio::buffer(json_command.dump() + "\n"));

    // Wait for the server's response
    asio::streambuf buffer;
    asio::read_until(socket, buffer, '\n');
    std::istream response_stream(&buffer);
    std::string response_message;
    std::getline(response_stream, response_message);

    // Parse the server's response
    auto response = json::parse(response_message);
    std::string status = response.at("status").get<std::string>();

    if (status == "OK")
    {
        std::cout << "Directory created successfully.\n";
    }
    else
    {
        std::cerr << "Failed to create directory: " << response.at("message").get<std::string>() << "\n";
    }
}

void process_rmdir_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    // Parse the input to extract the path
    std::istringstream iss(input);
    std::string command, path;
    iss >> command >> path;

    if (path.empty())
    {
        std::cerr << "RMDIR command requires a path.\n";
        return;
    }

    // Create JSON command for RMDIR
    json json_command;
    json_command["cmd"] = "RMDIR";
    json_command["args"]["path"] = path;

    // Send the command to the server
    asio::write(socket, asio::buffer(json_command.dump() + "\n"));

    // Wait for the server's response
    asio::streambuf buffer;
    asio::read_until(socket, buffer, '\n');
    std::istream response_stream(&buffer);
    std::string response_message;
    std::getline(response_stream, response_message);

    // Parse the server's response
    auto response = json::parse(response_message);
    std::string status = response.at("status").get<std::string>();

    if (status == "OK")
    {
        std::cout << "Directory removed successfully.\n";
    }
    else
    {
        std::cerr << "Failed to remove directory: " << response.at("message").get<std::string>() << "\n";
    }
}

void process_move_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    // Parse the input to extract source and destination paths
    std::istringstream iss(input);
    std::string command, src, dst;
    iss >> command >> src >> dst;

    if (src.empty() || dst.empty())
    {
        std::cerr << "MOVE command requires both source and destination paths.\n";
        return;
    }

    // Create JSON command for MOVE
    json json_command;
    json_command["cmd"] = "MOVE";
    json_command["args"]["src"] = src;
    json_command["args"]["dst"] = dst;

    // Send the command to the server
    asio::write(socket, asio::buffer(json_command.dump() + "\n"));

    // Wait for the server's response
    asio::streambuf buffer;
    asio::read_until(socket, buffer, '\n');
    std::istream response_stream(&buffer);
    std::string response_message;
    std::getline(response_stream, response_message);

    // Parse the server's response
    auto response = json::parse(response_message);
    std::string status = response.at("status").get<std::string>();

    if (status == "OK")
    {
        std::cout << "File or folder moved successfully.\n";
    }
    else
    {
        std::cerr << "Failed to move file or folder: " << response.at("message").get<std::string>() << "\n";
    }
}

void process_copy_command(asio::ip::tcp::socket &socket, const std::string &input)
{
    // Parse the input to extract source and destination paths
    std::istringstream iss(input);
    std::string command, src, dst;
    iss >> command >> src >> dst;

    if (src.empty() || dst.empty())
    {
        std::cerr << "COPY command requires both source and destination paths.\n";
        return;
    }

    // Create JSON command for COPY
    json json_command;
    json_command["cmd"] = "COPY";
    json_command["args"]["src"] = src;
    json_command["args"]["dst"] = dst;

    // Send the command to the server
    asio::write(socket, asio::buffer(json_command.dump() + "\n"));

    // Wait for the server's response
    asio::streambuf buffer;
    asio::read_until(socket, buffer, '\n');
    std::istream response_stream(&buffer);
    std::string response_message;
    std::getline(response_stream, response_message);

    // Parse the server's response
    auto response = json::parse(response_message);
    std::string status = response.at("status").get<std::string>();

    if (status == "OK")
    {
        std::cout << "File or folder copied successfully.\n";
    }
    else
    {
        std::cerr << "Failed to copy file or folder: " << response.at("message").get<std::string>() << "\n";
    }
}

static std::vector<std::string> collect_files_under(
    const std::unordered_map<std::string, FileInfo> &server_files,
    const std::string &dir)
{
    std::vector<std::string> result;
    std::string prefix = dir + "/";

    for (const auto &[path, info] : server_files)
    {
        if (info.type == "file" &&
            (path == dir || path.rfind(prefix, 0) == 0))
        {
            result.push_back(path);
        }
    }
    return result;
}

void process_sync_command(asio::ip::tcp::socket &socket, const std::string &src, const std::string &dst)
{
    size_t skipped_files = 0;
    size_t uploaded_files = 0;
    size_t deleted_files = 0;

    std::vector<std::string> skipped_paths;
    std::vector<std::string> uploaded_paths;
    std::vector<std::string> deleted_paths;

    try
    {
        // Commented out debug lines
        // log_debug("Sending SYNC command to server");

        // Create and send the SYNC command
        json sync_command;
        sync_command["cmd"] = "SYNC";
        sync_command["args"]["dst"] = dst;
        asio::write(socket, asio::buffer(sync_command.dump() + "\n"));

        // Wait for the server's response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        // Parse the server's response
        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status != "OK")
        {
            std::cerr << "SYNC command failed: " << response.at("message").get<std::string>() << "\n";
            return;
        }

        // log_debug("SYNC command successful. Comparing file trees.");

        // Get the server file tree
        std::unordered_map<std::string, FileInfo> server_files;
        for (const auto &[path, info] : response.at("data").items())
        {
            uint64_t hash = info.value("hash", 0);
            std::string type = info.value("type", "unknown");
            server_files[path] = FileInfo(hash, type);
        }

        // Pretty-print server file tree
        // log_debug("Server file tree:");
        for (const auto &[path, info] : server_files)
        {
            // log_debug("  Path: " + path + ", Type: " + info.type + ", Hash: " + std::to_string(info.hash));
        }

        // Build the local file tree
        std::unordered_map<std::string, FileInfo> local_files;
        for (const auto &entry : std::filesystem::recursive_directory_iterator(src))
        {
            std::string relative_path = std::filesystem::relative(entry.path(), src).string();
            // std::cout << "[DEBUG] hashing: " << entry.path() << '\n';
            // if (!entry.is_directory())
            //     std::cout << "[DEBUG] size: "
            //               << std::filesystem::file_size(entry.path())
            //               << '\n';
            // std::cout << "[DEBUG] absolute path: " << std::filesystem::absolute(entry.path()) << '\n';

            if (relative_path == ".")
                continue;

            if (entry.is_regular_file())
            {
                uint64_t file_hash = hash_file(entry.path().string());
                local_files[relative_path] = FileInfo(file_hash, "file");
            }
            else if (entry.is_directory())
            {
                local_files[relative_path] = FileInfo(0, "directory");
            }
        }

        // Pretty-print local file tree
        // log_debug("Local file tree:");
        for (const auto &[path, info] : local_files)
        {
            // log_debug("  Path: " + path + ", Type: " + info.type + ", Hash: " + std::to_string(info.hash));
        }

        // Compare server and local files
        for (auto it = server_files.begin(); it != server_files.end();)
        {
            const std::string &server_path = it->first;
            const FileInfo &server_info = it->second;

            auto local_it = local_files.find(server_path);
            if (local_it != local_files.end())
            {
                const FileInfo &local_info = local_it->second;

                if (server_info.type == "file" && local_info.hash == server_info.hash)
                {
                    skipped_files++;
                    skipped_paths.push_back(server_path);

                    // Same file, remove from both maps
                    local_files.erase(local_it);
                    it = server_files.erase(it);
                    continue;
                }
            }

            ++it; // Move to the next server file
        }

        // Debug: Print remaining server files
        // log_debug("Remaining server files after comparison:");
        for (const auto &[path, info] : server_files)
        {
            // log_debug("  Path: " + path + ", Type: " + info.type + ", Hash: " + std::to_string(info.hash));
        }

        // Debug: Print remaining local files
        // log_debug("Remaining local files after comparison:");
        for (const auto &[path, info] : local_files)
        {
            // log_debug("  Path: " + path + ", Type: " + info.type + ", Hash: " + std::to_string(info.hash));
        }

        // Handle files in server_files (delete from server)
        for (const auto &[server_path, server_info] : server_files)
        {
            if (server_info.type == "file")
            {
                deleted_files++;
                deleted_paths.push_back(server_path);

                // log_debug("Deleting server file: " + server_path);
                json delete_command;
                delete_command["cmd"] = "DELETE";
                delete_command["args"]["path"] = server_path;
                asio::write(socket, asio::buffer(delete_command.dump() + "\n"));

                // Check server response for DELETE
                asio::streambuf delete_buffer;
                asio::read_until(socket, delete_buffer, '\n');
                std::istream delete_response_stream(&delete_buffer);
                std::string delete_response_message;
                std::getline(delete_response_stream, delete_response_message);
                auto delete_response = json::parse(delete_response_message);
                if (delete_response.at("status").get<std::string>() != "OK")
                {
                    throw std::runtime_error("DELETE command failed: " + delete_response.at("message").get<std::string>());
                }
            }
            else if (server_info.type == "directory")
            {
                // log_debug("Deleting server directory: " + server_path);

                // 1. Collect files recursively BEFORE deletion
                auto files = collect_files_under(server_files, server_path);

                for (const auto &f : files)
                {
                    deleted_files++;
                    deleted_paths.push_back(f);
                }

                json rmdir_command;
                rmdir_command["cmd"] = "RMDIR";
                rmdir_command["args"]["path"] = server_path;
                asio::write(socket, asio::buffer(rmdir_command.dump() + "\n"));

                // Check server response for RMDIR
                asio::streambuf rmdir_buffer;
                asio::read_until(socket, rmdir_buffer, '\n');
                std::istream rmdir_response_stream(&rmdir_buffer);
                std::string rmdir_response_message;
                std::getline(rmdir_response_stream, rmdir_response_message);
                auto rmdir_response = json::parse(rmdir_response_message);
                if (rmdir_response.at("status").get<std::string>() != "OK")
                {
                    throw std::runtime_error("RMDIR command failed: " + rmdir_response.at("message").get<std::string>());
                }

                // Remove all entries related to the directory from the server's map
                for (auto it = server_files.begin(); it != server_files.end();)
                {
                    if (it->first.find(server_path + "/") == 0) // Check if the path starts with the directory path
                    {
                        // log_debug("Removing server map entry: " + it->first);
                        it = server_files.erase(it); // Erase and advance iterator
                    }
                    else
                    {
                        ++it;
                    }
                }
            }
        }

        // Create missing directories (sorted by depth)
        std::vector<std::string> dirs_to_create;

        for (const auto &[local_path, local_info] : local_files)
        {
            if (local_info.type == "directory")
                dirs_to_create.push_back(local_path);
        }

        // Sort so parents come before children
        std::sort(dirs_to_create.begin(), dirs_to_create.end(),
                  [](const std::string &a, const std::string &b)
                  {
                      return std::count(a.begin(), a.end(), '/') <
                             std::count(b.begin(), b.end(), '/');
                  });

        for (const auto &dir : dirs_to_create)
        {
            // log_debug("Creating directory on server: " + dir);

            json mkdir_command;
            mkdir_command["cmd"] = "MKDIR";
            mkdir_command["args"]["path"] = dir;

            asio::write(socket, asio::buffer(mkdir_command.dump() + "\n"));

            asio::streambuf buffer;
            asio::read_until(socket, buffer, '\n');

            std::istream is(&buffer);
            std::string response;
            std::getline(is, response);

            auto r = json::parse(response);
            if (r.at("status") != "OK")
            {
                throw std::runtime_error("MKDIR failed for " + dir);
            }
        }

        // Handle files in local_files (upload to server)
        for (const auto &[local_path, local_info] : local_files)
        {
            if (local_info.type == "file")
            {

                uploaded_files++;
                uploaded_paths.push_back(local_path);

                // log_debug("Uploading new file: " + local_path);

                // Send the upload command
                upload_file(socket, src + "/" + local_path, local_path);
            }
        }

        // log_debug("Synchronization complete.");
        std::cout << "\n=== SYNC SUMMARY ===\n";
        std::cout << "Files uploaded : " << uploaded_files << '\n';
        std::cout << "Files deleted  : " << deleted_files << '\n';
        std::cout << "Files skipped  : " << skipped_files << '\n';

        if (!uploaded_paths.empty())
        {
            std::cout << "\nUploaded files:\n";
            for (const auto &p : uploaded_paths)
                std::cout << "  + " << p << '\n';
        }

        if (!deleted_paths.empty())
        {
            std::cout << "\nDeleted files:\n";
            for (const auto &p : deleted_paths)
                std::cout << "  - " << p << '\n';
        }

        if (!skipped_paths.empty())
        {
            std::cout << "\nSkipped files:\n";
            for (const auto &p : skipped_paths)
                std::cout << "  = " << p << '\n';
        }

        std::cout << "====================\n\n";
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error processing SYNC command: " << e.what() << "\n";
        // log_debug("Error during SYNC command: " + std::string(e.what()));
    }
}

void interactive_shell(asio::ip::tcp::socket &socket)
{
    std::cout << "Enter commands. Type 'exit' to quit.\n";

    while (true)
    {
        try
        {
            std::string input;
            std::cout << "> ";
            std::getline(std::cin, input);

            if (input == "exit" || input == "EXIT")
            {
                std::cout << "Exiting interactive shell.\n";
                break;
            }

            if (input == "HELP")
            {
                print_available_commands();
                continue;
            }

            if (validate_command(input))
            {
                std::istringstream iss(input);
                std::string command;
                iss >> command;

                if (command == "UPLOAD")
                {
                    std::string local_path, remote_path;
                    iss >> local_path >> remote_path;

                    if (local_path.empty())
                    {
                        std::cerr << "UPLOAD command requires at least a local path.\n";
                        continue;
                    }

                    if (remote_path.empty())
                    {
                        remote_path = local_path; // Default to the same name on the server
                    }

                    upload_file(socket, local_path, remote_path);
                }
                else if (command == "LIST")
                {
                    process_list_command(socket, input);
                }
                else if (command == "DELETE")
                {
                    process_delete_command(socket, input);
                }
                else if (command == "DOWNLOAD")
                {
                    process_download_command(socket, input);
                }
                else if (command == "CD")
                {
                    process_cd_command(socket, input);
                }
                else if (command == "MKDIR")
                {
                    process_mkdir_command(socket, input);
                }
                else if (command == "RMDIR")
                {
                    process_rmdir_command(socket, input);
                }
                else if (command == "MOVE")
                {
                    process_move_command(socket, input);
                }
                else if (command == "COPY")
                {
                    process_copy_command(socket, input);
                }
                else if (command == "SYNC")
                {
                    std::string src, dst;
                    iss >> src >> dst;
                    process_sync_command(socket, src, dst);
                }
                else
                {
                    // Create JSON command for other commands
                    std::string json_command = create_json_command(input);

                    // Send the JSON command to the server
                    asio::write(socket, asio::buffer(json_command + "\n"));
                    std::cout << "Command sent to server: " << json_command << "\n";
                }
            }
            else
            {
                std::cout << "Invalid command or missing arguments.\n";
                print_available_commands();
            }
        }
        catch (const std::exception &e)
        {
            std::cerr << "Error: " << e.what() << "\n";
            break;
        }
    }
}

std::tuple<std::string, std::string, std::string> parse_client_arguments(const std::string &arg)
{
    std::regex pattern(R"((\w+)?@?([\d\.]+):(\d+))");
    std::smatch match;

    if (std::regex_match(arg, match, pattern) && match.size() == 4)
    {
        std::string username = match[1].matched ? match[1].str() : ""; // Default to empty string if username is not provided
        std::string ip = match[2].str();
        std::string port = match[3].str();
        return {username, ip, port};
    }

    throw std::invalid_argument("Invalid argument format. Expected: [username@]<server_ip>:<port>");
}

void handle_authentication(asio::ip::tcp::socket &socket, const std::string &username)
{
    try
    {
        // Step 1: Send username only
        json user_request;
        user_request["username"] = username;
        asio::write(socket, asio::buffer(user_request.dump() + "\n"));

        // std::cout << "[DEBUG] Sent username: " << username << std::endl;

        // Step 2: Wait for server response
        asio::streambuf buffer;
        asio::read_until(socket, buffer, '\n');
        std::istream response_stream(&buffer);
        std::string response_message;
        std::getline(response_stream, response_message);

        auto response = json::parse(response_message);
        std::string status = response.at("status").get<std::string>();

        if (status == "OK")
        {
            std::cout << "Authentication successful." << std::endl;
            std::cout << "[warning] operating in public mode - files are visible to everyone" << std::endl;
            return;
        }
        else if (status == "PSWD_REQ")
        {
            // Existing user → ask for password
            std::string password;
            std::cout << "Enter password: ";
            std::getline(std::cin, password);

            json pw_request;
            pw_request["password"] = password;
            asio::write(socket, asio::buffer(pw_request.dump() + "\n"));
        }
        else if (status == "NEW_USER")
        {
            // New user → ask to create a password
            std::cout << "User " << username << " not found. Register? (y/n):";
            std::string ans;
            std::cin >> ans;
            if (ans == "y")
            {
                std::cout << "Create password: ";
                std::string password;
                std::getline(std::cin >> std::ws, password);

                json pw_request;
                pw_request["password"] = password;
                asio::write(socket, asio::buffer(pw_request.dump() + "\n"));
            }
            else
                exit(EXIT_FAILURE);
        }
        else
        {
            std::cerr << "Unexpected server status: " << status << std::endl;
            exit(EXIT_FAILURE);
        }

        // Step 3: Wait for server authentication result
        asio::streambuf final_buffer;
        asio::read_until(socket, final_buffer, '\n');
        std::istream final_stream(&final_buffer);
        std::string final_message;
        std::getline(final_stream, final_message);

        auto final_response = json::parse(final_message);
        std::string final_status = final_response.at("status").get<std::string>();

        if (final_status == "OK")
        {
            std::cout << "Authentication successful." << std::endl;
            std::cout << "Logged as " << username << std::endl;
        }
        else
        {
            std::cerr << "Authentication failed: "
                      << final_response.at("message").get<std::string>() << std::endl;
            exit(EXIT_FAILURE);
        }
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error during authentication: " << e.what() << std::endl;
        exit(EXIT_FAILURE);
    }
}

void attempt_connection(const std::string &arg)
{
    try
    {
        auto [username, ip, port] = parse_client_arguments(arg);

        asio::io_context io_context;
        asio::ip::tcp::resolver resolver(io_context);
        asio::ip::tcp::resolver::results_type endpoints = resolver.resolve(ip, port);

        asio::ip::tcp::socket socket(io_context);
        asio::connect(socket, endpoints);

        std::string effective_username = username.empty() ? "public" : username;

        // Handle authentication
        handle_authentication(socket, effective_username);

        std::cout << "Successfully connected to " << ip << ":" << port << " as user " << effective_username << "\n";

        // Start the interactive shell
        interactive_shell(socket);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Failed to connect: " << e.what() << "\n";
    }
}

int main(int argc, char *argv[])
{
    try
    {
        if (argc != 2)
        {
            throw std::invalid_argument("Usage: ./client username@<server_ip>:<port>");
        }

        std::string arg = argv[1];
        attempt_connection(arg);
    }
    catch (const std::exception &e)
    {
        std::cerr << "Error: " << e.what() << "\n";
        return 1;
    }

    return 0;
}