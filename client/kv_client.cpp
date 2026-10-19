#include "client_connection.hpp"
#include <iostream>
int main(int argc, char** argv) {
    try {
        if (argc > 3) throw std::invalid_argument("usage: kv_client [host [port]]");
        kv::ClientConnection connection(argc > 1 ? argv[1] : "127.0.0.1", argc > 2 ? argv[2] : "8080");
        std::string line;
        while (std::getline(std::cin, line)) std::cout << connection.request(line) << std::endl;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
