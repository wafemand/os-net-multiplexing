#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "util/EventManager.h"
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <util/ConsoleHandler.h>


using namespace std;


class ClientHandler : public IHandler {
public:
    static const int BUFFER_SIZE = 1024;

    explicit ClientHandler(int fd) : client_socket(fd) {
        cout << "Client connected: " << fd << endl << endl;
    }

    void handleInput(EventManager &eventManager) override {
        uint8_t buffer[BUFFER_SIZE];
        const int bytes = read(client_socket, buffer, BUFFER_SIZE);

        if (bytes == -1) {
            throw HandlerException("Cannot read from client.", errno);
        } else if (bytes == 0) {
            cout << "Client disconnected: " << client_socket << endl << endl;
            eventManager.deleteHandler(client_socket);
        } else {
            string query(buffer, buffer + bytes);
            cout << "Data from client: " << client_socket << endl
                 << "    " << query << endl << endl;
            currentMessage += query;
            eventManager.resetHandler(getFD());
        }
    }

    void handleError(EventManager &eventManager) override {
        const error_t error = getError(client_socket);
        eventManager.deleteHandler(client_socket);
        if (error != 0) {
            throw HandlerException("Client handler failed.", error);
        }
    }

    void handleOutput(EventManager &eventManager) override {
        if (bytesSent == currentMessage.size()) {
            return;
        }
        int sent = write(client_socket,
                         currentMessage.data() + bytesSent,
                         currentMessage.size() - bytesSent);
        if (sent == -1) {
            throw HandlerException("Cannot send response.", errno);
        }
        bytesSent += sent;
    }

    int getFlags() override { return EventManager::INPUT_EVENT | EventManager::OUTPUT_EVENT; }

    int getFD() override { return client_socket; }

    ~ClientHandler() override { close(client_socket); }

private:
    int client_socket;
    string currentMessage;
    int bytesSent = 0;
};


class ClientAcceptor : public IHandler {
public:
    static const int MAX_PENDING = 228;

    explicit ClientAcceptor(in_port_t port) {
        listen_socket = socket(PF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
        sockaddr_in addr{};
        addr.sin_family = PF_INET;
        addr.sin_port = htons(port);
        addr.sin_addr.s_addr = INADDR_ANY;
        if (::bind(listen_socket, (sockaddr *) &addr, sizeof(addr)) == -1) {
            throw HandlerException("Cannot bind.", errno);
        }
        if (listen(listen_socket, MAX_PENDING) == -1) {
            throw HandlerException("Cannot listen.", errno);
        }
        cout << "Server started on port " << port << endl;
    }

    void handleInput(EventManager &eventManager) override {
        sockaddr_in clientAddr{};
        socklen_t len;
        const int client_socket = accept(listen_socket, (sockaddr *) &clientAddr, &len);
        if (client_socket == -1) {
            throw HandlerException("Cannot accept client", errno);
        }
        const int flags = fcntl(client_socket, F_GETFL, 0);
        fcntl(client_socket, F_SETFL, flags | O_NONBLOCK);
        shared_ptr<IHandler> handler(new ClientHandler(client_socket));
        eventManager.addHandler(handler);
        cout << "New client connected. Client's file descriptor: " << client_socket << endl;
    }

    void handleError(EventManager &eventManager) override {
        const error_t error = getError(listen_socket);
        eventManager.deleteAll();
        if (error != 0) {
            throw HandlerException("Listener failed.", error);
        }
    }

    int getFlags() override { return EventManager::INPUT_EVENT; }

    int getFD() override { return listen_socket; }

    ~ClientAcceptor() override = default;

private:
    int listen_socket;
};


class ServerConsole : public ConsoleHandler {
public:
    void handleInput(EventManager &eventManager) override {
        std::string command;
        std::cin >> command;
        if (command == "exit") {
            eventManager.deleteAll();
        }
    }
};


int main(int argc, char *argv[]) {
    try {
        const in_port_t port = stoi(argv[1]);
        shared_ptr<IHandler> acceptor(new ClientAcceptor(port));
        shared_ptr<IHandler> consoleHandler(new ServerConsole());
        EventManager epollWaiter;
        epollWaiter.addHandler(acceptor);
        epollWaiter.addHandler(consoleHandler);
        epollWaiter.wait();
    } catch (HandlerException const &e) {
        cerr << e.what() << endl;
    } catch (EventException const &e) {
        cerr << e.what() << endl;
    } catch (logic_error const &e) {
        cerr << "Invalid arguments." << endl;
    }
}
