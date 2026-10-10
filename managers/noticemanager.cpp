#include "noticemanager.h"
#include "packet_umsgmanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>

using json = nlohmann::json;

NoticeManager noticeManager;

static string GetNoticeConfigPath() {
	return "configs/notice.json";
}

NoticeManager::NoticeManager() {
	_secondCounter = 0;
	_currentMessageIndex = 0;
}

bool NoticeManager::Init() {
	serverConsole.Print(PrefixType::Info, "[ NoticeManager ] Initializing NoticeManager...\n");
	return LoadConfig();
}

bool NoticeManager::SaveConfig(const string& path) {
	string targetPath = path.empty() ? GetNoticeConfigPath() : path;
	std::filesystem::create_directories(std::filesystem::path(targetPath).parent_path());

	json j;
	json bulletinsJson = json::array();
	for (const auto& b : _config.serverBulletin.bulletins) {
		bulletinsJson.push_back({
			{ "NoticeID", b.noticeID },
			{ "Category", b.category },
			{ "Title", b.title },
			{ "Content", b.content }
		});
	}
	j["ServerBulletin"] = {
		{ "Enabled", _config.serverBulletin.enabled },
		{ "SendOnLogin", _config.serverBulletin.sendOnLogin },
		{ "Bulletins", bulletinsJson }
	};

	j["LoginWelcomeNotice"] = {
		{ "Enabled", _config.loginWelcomeNotice.enabled },
		{ "Type", _config.loginWelcomeNotice.type },
		{ "Category", _config.loginWelcomeNotice.category },
		{ "Title", _config.loginWelcomeNotice.title },
		{ "Message", _config.loginWelcomeNotice.message }
	};

	j["ChannelLoginNotice"] = {
		{ "Enabled", _config.channelLoginNotice.enabled },
		{ "Type", _config.channelLoginNotice.type },
		{ "Category", _config.channelLoginNotice.category },
		{ "Title", _config.channelLoginNotice.title },
		{ "Message", _config.channelLoginNotice.message }
	};

	j["ChannelChatNotice"] = {
		{ "Enabled", _config.channelChatNotice.enabled },
		{ "Type", _config.channelChatNotice.type },
		{ "Message", _config.channelChatNotice.message }
	};

	j["PeriodicAnnouncements"] = {
		{ "Enabled", _config.periodicAnnouncements.enabled },
		{ "IntervalSeconds", _config.periodicAnnouncements.intervalSeconds },
		{ "Type", _config.periodicAnnouncements.type },
		{ "Messages", _config.periodicAnnouncements.messages }
	};

	bool success = false;
	ofstream file(targetPath);
	if (file.is_open()) {
		file << j.dump(4);
		serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Saved config to {}\n", targetPath));
		success = true;
	}
	else {
		serverConsole.Print(PrefixType::Error, format("[ NoticeManager ] Failed to save config file: {}\n", targetPath));
	}

	return success;
}

bool NoticeManager::LoadConfig() {
	string configPath = GetNoticeConfigPath();
	if (!std::filesystem::exists(configPath)) {
		serverConsole.Print(PrefixType::Warn, format("[ NoticeManager ] Config file {} not found. Generating default...\n", configPath));
		SaveConfig(configPath);
		return true;
	}

	ifstream file(configPath);
	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, format("[ NoticeManager ] Failed to open {}!\n", configPath));
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
			if (sb.contains("SendOnLogin") && sb["SendOnLogin"].is_boolean()) {
				_config.serverBulletin.sendOnLogin = sb["SendOnLogin"].get<bool>();
			}
			if (sb.contains("Bulletins") && sb["Bulletins"].is_array()) {
				_config.serverBulletin.bulletins.clear();
				for (const auto& item : sb["Bulletins"]) {
					if (item.is_object()) {
						BulletinItem b;
						if (item.contains("NoticeID") && item["NoticeID"].is_number()) {
							b.noticeID = item["NoticeID"].get<unsigned short>();
						}
						if (item.contains("Category") && item["Category"].is_number()) {
							b.category = item["Category"].get<unsigned char>();
						}
						if (item.contains("Title") && item["Title"].is_string()) {
							b.title = item["Title"].get<string>();
						}
						if (item.contains("Content") && item["Content"].is_string()) {
							b.content = item["Content"].get<string>();
						}
						_config.serverBulletin.bulletins.push_back(b);
					}
				}
			}
			else if (sb.contains("Title") && sb["Title"].is_string()) {
				// Legacy single bulletin format compatibility
				BulletinItem b;
				if (sb.contains("NoticeID") && sb["NoticeID"].is_number()) {
					b.noticeID = sb["NoticeID"].get<unsigned short>();
				}
				if (sb.contains("Category") && sb["Category"].is_number()) {
					b.category = sb["Category"].get<unsigned char>();
				}
				b.title = sb["Title"].get<string>();
				if (sb.contains("Content") && sb["Content"].is_string()) {
					b.content = sb["Content"].get<string>();
				}
				_config.serverBulletin.bulletins = { b };
			}
		}

		// 2. Login Welcome Notice (sent upon logging in / entering server list, aligned with 2014 "WelcomeMessage")
		if (j.contains("LoginWelcomeNotice") && j["LoginWelcomeNotice"].is_object()) {
			auto& lwn = j["LoginWelcomeNotice"];
			if (lwn.contains("Enabled") && lwn["Enabled"].is_boolean()) {
				_config.loginWelcomeNotice.enabled = lwn["Enabled"].get<bool>();
			}
			if (lwn.contains("Type") && lwn["Type"].is_string()) {
				_config.loginWelcomeNotice.type = lwn["Type"].get<string>();
			}
			if (lwn.contains("Category") && lwn["Category"].is_number()) {
				_config.loginWelcomeNotice.category = lwn["Category"].get<unsigned char>();
			}
			if (lwn.contains("Title") && lwn["Title"].is_string()) {
				_config.loginWelcomeNotice.title = lwn["Title"].get<string>();
			}
			if (lwn.contains("Message") && lwn["Message"].is_string()) {
				_config.loginWelcomeNotice.message = lwn["Message"].get<string>();
			}
		}
		else if (j.contains("WelcomeMessage")) {
			// Compatibility with 2014 WelcomeMessage format
			if (j["WelcomeMessage"].is_string()) {
				_config.loginWelcomeNotice.message = j["WelcomeMessage"].get<string>();
				_config.loginWelcomeNotice.enabled = true;
				_config.loginWelcomeNotice.type = "Bulletin";
			}
			else if (j["WelcomeMessage"].is_object()) {
				auto& wm = j["WelcomeMessage"];
				if (wm.contains("Enabled") && wm["Enabled"].is_boolean()) {
					_config.loginWelcomeNotice.enabled = wm["Enabled"].get<bool>();
				}
				if (wm.contains("Type") && wm["Type"].is_string()) {
					_config.loginWelcomeNotice.type = wm["Type"].get<string>();
				}
				if (wm.contains("Category") && wm["Category"].is_number()) {
					_config.loginWelcomeNotice.category = wm["Category"].get<unsigned char>();
				}
				if (wm.contains("Title") && wm["Title"].is_string()) {
					_config.loginWelcomeNotice.title = wm["Title"].get<string>();
				}
				if (wm.contains("Message") && wm["Message"].is_string()) {
					_config.loginWelcomeNotice.message = wm["Message"].get<string>();
				}
			}
		}

		// 3. Channel Login Notice (sent upon entering a channel, aligned with 2014 "LoginMsg")
		if (j.contains("ChannelLoginNotice") && j["ChannelLoginNotice"].is_object()) {
			auto& cln = j["ChannelLoginNotice"];
			if (cln.contains("Enabled") && cln["Enabled"].is_boolean()) {
				_config.channelLoginNotice.enabled = cln["Enabled"].get<bool>();
			}
			if (cln.contains("Type") && cln["Type"].is_string()) {
				_config.channelLoginNotice.type = cln["Type"].get<string>();
			}
			if (cln.contains("Category") && cln["Category"].is_number()) {
				_config.channelLoginNotice.category = cln["Category"].get<unsigned char>();
			}
			if (cln.contains("Title") && cln["Title"].is_string()) {
				_config.channelLoginNotice.title = cln["Title"].get<string>();
			}
			if (cln.contains("Message") && cln["Message"].is_string()) {
				_config.channelLoginNotice.message = cln["Message"].get<string>();
			}
		}
		else if (j.contains("LoginMsg")) {
			// Compatibility with 2014 LoginMsg format
			if (j["LoginMsg"].is_string()) {
				_config.channelLoginNotice.message = j["LoginMsg"].get<string>();
				_config.channelLoginNotice.enabled = true;
				_config.channelLoginNotice.type = "PublicNotice";
			}
			else if (j["LoginMsg"].is_object()) {
				auto& lm = j["LoginMsg"];
				if (lm.contains("Enabled") && lm["Enabled"].is_boolean()) {
					_config.channelLoginNotice.enabled = lm["Enabled"].get<bool>();
				}
				if (lm.contains("Type") && lm["Type"].is_string()) {
					_config.channelLoginNotice.type = lm["Type"].get<string>();
				}
				if (lm.contains("Category") && lm["Category"].is_number()) {
					_config.channelLoginNotice.category = lm["Category"].get<unsigned char>();
				}
				if (lm.contains("Title") && lm["Title"].is_string()) {
					_config.channelLoginNotice.title = lm["Title"].get<string>();
				}
				if (lm.contains("Message") && lm["Message"].is_string()) {
					_config.channelLoginNotice.message = lm["Message"].get<string>();
				}
			}
		}
		else if (j.contains("WelcomeNotice") && j["WelcomeNotice"].is_object()) {
			// Legacy fallback: convert WelcomeNotice to ChannelLoginNotice
			auto& wn = j["WelcomeNotice"];
			if (wn.contains("Enabled") && wn["Enabled"].is_boolean()) {
				_config.channelLoginNotice.enabled = wn["Enabled"].get<bool>();
			}
			if (wn.contains("Type") && wn["Type"].is_string()) {
				_config.channelLoginNotice.type = wn["Type"].get<string>();
			}
			if (wn.contains("Title") && wn["Title"].is_string()) {
				_config.channelLoginNotice.title = wn["Title"].get<string>();
			}
			if (wn.contains("Message") && wn["Message"].is_string()) {
				_config.channelLoginNotice.message = wn["Message"].get<string>();
			}
		}

		// 4. Channel Chat Notice (sent to lobby chat upon entering a channel, defaults to ServerMessage [黃色字 伺服器信息])
		if (j.contains("ChannelChatNotice") && j["ChannelChatNotice"].is_object()) {
			auto& ccn = j["ChannelChatNotice"];
			if (ccn.contains("Enabled") && ccn["Enabled"].is_boolean()) {
				_config.channelChatNotice.enabled = ccn["Enabled"].get<bool>();
			}
			if (ccn.contains("Type") && ccn["Type"].is_string()) {
				_config.channelChatNotice.type = ccn["Type"].get<string>();
			}
			if (ccn.contains("Message") && ccn["Message"].is_string()) {
				_config.channelChatNotice.message = ccn["Message"].get<string>();
			}
		}
		else if (j.contains("ChannelLoginNotice") && j["ChannelLoginNotice"].is_object()) {
			// Backward compatibility: if SendToChat or ChatMessage was present in ChannelLoginNotice
			auto& cln = j["ChannelLoginNotice"];
			if (cln.contains("SendToChat") && cln["SendToChat"].is_boolean()) {
				_config.channelChatNotice.enabled = cln["SendToChat"].get<bool>();
			}
			if (cln.contains("ChatMessage") && cln["ChatMessage"].is_string()) {
				_config.channelChatNotice.message = cln["ChatMessage"].get<string>();
			}
		}

		// 5. Periodic Announcements
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

		serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Config loaded from {}! ServerBulletin: {}, LoginWelcome: {}, ChannelLogin: {}, ChannelChat: {}, PeriodicAnnouncements: {} (Interval: {}s, Count: {})\n",
			configPath,
			_config.serverBulletin.enabled ? format("ON ({} bulletins)", _config.serverBulletin.bulletins.size()) : "OFF",
			_config.loginWelcomeNotice.enabled ? _config.loginWelcomeNotice.type : "OFF",
			_config.channelLoginNotice.enabled ? _config.channelLoginNotice.type : "OFF",
			_config.channelChatNotice.enabled ? _config.channelChatNotice.type : "OFF",
			_config.periodicAnnouncements.enabled ? "ON" : "OFF",
			_config.periodicAnnouncements.intervalSeconds,
			_config.periodicAnnouncements.messages.size()));

		return true;
	}
	catch (json::parse_error& e) {
		serverConsole.Print(PrefixType::Error, format("[ NoticeManager ] Failed to parse {}: {}\n", configPath, e.what()));
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

	for (const auto& b : _config.serverBulletin.bulletins) {
		packet_UMsgManager.SendPacket_UMsg_BulletinNotice(
			user->GetConnection(),
			b.noticeID,
			b.category,
			b.title,
			b.content
		);
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Sent {} server bulletin(s) to user ({})\n", _config.serverBulletin.bulletins.size(), user->GetUserLogName()));
}

void NoticeManager::BroadcastServerBulletin() {
	if (!_config.serverBulletin.enabled) {
		return;
	}

	for (const auto& b : _config.serverBulletin.bulletins) {
		packet_UMsgManager.BroadcastBulletinNotice(
			b.noticeID,
			b.category,
			b.title,
			b.content
		);
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Broadcast {} server bulletin(s) to all users\n", _config.serverBulletin.bulletins.size()));
}

void NoticeManager::SendLoginWelcomeNotice(User* user) {
	if (user == NULL || user->GetConnection() == NULL) {
		return;
	}

	if (!_config.loginWelcomeNotice.enabled || _config.loginWelcomeNotice.type == "None") {
		return;
	}

	const string& type = _config.loginWelcomeNotice.type;
	const string& msg = _config.loginWelcomeNotice.message;
	const string& title = _config.loginWelcomeNotice.title.empty() ? "伺服器公告" : _config.loginWelcomeNotice.title;

	if (msg.empty()) {
		return;
	}

	if (type == "Bulletin") {
		packet_UMsgManager.SendPacket_UMsg_BulletinNotice(
			user->GetConnection(),
			1000,
			_config.loginWelcomeNotice.category,
			title,
			msg
		);
	}
	else if (type == "PublicNotice" || type == "RollingBanner") {
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(user->GetConnection(), msg);
	}
	else if (type == "ServerYell") {
		packet_UMsgManager.SendPacket_UMsg_ServerYell(user->GetConnection(), "系統", msg);
	}
	else if (type == "Chat") {
		packet_UMsgManager.SendPacket_UMsg_Chat(user->GetConnection(), Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(msg));
	}
	else if (type == "ServerMessage") {
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(user->GetConnection(), msg);
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Sent login welcome notice ({}) to user ({})\n", type, user->GetUserLogName()));
}

void NoticeManager::SendChannelLoginNotice(User* user, const string& channelCustomLoginMsg) {
	if (user == NULL || user->GetConnection() == NULL) {
		return;
	}

	if (!_config.channelLoginNotice.enabled || _config.channelLoginNotice.type == "None") {
		return;
	}

	string msg = !channelCustomLoginMsg.empty() ? channelCustomLoginMsg : _config.channelLoginNotice.message;
	if (msg.empty()) {
		return;
	}

	const string& type = _config.channelLoginNotice.type;
	const string& title = _config.channelLoginNotice.title.empty() ? "頻道公告" : _config.channelLoginNotice.title;

	if (type != "None") {
		if (type == "Bulletin") {
			packet_UMsgManager.SendPacket_UMsg_BulletinNotice(
				user->GetConnection(),
				2000,
				_config.channelLoginNotice.category,
				title,
				msg
			);
		}
		else if (type == "PublicNotice" || type == "RollingBanner") {
			packet_UMsgManager.SendPacket_UMsg_PublicNotice(user->GetConnection(), msg);
		}
		else if (type == "ServerMessage") {
			packet_UMsgManager.SendPacket_UMsg_ServerMessageText(user->GetConnection(), msg);
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Sent channel login notice ({}) to user ({})\n", type, user->GetUserLogName()));
}

void NoticeManager::SendChannelChatNotice(User* user, const string& channelCustomLoginMsg) {
	if (user == NULL || user->GetConnection() == NULL) {
		return;
	}

	if (!_config.channelChatNotice.enabled || _config.channelChatNotice.type == "None") {
		return;
	}

	string msg = !channelCustomLoginMsg.empty() ? channelCustomLoginMsg : _config.channelChatNotice.message;
	if (msg.empty()) {
		return;
	}

	const string& type = _config.channelChatNotice.type;
	if (type == "ServerMessage") {
		// Native client renders ServerMessage in yellow text with [伺服器信息] header
		packet_UMsgManager.SendPacket_UMsg_ServerMessageText(user->GetConnection(), msg);
	}
	else if (type == "Chat") {
		// Normal client chat renders in white text with [系統] sender
		packet_UMsgManager.SendPacket_UMsg_Chat(user->GetConnection(), Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(msg));
	}
	else if (type == "ServerYell") {
		packet_UMsgManager.SendPacket_UMsg_ServerYell(user->GetConnection(), "系統", msg);
	}
	else if (type == "PublicNotice" || type == "RollingBanner") {
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(user->GetConnection(), msg);
	}

	serverConsole.Print(PrefixType::Info, format("[ NoticeManager ] Sent channel chat notice ({}) to user ({})\n", type, user->GetUserLogName()));
}
