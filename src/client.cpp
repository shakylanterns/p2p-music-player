#include "client.h"

Client::Client(uint16_t port,
               std::vector<std::tuple<std::string, std::string>> names)
    : acceptor(ctx, tcp::endpoint(tcp::v4(), port)), resolver(ctx), timer(ctx),
      names(names) {
    acceptor.set_option(tcp::acceptor::reuse_address(true));
    acceptor.listen();
    trap_signal();
    accept_socket();
    connect_to_peers();
    cycle();
    worker = std::thread([this]() { ctx.run(); });
}

Client::~Client() {
    // context, you can stop now!
    if (worker.joinable()) {
        worker.join();
    }
}

void Client::trap_signal() {
    asio::signal_set sigs(ctx, SIGINT, SIGTERM);

    sigs.async_wait([&](asio::error_code ec, int signal) {
        if (!ec) {
            std::cout << "Signal received: " << signal << std::endl;
            acceptor.cancel();
            timer.cancel();
        } else {
            std::cout << std::endl << ec.message() << std::endl;
        }
    });
}

void Client::accept_socket() {
    acceptor.async_accept([&](asio::error_code const &ec, tcp::socket socket) {
        if (!ec) {
            std::cout << "New session: " << socket.remote_endpoint()
                      << std::endl;
            auto ptr = std::make_shared<tcp::socket>(std::move(socket));
            add_to_peers(std::shared_ptr<tcp::socket>(ptr));
            // connection is established, now we can wait
            // for messages from that socket
            start_reading(std::shared_ptr<tcp::socket>(ptr));
            accept_socket();
        } else {
            std::cout << ec.message() << std::endl;
        }
    });
}

void Client::add_to_peers(std::shared_ptr<tcp::socket> socket) {
    peers[current_id] = socket;
    reverse[socket] = current_id;
    std::cout << "Added to peers with id " << current_id << std::endl;
    current_id++;
}

void Client::connect_to_peers() {
    for (auto &name : names) {
        connect_to_peer(std::get<0>(name), std::get<1>(name));
    }
}

void Client::connect_to_peer(std::string &host, std::string &service) {
    add_to_peers(std::make_shared<tcp::socket>(ctx));
    auto &p = peers[current_id - 1];
    auto endpoints = resolver.resolve(host, service);
    asio::async_connect(*p, endpoints,
                        [&, spec = (host + ":"s + service)](
                            asio::error_code ec, tcp::endpoint endpoint) {
                            std::cout << "For: " << spec << " " << ec.message();
                            if (!ec) {
                                std::cout << " " << p->remote_endpoint();
                                // connection is established, now we can wait
                                // for messages from that socket
                                start_reading(p);
                            } else {
                                peers.erase(peers.find(current_id - 1));
                            }
                            std::cout << std::endl;
                        });
}

void Client::start_reading(std::shared_ptr<tcp::socket> socket) {
    // read the header first -- the lucky thing is that the header has fixed
    // size
    asio::async_read(*socket,
                     asio::buffer(&temp_msg.header, sizeof(MessageHeader)),
                     [this, socket](asio::error_code ec, std::size_t len) {
                         if (ec) {
                             std::cout
                                 << "[READ HEADER] Cannot read from peer: "
                                 << ec.message() << std::endl;
                             remove_socket(socket);
                             return;
                         }
                         // check if the message contains a body, if yes, read
                         // it if not just add the message to the queue
                         if (temp_msg.header.size > 0) {
                             temp_msg.body.resize(temp_msg.header.size);
                             read_body(socket);
                         } else {
                             add_to_incoming(socket);
                         }
                     });
}

void Client::read_body(std::shared_ptr<tcp::socket> socket) {
    // header size field indicates how many bytes the body is
    // we read exactly that many bytes from the socket
    asio::async_read(*socket,
                     asio::buffer(temp_msg.body.data(), temp_msg.body.size()),
                     [this, socket](asio::error_code ec, std::size_t len) {
                         if (ec) {
                             std::cout << "[READ BODY] Cannot read from peer: "
                                       << ec.message() << std::endl;
                             remove_socket(socket);
                             return;
                         }
                         add_to_incoming(socket);
                     });
}

void Client::add_to_incoming(std::shared_ptr<tcp::socket> socket) {
    // find the id of that socket with the reverse map
    auto it = reverse.find(socket);
    if (it == reverse.end()) {
        std::cout << "This socket is not in the peer connections!" << std::endl;
        return;
    }
    in_msgs.push_back(MessageWithOwner{temp_msg, it->second});
    // prime the contxt again to read the next message for that socket
    start_reading(socket);
}

void Client::push_message(peer_id id, const Message &msg) {
    out_msgs.push_back({msg, id});
}

void Client::remove_socket(peer_id id) {
    auto it = peers.find(id);
    if (it != peers.end()) {
        auto rit = reverse.find(it->second);
        peers.erase(it);
        reverse.erase(rit);
        std::cout << "[REMOVE BY ID] Socket " << id << " has been removed."
                  << std::endl;
    } else {
        std::cout << "[REMOVE BY SOCKET] Cannot find this id " << std::endl;
    }
}

void Client::remove_socket(std::shared_ptr<tcp::socket> socket) {
    auto it = reverse.find(socket);
    if (it != reverse.end()) {
        auto rit = peers.find(it->second);
        reverse.erase(it);
        peers.erase(rit);
        std::cout << "[REMOVE BY SOCKET] Socket " << it->second
                  << " has been removed." << std::endl;
    } else {
        std::cout << "[REMOVE BY SOCKET] Cannot find this socket " << std::endl;
    }
}

void Client::cycle() {
    timer.expires_from_now(2s);
    timer.async_wait([&](asio::error_code ec) {
        if (ec) {
            std::cout << "[CYCLE] Something is wrong with the timer: "
                      << ec.message() << std::endl;
            return;
        }

        // clear the in messages array first, if there are messages clear them
        while (!in_msgs.empty()) {
            auto msg = in_msgs.pop_front();
            handle_message(msg);
        }
        // now we clear the out messages array
        start_writing();
        cycle();
    });
}

void Client::start_writing() {
    // base case
    if (out_msgs.empty()) {
        return;
    }
    asio::error_code ec2;
    auto it = peers.find(out_msgs.front().id);
    if (it == peers.end()) {
        std::cout << "[START WRITING] The output message has invalid peer id."
                  << std::endl;
        // remove the invalid message
        out_msgs.pop_front();
        start_writing();
        return;
    }
    auto &socket = it->second;
    auto remote = socket->remote_endpoint(ec2);
    if (ec2) {
        std::cout << "[START WRITING] Removed a peer from the peers "
                     "collection as "
                     "it is down"
                  << std::endl;
        remove_socket(out_msgs.front().id);
        // this message cannot be sent, remove it
        out_msgs.pop_front();
        start_writing();
        return;
    }
    asio::async_write(
        *socket,
        asio::buffer(&out_msgs.front().msg.header, sizeof(MessageHeader)),
        [this, socket](asio::error_code ec, size_t len) {
            if (!ec) {
                if (out_msgs.front().msg.header.size > 0) {
                    write_body(socket);
                    return;
                }
                // this is just for debugging, remove it later
                std::cout << "[START WRITING] Finish writing header that is "
                          << len << " bytes " << std::endl;
                // this time don't have to re-prime
            } else {
                // should I remove or not?
                std::cout << "[START WRITING] Cannot write header to socket, "
                             "removing connection."
                          << std::endl;
                remove_socket(out_msgs.front().id);
            }
            // no matter what, remove the message if it is not removed by
            // write_body
            out_msgs.pop_front();
            start_writing();
        });
}

void Client::write_body(std::shared_ptr<tcp::socket> socket) {
    asio::async_write(
        *socket,
        asio::buffer(out_msgs.front().msg.body.data(),
                     sizeof(out_msgs.front().msg.body.size())),
        [this, socket](asio::error_code ec, size_t len) {
            if (ec) {
                // should I remove or not?
                std::cout << "[WRITING BODY] Cannot write body to socket, "
                             "removing connection."
                          << std::endl;
                remove_socket(out_msgs.front().id);
            } else {
                // this is just for debugging, remove it later
                std::cout << "[WRITING BODY] Finish writing body that is "
                          << len << " bytes " << std::endl;
            }
            // no matter what happens, remove that message
            out_msgs.pop_front();
            start_writing();
        });
}

void Client::handle_message(MessageWithOwner &t) {
    // toy example, sending pings and pongs
    if (t.msg.header.type == MessageType::PING) {
        std::cout << "received PING from (" << t.id << ")" << std::endl;
        Message resp(MessageType::PONG);
        push_message(t.id, resp);
    } else if (t.msg.header.type == MessageType::PONG) {
        std::cout << "received PONG from (" << t.id << ")" << std::endl;
        Message resp(MessageType::PING);
        push_message(t.id, resp);
    }
}
