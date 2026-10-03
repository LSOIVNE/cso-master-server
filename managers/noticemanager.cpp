#include "noticemanager.h"
#include "packet_umsgmanager.h"
#include "usermanager.h"
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
	j["ServerBulletin"] = {
		{ "Enabled", _config.serverBulletin.enabled },
		{ "NoticeID", _config.serverBulletin.noticeID },
		{ "Category", _config.serverBulletin.category },
		{ "Title", _config.serverBulletin.title },
		{ "Content", _config.serverBulletin.content },
		{ "SendOnLogin", _config.serverBulletin.sendOnLogin }
	};

	j["WelcomeNotice"] = {
		{ "Enabled", _config.welcomeNotice.enabled },
		{ "Type", _config.welcomeNotice.type },
		{ "Message", _config.welcomeNotice.message }
	};

	j["PeriodicAnnouncements"] = {
		{ "Enabled", _config.periodicAnnouncements.enabled },
		{ "IntervalSeconds", _config.periodicAnnouncements.intervalSeconds },
		{ "Type", _config.periodicAnnouncements.type },
		{ "Messages", _config.periodicAnnouncements.messages }
	};

	ofstream file(path);
	if (file.is_open()) {
		file << j.dump(4);
		serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Created default config at {}\n", path));
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

		// 1. Server Bulletin (Client top "公告" tab in lobby/serverlist)
		if (j.contains("ServerBulletin") && j["ServerBulletin"].is_object()) {
			auto& sb = j["ServerBulletin"];
			if (sb.contains("Enabled") && sb["Enabled"].is_boolean()) {
				_config.serverBulletin.enabled = sb["Enabled"].get<bool>();
			}
			if (sb.contains("NoticeID") && sb["NoticeID"].is_number()) {
				_config.serverBulletin.noticeID = sb["NoticeID"].get<unsigned short>();
			}
			if (sb.contains("Category") && sb["Category"].is_number()) {
				_config.serverBulletin.category = sb["Category"].get<unsigned char>();
			}
			if (sb.contains("Title") && sb["Title"].is_string()) {
				_config.serverBulletin.title = sb["Title"].get<string>();
			}
			if (sb.contains("Content") && sb["Content"].is_string()) {
				_config.serverBulletin.content = sb["Content"].get<string>();
			}
			if (sb.contains("SendOnLogin") && sb["SendOnLogin"].is_boolean()) {
				_config.serverBulletin.sendOnLogin = sb["SendOnLogin"].get<bool>();
			}
		}

		// 2. Welcome Notice (sent upon entering a channel)
		if (j.contains("WelcomeNotice") && j["WelcomeNotice"].is_object()) {
			auto& wn = j["WelcomeNotice"];
			if (wn.contains("Enabled") && wn["Enabled"].is_boolean()) {
				_config.welcomeNotice.enabled = wn["Enabled"].get<bool>();
			}
			if (wn.contains("Type") && wn["Type"].is_string()) {
				string typeStr = wn["Type"].get<string>();
				// If legacy type was "Bulletin", convert to "ServerMessage" to prevent channel-entry popup conflict
				if (typeStr == "Bulletin") {
					_config.welcomeNotice.type = "ServerMessage";
				}
				else {
					_config.welcomeNotice.type = typeStr;
				}
			}
			if (wn.contains("Message") && wn["Message"].is_string()) {
				_config.welcomeNotice.message = wn["Message"].get<string>();
			}
			// Legacy fallback: if old config had Title/Content inside WelcomeNotice, use them for ServerBulletin if not already set
			if (wn.contains("Title") && wn["Title"].is_string() && !j.contains("ServerBulletin")) {
				_config.serverBulletin.title = wn["Title"].get<string>();
			}
			if (wn.contains("Content") && wn["Content"].is_string() && !j.contains("ServerBulletin")) {
				_config.serverBulletin.content = wn["Content"].get<string>();
			}
		}

		// 3. Periodic Announcements
		if (j.contains("PeriodicAnnouncements") && j["PeriodicAnnouncements"].is_object()) {
			auto& pa = j["PeriodicAnnouncements"];
			if (pa.contains("Enabled") && pa["Enabled"].is_boolean()) {
				_config.periodicAnnouncements.enabled = pa["Enabled"].get<bool>();
			}
			if (pa.contains("IntervalSeconds") && pa["IntervalSeconds"].is_number()) {
				_config.periodicAnnouncements.intervalSeconds = pa["IntervalSeconds"].get<unsigned int>();
				if (_config.periodicAnnouncements.intervalSeconds < 10) {
					_config.periodicAnnouncements.intervalSeconds = 10;
				}
			}
			if (pa.contains("Type") && pa["Type"].is_string()) {
				_config.periodicAnnouncements.type = pa["Type"].get<string>();
			}
			if (pa.contains("Messages") && pa["Messages"].is_array()) {
				_config.periodicAnnouncements.messages.clear();
				for (const auto& item : pa["Messages"]) {
					if (item.is_string() && !item.get<string>().empty()) {
						_config.periodicAnnouncements.messages.push_back(item.get<string>());
					}
				}
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Config loaded! ServerBulletin: {}, WelcomeNotice: {}, PeriodicAnnouncements: {} (Interval: {}s, Count: {})\n",
			_config.serverBulletin.enabled ? "ON" : "OFF",
			_config.welcomeNotice.enabled ? _config.welcomeNotice.type : "OFF",
			_config.periodicAnnouncements.enabled ? "ON" : "OFF",
			_config.periodicAnnouncements.intervalSeconds,
			_config.periodicAnnouncements.messages.size()));

		return true;
	}
	catch (json::parse_error& e) {
		serverConsole.Print(PrefixType::Error, format("[ NoticeManager ] Failed to parse {}: {}\n", NOTICE_CONFIG_PATH, e.what()));
		return false;
	}
}

void NoticeManager::Reload() {
	serverConsole.Print(PrefixType::Info, "[ NoticeManager ] Reloading notice configs...\n");
	LoadConfig();
	if (_config.serverBulletin.enabled) {
		BroadcastServerBulletin();
	}
}

void NoticeManager::OnSecondTick() {
	if (!_config.periodicAnnouncements.enabled || _config.periodicAnnouncements.messages.empty()) {
		return;
	}

	_secondCounter++;
	if (_secondCounter < _config.periodicAnnouncements.intervalSeconds) {
		return;
	}

	_secondCounter = 0;

	if (_currentMessageIndex >= _config.periodicAnnouncements.messages.size()) {
		_currentMessageIndex = 0;
	}

	const string& msg = _config.periodicAnnouncements.messages[_currentMessageIndex];
	_currentMessageIndex = (_currentMessageIndex + 1) % _config.periodicAnnouncements.messages.size();

	if (_config.periodicAnnouncements.type == "Chat") {
		// Non-intrusive: send Chat to lobby/room users, PublicNotice to in-game users
		vector<User*> users = userManager.GetUsers();
		for (auto& u : users) {
			if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u)) {
				if (u->GetUserStatus() == UserStatus::InLobby) {
					packet_UMsgManager.SendPacket_UMsg_Chat(u->GetConnection(), Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(msg));
				}
				else if (u->GetUserStatus() == UserStatus::InGame) {
					packet_UMsgManager.SendPacket_UMsg_PublicNotice(u->GetConnection(), msg);
				}
				else if (u->GetUserStatus() == UserStatus::InRoom) {
					packet_UMsgManager.SendPacket_UMsg_Chat(u->GetConnection(), Packet_UMsgType::RoomChat, Utf8ToCp950("系統"), Utf8ToCp950(msg));
				}
			}
		}
	}
	else if (_config.periodicAnnouncements.type == "RollingBanner") {
		packet_UMsgManager.BroadcastRollingBanner(msg);
	}
	else if (_config.periodicAnnouncements.type == "ServerYell") {
		packet_UMsgManager.BroadcastServerYell("系統", msg);
	}
	else if (_config.periodicAnnouncements.type == "ServerMessage") {
		packet_UMsgManager.BroadcastServerMessage(msg);
	}
	else {
		// Default: PublicNotice
		packet_UMsgManager.BroadcastPublicNotice(msg);
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Periodic announcement broadcast: {}\n", msg));
}

void NoticeManager::SendServerBulletin(User* user) {
	if (user == NULL || user->GetConnection() == NULL) {
		return;
	}

	if (!_config.serverBulletin.enabled || !_config.serverBulletin.sendOnLogin) {
		return;
	}

	packet_UMsgManager.SendPacket_UMsg_BulletinNotice(
		user->GetConnection(),
		_config.serverBulletin.noticeID,
		_config.serverBulletin.category,
		_config.serverBulletin.title,
		_config.serverBulletin.content
	);

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Server bulletin sent to user ({})\n", user->GetUserLogName()));
}

void NoticeManager::BroadcastServerBulletin() {
	if (!_config.serverBulletin.enabled) {
		return;
	}

	packet_UMsgManager.BroadcastBulletinNotice(
		_config.serverBulletin.noticeID,
		_config.serverBulletin.category,
		_config.serverBulletin.title,
		_config.serverBulletin.content
	);

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Server bulletin broadcast to all users: {}\n", _config.serverBulletin.title));
}

void NoticeManager::SendWelcomeNotice(User* user) {
	if (user == NULL || user->GetConnection() == NULL) {
		return;
	}

	if (!_config.welcomeNotice.enabled || _config.welcomeNotice.type == "None") {
		return;
	}

	if (_config.welcomeNotice.type == "ServerMessage") {
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(user->GetConnection(), _config.welcomeNotice.message);
	}
	else if (_config.welcomeNotice.type == "ServerYell") {
		packet_UMsgManager.SendPacket_UMsg_ServerYell(user->GetConnection(), "系統", _config.welcomeNotice.message);
	}
	else if (_config.welcomeNotice.type == "RollingBanner") {
		packet_UMsgManager.SendPacket_UMsg_RollingBanner(user->GetConnection(), _config.welcomeNotice.message);
	}
	else if (_config.welcomeNotice.type == "Chat") {
		packet_UMsgManager.SendPacket_UMsg_Chat(user->GetConnection(), Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(_config.welcomeNotice.message));
	}
	else {
		// Default: PublicNotice
		if (!_config.welcomeNotice.message.empty()) {
			packet_UMsgManager.SendPacket_UMsg_PublicNotice(user->GetConnection(), _config.welcomeNotice.message);
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Welcome notice sent to user ({})\n", user->GetUserLogName()));
}
