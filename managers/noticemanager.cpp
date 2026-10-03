#include "noticemanager.h"
#include "packet_umsgmanager.h"
#include "serverconsole.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

NoticeManager noticeManager;

static const string NOTICE_CONFIG_PATH = "configs/notice.json";

NoticeManager::NoticeManager() {
	_secondCounter = 0;
	_currentMessageIndex = 0;
}

bool NoticeManager::Init() {
	serverConsole.Print(PrefixType::Info, "[ NoticeManager ] Initializing NoticeManager...\n");
	return LoadConfig();
}

void NoticeManager::saveDefaultConfig(const string& path) {
	std::filesystem::create_directories("configs");

	json j;
	j["WelcomeNotice"] = {
		{ "Enabled", _config.welcomeEnabled },
		{ "Type", _config.welcomeType },
		{ "Message", _config.welcomeMessage }
	};
	j["PeriodicAnnouncements"] = {
		{ "Enabled", _config.periodicEnabled },
		{ "IntervalSeconds", _config.periodicIntervalSeconds },
		{ "Type", _config.periodicType },
		{ "Messages", _config.periodicMessages }
	};

	ofstream file(path);
	if (file.is_open()) {
		file << j.dump(4);
		file.close();
		serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Created default config file: {}\n", path));
	}
	else {
		serverConsole.Print(PrefixType::Error, format("[ NoticeManager ] Failed to create default config file: {}\n", path));
	}
}

bool NoticeManager::LoadConfig() {
	if (!std::filesystem::exists(NOTICE_CONFIG_PATH)) {
		serverConsole.Print(PrefixType::Warn, format("[ NoticeManager ] Config file {} not found. Generating default...\n", NOTICE_CONFIG_PATH));
		saveDefaultConfig(NOTICE_CONFIG_PATH);
		return true;
	}

	ifstream file(NOTICE_CONFIG_PATH);
	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, format("[ NoticeManager ] Failed to open {}!\n", NOTICE_CONFIG_PATH));
		return false;
	}

	try {
		json j;
		file >> j;

		if (j.contains("WelcomeNotice") && j["WelcomeNotice"].is_object()) {
			auto& wn = j["WelcomeNotice"];
			if (wn.contains("Enabled") && wn["Enabled"].is_boolean()) {
				_config.welcomeEnabled = wn["Enabled"].get<bool>();
			}
			if (wn.contains("Type") && wn["Type"].is_string()) {
				_config.welcomeType = wn["Type"].get<string>();
			}
			if (wn.contains("Message") && wn["Message"].is_string()) {
				_config.welcomeMessage = wn["Message"].get<string>();
			}
		}

		if (j.contains("PeriodicAnnouncements") && j["PeriodicAnnouncements"].is_object()) {
			auto& pa = j["PeriodicAnnouncements"];
			if (pa.contains("Enabled") && pa["Enabled"].is_boolean()) {
				_config.periodicEnabled = pa["Enabled"].get<bool>();
			}
			if (pa.contains("IntervalSeconds") && pa["IntervalSeconds"].is_number()) {
				_config.periodicIntervalSeconds = pa["IntervalSeconds"].get<unsigned int>();
				if (_config.periodicIntervalSeconds < 10) {
					_config.periodicIntervalSeconds = 10;
				}
			}
			if (pa.contains("Type") && pa["Type"].is_string()) {
				_config.periodicType = pa["Type"].get<string>();
			}
			if (pa.contains("Messages") && pa["Messages"].is_array()) {
				_config.periodicMessages.clear();
				for (const auto& item : pa["Messages"]) {
					if (item.is_string() && !item.get<string>().empty()) {
						_config.periodicMessages.push_back(item.get<string>());
					}
				}
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Config loaded: Welcome={}, Periodic={} (interval: {}s, messages: {})\n",
			_config.welcomeEnabled ? "ON" : "OFF",
			_config.periodicEnabled ? "ON" : "OFF",
			_config.periodicIntervalSeconds,
			_config.periodicMessages.size()));

		return true;
	}
	catch (const exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ NoticeManager ] JSON parse error in {}: {}\n", NOTICE_CONFIG_PATH, e.what()));
		return false;
	}
}

void NoticeManager::Reload() {
	serverConsole.Print(PrefixType::Info, "[ NoticeManager ] Reloading config...\n");
	_secondCounter = 0;
	_currentMessageIndex = 0;
	LoadConfig();
}

void NoticeManager::OnSecondTick() {
	if (!_config.periodicEnabled || _config.periodicMessages.empty()) {
		return;
	}

	_secondCounter++;
	if (_secondCounter < _config.periodicIntervalSeconds) {
		return;
	}

	_secondCounter = 0;

	if (_currentMessageIndex >= _config.periodicMessages.size()) {
		_currentMessageIndex = 0;
	}

	const string& msg = _config.periodicMessages[_currentMessageIndex];
	_currentMessageIndex = (_currentMessageIndex + 1) % _config.periodicMessages.size();

	if (_config.periodicType == "RollingBanner") {
		packet_UMsgManager.BroadcastRollingBanner(msg);
	}
	else if (_config.periodicType == "ServerYell") {
		packet_UMsgManager.BroadcastServerYell("系統", msg);
	}
	else if (_config.periodicType == "ServerMessage") {
		packet_UMsgManager.BroadcastServerMessage(msg);
	}
	else {
		// Default: PublicNotice
		packet_UMsgManager.BroadcastPublicNotice(msg);
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Periodic announcement broadcast: {}\n", msg));
}

void NoticeManager::SendWelcomeNotice(User* user) {
	if (user == NULL || user->GetConnection() == NULL) {
		return;
	}

	if (!_config.welcomeEnabled || _config.welcomeMessage.empty()) {
		return;
	}

	if (_config.welcomeType == "ServerMessage") {
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(user->GetConnection(), _config.welcomeMessage);
	}
	else if (_config.welcomeType == "ServerYell") {
		packet_UMsgManager.SendPacket_UMsg_ServerYell(user->GetConnection(), "系統", _config.welcomeMessage);
	}
	else if (_config.welcomeType == "RollingBanner") {
		packet_UMsgManager.SendPacket_UMsg_RollingBanner(user->GetConnection(), _config.welcomeMessage);
	}
	else {
		// Default: PublicNotice
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(user->GetConnection(), _config.welcomeMessage);
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Welcome notice sent to user ({})\n", user->GetUserLogName()));
}
