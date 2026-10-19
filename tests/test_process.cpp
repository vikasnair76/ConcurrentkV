#include "test_support.hpp"
#include "client_connection.hpp"
#include <arpa/inet.h>
#include <csignal>
#include <sys/wait.h>
#include <spawn.h>
extern char** environ;
#include <thread>
struct Child {
    pid_t pid = -1;
    Child(const char* executable, const std::string& port, const std::string& wal) {
        const char* args[] = {executable, "--port", port.c_str(), "--wal", wal.c_str(), "--idle-seconds", "1", nullptr};
        int error = ::posix_spawn(&pid, executable, nullptr, nullptr, const_cast<char**>(args), environ);
        if (error) { pid = -1; throw std::runtime_error("posix_spawn: " + std::string(std::strerror(error))); }
    }
    ~Child() { if (pid > 0) { ::kill(pid, SIGKILL); int status; ::waitpid(pid, &status, 0); } }
    void stop(int signal, bool graceful) {
        CHECK(::kill(pid, signal) == 0); int status = 0;
        CHECK(::waitpid(pid, &status, 0) == pid); pid = -1;
        if (graceful) CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
        else CHECK(WIFSIGNALED(status) && WTERMSIG(status) == signal);
    }
};
kv::ClientConnection ready(const std::string& port) {
    for (int i = 0; i < 1500; ++i) {
        try { return kv::ClientConnection("127.0.0.1", port); }
        catch (const std::exception&) { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
    }
    throw std::runtime_error("server did not start");
}
int main(int argc, char** argv) {
    CHECK(argc == 2); Temp temp;
    // Reserve an OS-selected port; the short close/rebind interval is test-only.
    kv::Fd probe(::socket(AF_INET, SOCK_STREAM, 0)); CHECK(probe.get() >= 0);
    sockaddr_in address{}; address.sin_family = AF_INET; address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    CHECK(::bind(probe.get(), reinterpret_cast<sockaddr*>(&address), sizeof(address)) == 0);
    socklen_t length = sizeof(address); CHECK(::getsockname(probe.get(), reinterpret_cast<sockaddr*>(&address), &length) == 0);
    std::string port = std::to_string(ntohs(address.sin_port)); probe = kv::Fd();
    {
        Child child(argv[1], port, temp.wal()); auto c = ready(port);
        CHECK(c.request("SET survive graceful") == "OK"); child.stop(SIGTERM, true);
        CHECK(c.receive() == "ERR SHUTDOWN");
    }
    {
        Child child(argv[1], port, temp.wal()); auto c = ready(port);
        CHECK(c.request("GET survive") == "VALUE graceful");
        CHECK(c.request("SET crash acknowledged") == "OK"); child.stop(SIGKILL, false);
    }
    {
        Child child(argv[1], port, temp.wal()); auto c = ready(port);
        CHECK(c.request("GET crash") == "VALUE acknowledged");
        std::this_thread::sleep_for(std::chrono::milliseconds(1200)); fails([&] { c.receive(); });
        child.stop(SIGINT, true);
    }
    std::cout << "SIGTERM, SIGKILL recovery, idle timeout and SIGINT checks passed\n";
}
