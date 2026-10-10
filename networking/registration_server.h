#pragma once
#include <boost/asio.hpp>
#include <thread>
#include <atomic>
#include <string>
#include <memory>

class RegistrationServer {
public:
	RegistrationServer();
	~RegistrationServer();

	bool Init(unsigned short port);
	void Start();
	void Stop();

private:
	void run();
	void startAccept();

	unsigned short _port = 0;
	std::atomic<bool> _running{ false };
	std::thread _serverThread;
	boost::asio::io_context _ioContext;
	std::unique_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
};

extern RegistrationServer registrationServer;
