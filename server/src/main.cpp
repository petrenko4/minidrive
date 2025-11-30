#include <iostream>
#include <asio.hpp>
#include <string>
#include <signal.h>

#include "minidrive/version.hpp"

void handle_client(asio::ip::tcp::socket &socket) {
    try {
        std::cout << "Client connected: " << socket.remote_endpoint() << "\n";

        // Send a welcome message to the client
        std::string welcome_message = "Welcome to the server!\n";
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

            // Echo the message back to the client
            std::string response = "Echo: " + message + "\n";
            asio::write(socket, asio::buffer(response));
        }
    } catch (const std::exception& e) {
        std::cerr << "Client disconnected or error: " << e.what() << "\n";
    }
}

void run_server(const std::string& host, const std::string& port) {
    try {
        asio::io_context io_context;

        asio::ip::tcp::acceptor acceptor(io_context, asio::ip::tcp::endpoint(asio::ip::make_address(host), std::stoi(port)));
        std::cout << "Server is running on " << host << ":" << port << "\n";

        while (true) {
            asio::ip::tcp::socket socket(io_context);
            acceptor.accept(socket);

            std::cout << "New connection from " << socket.remote_endpoint() << "\n";

            handle_client(socket);
        }
    } catch (const std::exception& e) {
        std::cerr << "Server error: " << e.what() << "\n";
    }
}

void handle_sigint(int) {
    std::cout << "Server shutting down gracefully...\n";
    exit(0);
}

int main(int argc, char* argv[]) {

    // auto err = signal(SIGSEGV, handle_sigint);
    // if(err == SIG_ERR) {
    //     std::cerr << "Failed to set signal handler\n";
    //     return 1;
    // }

    // while(true) {
    //     std::cout << "Server still running...\n";
    //     sleep(3);
    // }

    if (argc != 3) {
        std::cerr << "Usage: " << argv[0] << " <host> <port>\n";
        return 1;
    }

    std::string host = argv[1];
    std::string port = argv[2];

    std::cout << "MiniDrive server (version " << minidrive::version() << ")\n";
    run_server(host, port);

    return 0;
}
