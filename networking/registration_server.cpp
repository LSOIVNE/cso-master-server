#include "registration_server.h"
#include "databasemanager.h"
#include "serverconsole.h"
#include "serverconfig.h"
#include <boost/algorithm/string.hpp>

RegistrationServer registrationServer;

RegistrationServer::RegistrationServer()
	: _sslContext(boost::asio::ssl::context::sslv23) {
}

RegistrationServer::~RegistrationServer() {
	Stop();
}

bool RegistrationServer::Init(unsigned short port) {
	try {
		_port = port;
		_acceptor = std::make_unique<boost::asio::ip::tcp::acceptor>(_ioContext, boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), _port));

		if (serverConfig.ssl) {
			_sslContext.set_options(
				boost::asio::ssl::context::default_workarounds
				| boost::asio::ssl::context::no_sslv2
				| boost::asio::ssl::context::single_dh_use);
			_sslContext.set_password_callback([](std::size_t, boost::asio::ssl::context_base::password_purpose) {
				return "-?H2byVtl";
			});
			_sslContext.use_certificate_chain_file("certs\\cert.pem");
			_sslContext.use_private_key_file("certs\\key.pem", boost::asio::ssl::context::pem);
			_sslContext.use_tmp_dh_file("certs\\dh2048.pem");
		}
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ RegistrationServer ] Error on Init: {}\n", e.what()));
		return false;
	}
	return true;
}

void RegistrationServer::Start() {
	if (_running) return;
	_running = true;
	startAccept();
	_serverThread = std::thread(&RegistrationServer::run, this);
	serverConsole.Print(PrefixType::Info, format("[ RegistrationServer ] Started on port {} (SSL: {})!\n", _port, serverConfig.ssl ? "Enabled" : "Disabled"));
}

void RegistrationServer::Stop() {
	if (!_running) return;
	_running = false;
	try {
		if (_acceptor && _acceptor->is_open()) {
			boost::system::error_code ec;
			_acceptor->close(ec);
		}
		_ioContext.stop();
		if (_serverThread.joinable()) {
			_serverThread.join();
		}
	}
	catch (...) {}
}

void RegistrationServer::run() {
	try {
		_ioContext.run();
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ RegistrationServer ] Error on run: {}\n", e.what()));
	}
}

void RegistrationServer::startAccept() {
	if (!_running || !_acceptor) return;

	if (serverConfig.ssl) {
		auto socket = std::make_shared<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>>(_ioContext, _sslContext);
		_acceptor->async_accept(socket->next_layer(), [this, socket](const boost::system::error_code& ec) {
			if (!ec) {
				socket->async_handshake(boost::asio::ssl::stream_base::server, [this, socket](const boost::system::error_code& hec) {
					if (!hec) {
						handleSSLConnection(socket);
					}
					else {
						serverConsole.Print(PrefixType::Warn, format("[ RegistrationServer ] SSL handshake failed: {}\n", hec.message()));
						boost::system::error_code sec;
						socket->next_layer().close(sec);
					}
				});
			}
			if (_running) {
				startAccept();
			}
		});
	}
	else {
		auto socket = std::make_shared<boost::asio::ip::tcp::socket>(_ioContext);
		_acceptor->async_accept(*socket, [this, socket](const boost::system::error_code& ec) {
			if (!ec) {
				handlePlainConnection(socket);
			}
			if (_running) {
				startAccept();
			}
		});
	}
}

std::string RegistrationServer::processRequest(const std::string& req) {
	std::string reply;

	// 1. Plain text protocol: "REGISTER <username> <password>"
	if (boost::starts_with(req, "REGISTER ")) {
		std::string line = req.substr(9);
		boost::trim(line);
		size_t sp = line.find(' ');
		if (sp != std::string::npos) {
			std::string user = line.substr(0, sp);
			std::string pass = line.substr(sp + 1);
			boost::trim(user);
			boost::trim(pass);

			std::string errorMsg;
			int res = databaseManager.RegisterUser(user, pass, errorMsg);
			if (res == 0) {
				reply = "OK:帳號註冊成功！\n請點擊「開始遊戲」按鈕登入。\n";
			}
			else {
				reply = "ERR:" + errorMsg + "\n";
			}
		}
		else {
			reply = "ERR:請輸入帳號與密碼！\n";
		}
	}
	// 2. HTTP GET /register?user=...&pass=...
	else if (boost::starts_with(req, "GET /register?") || boost::starts_with(req, "GET /register ")) {
		size_t qpos = req.find('?');
		size_t endpos = req.find(' ', qpos);
		std::string query = (qpos != std::string::npos && endpos != std::string::npos) ? req.substr(qpos + 1, endpos - qpos - 1) : "";
		std::string user, pass;
		std::vector<std::string> params;
		boost::split(params, query, boost::is_any_of("&"));
		for (auto& p : params) {
			size_t eq = p.find('=');
			if (eq != std::string::npos) {
				std::string k = p.substr(0, eq);
				std::string v = p.substr(eq + 1);
				if (k == "user" || k == "username") user = v;
				if (k == "pass" || k == "password") pass = v;
			}
		}

		std::string errorMsg;
		int res = (!user.empty() && !pass.empty()) ? databaseManager.RegisterUser(user, pass, errorMsg) : 2;
		if (user.empty() || pass.empty()) errorMsg = "請輸入帳號與密碼！";

		std::string body = format("{{\"status\":{},\"message\":\"{}\"}}", res, res == 0 ? "帳號註冊成功！" : errorMsg);
		reply = "HTTP/1.1 200 OK\r\nContent-Type: application/json; charset=utf-8\r\nAccess-Control-Allow-Origin: *\r\nContent-Length: " + std::to_string(body.size()) + "\r\n\r\n" + body;
	}
	else {
		reply = "ERR:無效的請求格式！\n";
	}

	return reply;
}

void RegistrationServer::handleSSLConnection(std::shared_ptr<boost::asio::ssl::stream<boost::asio::ip::tcp::socket>> socket) {
	auto buffer = std::make_shared<std::vector<char>>(1024);
	socket->async_read_some(boost::asio::buffer(*buffer), [this, socket, buffer](const boost::system::error_code& rec, size_t bytesTransferred) {
		if (!rec && bytesTransferred > 0) {
			std::string req(buffer->data(), bytesTransferred);
			std::string reply = processRequest(req);

			auto replyBuf = std::make_shared<std::string>(reply);
			boost::asio::async_write(*socket, boost::asio::buffer(*replyBuf), [socket, replyBuf](const boost::system::error_code&, size_t) {
				boost::system::error_code sec;
				socket->shutdown(sec);
				socket->next_layer().close(sec);
			});
		}
	});
}

void RegistrationServer::handlePlainConnection(std::shared_ptr<boost::asio::ip::tcp::socket> socket) {
	auto buffer = std::make_shared<std::vector<char>>(1024);
	socket->async_read_some(boost::asio::buffer(*buffer), [this, socket, buffer](const boost::system::error_code& rec, size_t bytesTransferred) {
		if (!rec && bytesTransferred > 0) {
			std::string req(buffer->data(), bytesTransferred);
			std::string reply = processRequest(req);

			auto replyBuf = std::make_shared<std::string>(reply);
			boost::asio::async_write(*socket, boost::asio::buffer(*replyBuf), [socket, replyBuf](const boost::system::error_code&, size_t) {
				boost::system::error_code sec;
				socket->shutdown(boost::asio::ip::tcp::socket::shutdown_both, sec);
				socket->close(sec);
			});
		}
	});
}
