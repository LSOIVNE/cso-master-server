#include "serverconsole.h"
#include "tcp_server.h"
#include "usermanager.h"
#include "udp_server.h"
#include "noticemanager.h"
#include "packet_umsgmanager.h"
#include <iostream>
#include <sstream>
#include <mutex>
#include <windows.h>

ServerConsole serverConsole;
static mutex consoleMutex;

ServerConsole::ServerConsole() {
	_running = false;
	_currentTime = 0;
	_currentTimeStr = "";
	_secondCount = 0;
	_consoleOutput = INVALID_HANDLE_VALUE;
	_logFile = "";
}

ServerConsole::~ServerConsole() {
	shutdown();
}

bool ServerConsole::Init() {
	SetConsoleOutputCP(CP_UTF8);
	SetConsoleCP(CP_UTF8);

	time(&_currentTime);

	char time_buf[28];
	struct tm ts;
	ts = *localtime(&_currentTime);

	strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &ts);
	_currentTimeStr = time_buf;

	strftime(time_buf, sizeof(time_buf), "%Y%m%d_%H%M%S", &ts);
	_logFile = format("logs/serverlog_{}.log", time_buf);

	_consoleOutput = GetStdHandle(STD_OUTPUT_HANDLE);
	if (_consoleOutput == INVALID_HANDLE_VALUE) {
		Print(PrefixType::Error, "[ ServerConsole ] Failed to get console output handle!\n");
		return false;
	}

	return true;
}

void ServerConsole::Start() {
	if (_serverConsoleThread.joinable()) {
		Print(PrefixType::Warn, "[ ServerConsole ] Thread is already running!\n");
		return;
	}

	Print(PrefixType::Info, "[ ServerConsole ] Starting!\n");

	_serverConsoleThread = thread(&ServerConsole::run, this);
}

void ServerConsole::Stop() {
	if (!_serverConsoleThread.joinable()) {
		Print(PrefixType::Warn, "[ ServerConsole ] Thread is already shut down!\n");
		return;
	}

	shutdown();
}

void ServerConsole::Log(PrefixType prefixType, const string& text) {
	string prefix;

	switch (prefixType) {
		case Info: {
			prefix = INFO_PREFIX;
			break;
		}
		case Warn: {
			prefix = WARN_PREFIX;
			break;
		}
		case Error: {
			prefix = ERROR_PREFIX;
			break;
		}
		case Fatal: {
			prefix = FATAL_PREFIX;
			break;
		}
		case Debug: {
			prefix = DEBUG_PREFIX;
			break;
		}
		default: {
			break;
		}
	}

	log(format("[ {} ]{}{}", _currentTimeStr, prefix, text));
}

void ServerConsole::Print(PrefixType prefixType, const string& text, bool logToFile) {
	string output;
	{
		lock_guard<mutex> lock(consoleMutex);

		string prefix;

		switch (prefixType) {
			case Info: {
				SetConsoleTextAttribute(_consoleOutput, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
				prefix = INFO_PREFIX;
				break;
			}
			case Warn: {
				SetConsoleTextAttribute(_consoleOutput, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY);
				prefix = WARN_PREFIX;
				break;
			}
			case Error: {
				SetConsoleTextAttribute(_consoleOutput, FOREGROUND_RED | FOREGROUND_INTENSITY);
				prefix = ERROR_PREFIX;
				break;
			}
			case Fatal: {
				SetConsoleTextAttribute(_consoleOutput, FOREGROUND_RED);
				prefix = FATAL_PREFIX;
				break;
			}
			case Debug: {
				SetConsoleTextAttribute(_consoleOutput, FOREGROUND_INTENSITY);
				prefix = DEBUG_PREFIX;
				break;
			}
			default: {
				SetConsoleTextAttribute(_consoleOutput, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
				break;
			}
		}

		output = format("[ {} ]{}{}", _currentTimeStr, prefix, text);

		cout << output;

		SetConsoleTextAttribute(_consoleOutput, FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY);
	}

	if (logToFile) {
		log(output);
	}
}

void ServerConsole::StartRead() {
	string command;
	while (getline(cin >> ws, command)) {
		if (command == "stop") {
			break;
		}
		else if (command == "status") {
			Print(PrefixType::Info, format("[ ServerConsole ] Connected clients: {}, connected users: {}\n", tcpServer.GetConnections().size(), userManager.GetUsers().size()));
			continue;
		}
		else if (command == "users") {
			string usersList;
			vector<User*> users = userManager.GetUsers();

			users.erase(remove_if(users.begin(), users.end(), [](User* user) {
				return (user == NULL);
			}), users.end());

			Print(PrefixType::Info, format("[ ServerConsole ] Connected users: {}\n", users.size()));
			Print(PrefixType::Info, "[ ServerConsole ] UserID, UserName, IP, Status\n");

			for (auto& user : users) {
				Print(PrefixType::Info, format("[ ServerConsole ] {}, {}, {}, {}\n", user->GetUserID(), user->GetUserName(), user->GetUserIPAddress(), (unsigned char)user->GetUserStatus()));
			}
			continue;
		}
		else if (command.rfind("notice ", 0) == 0) {
			string text = command.substr(7);
			packet_UMsgManager.BroadcastPublicNotice(text);
			Print(PrefixType::Info, format("[ ServerConsole ] Broadcast notice: {}\n", text));
			continue;
		}
		else if (command.rfind("yell ", 0) == 0) {
			string text = command.substr(5);
			packet_UMsgManager.BroadcastServerYell("[系統廣播]", text);
			Print(PrefixType::Info, format("[ ServerConsole ] Broadcast yell: {}\n", text));
			continue;
		}
		else if (command.rfind("msg ", 0) == 0) {
			string text = command.substr(4);
			packet_UMsgManager.BroadcastServerMessage(text);
			Print(PrefixType::Info, format("[ ServerConsole ] Broadcast message: {}\n", text));
			continue;
		}
		else if (command.rfind("banner ", 0) == 0) {
			string text = command.substr(7);
			packet_UMsgManager.BroadcastRollingBanner(text);
			Print(PrefixType::Info, format("[ ServerConsole ] Broadcast rolling banner: {}\n", text));
			continue;
		}
		else if (command == "reloadnotice") {
			noticeManager.Reload();
			Print(PrefixType::Info, "[ ServerConsole ] Reloaded notice configuration.\n");
			continue;
		}
		else {
			Print(PrefixType::Info, "[ ServerConsole ] Available commands: stop, status, users, notice <text>, yell <text>, msg <text>, banner <text>, reloadnotice\n");
			continue;
		}
	}
}

int ServerConsole::run() {
	Print(PrefixType::Info, "[ ServerConsole ] Thread starting!\n");

	try {
		_running = true;
		while (_running) {
			this_thread::sleep_for(1s);

			onSecondTick();
		}
	}
	catch (exception& e) {
		Print(PrefixType::Error, format("[ ServerConsole ] Error on run: {}\n", e.what()));
		return -1;
	}

	Print(PrefixType::Info, "[ ServerConsole ] Thread shutting down!\n");
	return 0;
}

int ServerConsole::shutdown() {
	try {
		if (_serverConsoleThread.joinable()) {
			Print(PrefixType::Info, "[ ServerConsole ] Shutting down!\n");

			_running = false;
			_serverConsoleThread.detach();
		}
	}
	catch (exception& e) {
		Print(PrefixType::Error, format("[ ServerConsole ] Error on shutdown: {}\n", e.what()));
		return -1;
	}

	return 0;
}

void ServerConsole::onSecondTick() {
	time(&_currentTime);

	static char time_buf[28];
	static struct tm ts;
	ts = *localtime(&_currentTime);

	strftime(time_buf, sizeof(time_buf), "%Y-%m-%d %H:%M:%S", &ts);
	_currentTimeStr = time_buf;

	udpServer.OnSecondTick();
	noticeManager.OnSecondTick();

	_secondCount++;

	if (_secondCount == 60) {
		onMinuteTick();
		_secondCount = 0;
	}
}

void ServerConsole::onMinuteTick() {
	userManager.OnMinuteTick();
}

void ServerConsole::log(const string& text) {
	CreateDirectoryA("logs", NULL);

	FILE* file = fopen(_logFile.c_str(), "a");
	if (!file) {
		Print(PrefixType::Warn, format("[ ServerConsole ] Can't open {} to write server log!\n", _logFile), false);
		return;
	}

	fwrite(text.c_str(), text.size(), 1, file);
	fclose(file);
}

std::string Utf8ToCp950(const std::string& utf8Str) {
	if (utf8Str.empty()) return "";
	int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), NULL, 0);
	if (wlen <= 0) return utf8Str;
	std::wstring wstr(wlen, 0);
	MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), &wstr[0], wlen);
	int mblen = WideCharToMultiByte(950, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
	if (mblen <= 0) return utf8Str;
	std::string result(mblen, 0);
	WideCharToMultiByte(950, 0, wstr.data(), (int)wstr.size(), &result[0], mblen, NULL, NULL);
	return result;
}

std::string Cp950ToUtf8(const std::string& cp950Str) {
	if (cp950Str.empty()) return "";
	int wlen = MultiByteToWideChar(950, 0, cp950Str.data(), (int)cp950Str.size(), NULL, 0);
	if (wlen <= 0) return cp950Str;
	std::wstring wstr(wlen, 0);
	MultiByteToWideChar(950, 0, cp950Str.data(), (int)cp950Str.size(), &wstr[0], wlen);
	int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
	if (u8len <= 0) return cp950Str;
	std::string result(u8len, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], u8len, NULL, NULL);
	return result;
}