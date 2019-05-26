#include <sys/socket.h>
#include <netinet/in.h>
#include <fcntl.h>
#include "util/EpollWaiter.h"
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <netdb.h>
#include "util/ConsoleHandler.h"


using namespace std;


in_addr_t hostnameToIp(string const &hostname) {
    auto he = gethostbyname(hostname.c_str());
    if (he == nullptr) {
        return 0;
    }
    auto **addr_list = (in_addr **) he->h_addr_list;
    return addr_list[0]->s_addr;
}


class Client : public IHandler {
public:
    static const int BUFFER_SIZE = 1024;

    Client(const string &host, in_port_t port) {
        server_socket = socket(AF_INET, SOCK_STREAM, 0);
        if (server_socket == -1) {
            throw HandlerException("Cannot create socket.", errno);
        }
        sockaddr_in addr{};
        addr.sin_family = AF_INET;
        addr.sin_addr.s_addr = hostnameToIp(host);
        addr.sin_port = htons(port);
        if (connect(server_socket, (sockaddr *) &addr, sizeof(addr)) == -1) {
            throw HandlerException("Cannot connect to server.", errno);
        }
    }

    void sendMessage(string const &message) {
        if (send(server_socket, message.data(), message.size(), 0) == -1) {
            throw HandlerException("Cannot send message.", errno);
        }
    }

    void handleData(EpollWaiter &waiter) override {
        uint8_t buffer[BUFFER_SIZE];
        int bytes = read(server_socket, buffer, BUFFER_SIZE);

        if (bytes == -1) {
            waiter.deleteAll();
            throw HandlerException("Cannot read message.", errno);
        } else if (bytes == 0) {
            cout << "Server disconnected." << endl << endl;
            waiter.deleteAll();
        } else {
            string query(buffer, buffer + bytes);
            cout << "Server answer: " << endl
                 << "    " << query << endl << endl;
        }
    }

    void handleError(EpollWaiter &waiter) override {
        waiter.deleteAll();
    }

    int getFD() override { return server_socket; }

    uint32_t getActions() override { return WAIT_INPUT; }

    ~Client() override { close(server_socket); }

private:
    int server_socket;
};


class ClientConsole : public ConsoleHandler {
public:
    explicit ClientConsole(IHandler *client) {
        this->client = dynamic_cast<Client *>(client);
        if (this->client == nullptr) {
            throw HandlerException("Handler is not Client", 0);
        }
    }

    void handleData(EpollWaiter &waiter) override {
        std::string message;
        getline(cin, message);
        if (message == "exit") {
            waiter.deleteAll();
        } else {
            client->sendMessage(message);
        }
    }

private:
    Client *client;
};


int main(int argc, char *argv[]) {
    try {
        const in_port_t port = stoi(argv[2]);
        shared_ptr<IHandler> acceptor(new Client(argv[1], port));
        shared_ptr<IHandler> consoleHandler(new ClientConsole(acceptor.get()));
        EpollWaiter epollWaiter;
        epollWaiter.addHandler(acceptor);
        epollWaiter.addHandler(consoleHandler);
        epollWaiter.wait();
    } catch (HandlerException const &e) {
        cerr << e.what() << endl;
    } catch (EpollException const &e) {
        cerr << e.what() << endl;
    } catch (logic_error const &e) {
        cerr << "Invalid arguments." << endl;
        cerr << e.what() << endl;
    }
}