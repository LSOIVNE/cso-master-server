#pragma once
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
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
	void handlePlainConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket);
	void handleSSLConnection(std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket);
	std::string processRequest(const std::string& req);

	unsigned short _port = 0;
	std::atomic<bool> _running{ false };
	std::thread _serverThread;
	boost::asio::io_context _ioContext;
	boost::asio::ssl::context _sslContext;
	std::unique_ptr<boost::asio::ip::tcp::acceptor> _acceptor;
};

extern RegistrationServer registrationServer;
