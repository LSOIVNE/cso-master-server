#include "titlemanager.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_titlemanager.h"
#include "packet_umsgmanager.h"
#include "roommanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <format>

using json = nlohmann::json;

TitleManager titleManager;

bool TitleManager::Init() {
	return Reload();
}

bool TitleManager::Reload() {
	return loadConfigs();
}

bool TitleManager::loadConfigs() {
	const std::vector<std::string> searchPaths = {
		"configs\\title.json",
		"configs/title.json",
		"title.json",
		"..\\configs\\title.json",
		"..\\..\\configs\\title.json",
		"bin\\configs\\title.json"
	};

	std::ifstream file;
	std::string resolvedPath;
	for (const auto& path : searchPaths) {
		file.open(path);
		if (file.is_open()) {
			resolvedPath = path;
			break;
		}
	}

	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, "[ TitleManager ] Cannot open title.json from any search path!\n");
		return false;
	}

	try {
		json j;
		file >> j;
		file.close();

		_titles.clear();
		for (const auto& item : j) {
			TitleConfig config;
			config.id = item.value("id", (unsigned short)0);
			config.key = item.value("key", "");
			config.name = item.value("name", "");
			config.desc = item.value("desc", "");
			config.honorPoints = item.value("honorPoints", (unsigned short)10);

			if (config.id > 0) {
				_titles[config.id] = config;
			}
		}

		serverConsole.Print(PrefixType::Info, std::format("[ TitleManager ] Successfully loaded {} titles from {}\n",
			_titles.size(), resolvedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, std::format("[ TitleManager ] Failed to parse title.json: {}\n", e.what()));
		if (file.is_open()) {
			file.close();
		}
		return false;
	}
}

const TitleConfig* TitleManager::GetTitle(unsigned short id) const {
	auto it = _titles.find(id);
	if (it != _titles.end()) {
		return &it->second;
	}
	return nullptr;
}

const map<unsigned short, TitleConfig>& TitleManager::GetAllTitles() const {
	return _titles;
}

bool TitleManager::UnlockTitle(User* user, unsigned short titleID, bool syncToClient) {
	if (user == nullptr || titleID == 0) {
		return false;
	}

	const TitleConfig* cfg = GetTitle(titleID);
	if (cfg == nullptr) {
		serverConsole.Print(PrefixType::Warn, std::format("[ TitleManager ] Unknown title ID {} for user ({})\n",
			titleID, user->GetUserLogName()));
		return false;
	}

	unsigned long userID = user->GetUserID();
	if (databaseManager.HasUserTitle(userID, titleID)) {
		// Already unlocked
		return true;
	}

	if (!databaseManager.AddUserTitle(userID, titleID)) {
		serverConsole.Print(PrefixType::Error, std::format("[ TitleManager ] Failed to insert title {} into database for user ({})\n",
			titleID, user->GetUserLogName()));
		return false;
	}

	// Add honor points
	if (cfg->honorPoints > 0) {
		databaseManager.AddUserHonorPoints(userID, cfg->honorPoints);
	}

	serverConsole.Print(PrefixType::Info, std::format("[ TitleManager ] User ({}) unlocked Title [{}] '{}' (+{} Honor Points)!\n",
		user->GetUserLogName(), cfg->id, cfg->name, cfg->honorPoints));

	if (syncToClient) {
		SyncTitleUpdate(user);

		// Send celebratory chat notice
		string congratsMsg = std::format("恭喜獲得稱號【{}】！榮譽點數 +{}", cfg->name, cfg->honorPoints);
		packet_UMsgManager.SendPacket_UMsg_NoticeMessage(user->GetConnection(), congratsMsg);

		// Trigger official medal unlock popup (#CSO_Facebook_Medal_Get)
		packet_TitleManager.SendPacket_Title_UnlockMedal(user->GetConnection(), titleID);
	}

	return true;
}

bool TitleManager::UnlockAllTitles(User* user, bool syncToClient) {
	if (user == nullptr) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	unsigned short totalAddedHonor = 0;

	for (const auto& [tid, cfg] : _titles) {
		if (!databaseManager.HasUserTitle(userID, tid)) {
			if (databaseManager.AddUserTitle(userID, tid)) {
				totalAddedHonor += cfg.honorPoints;
			}
		}
	}

	if (totalAddedHonor > 0) {
		databaseManager.AddUserHonorPoints(userID, totalAddedHonor);
	}

	serverConsole.Print(PrefixType::Info, std::format("[ TitleManager ] User ({}) unlocked all {} titles (+{} Honor Points)!\n",
		user->GetUserLogName(), _titles.size(), totalAddedHonor));

	if (syncToClient) {
		SyncTitleUpdate(user);

		string congratsMsg = std::format("恭喜解鎖全 {} 個榮譽稱號！榮譽點數 +{}", _titles.size(), totalAddedHonor);
		packet_UMsgManager.SendPacket_UMsg_NoticeMessage(user->GetConnection(), congratsMsg);
	}

	return true;
}

bool TitleManager::EquipTitle(User* user, unsigned short prefixID) {
	if (user == nullptr) {
		return false;
	}

	unsigned long userID = user->GetUserID();

	if (prefixID == 0) {
		// Unequip current title
		if (!databaseManager.SetUserPrefixID(userID, 0)) {
			return false;
		}
		serverConsole.Print(PrefixType::Info, std::format("[ TitleManager ] User ({}) unequipped title\n",
			user->GetUserLogName()));
		SyncTitleUpdate(user);
		return true;
	}

	// Validate title existence and ownership
	const TitleConfig* cfg = GetTitle(prefixID);
	if (cfg == nullptr) {
		serverConsole.Print(PrefixType::Warn, std::format("[ TitleManager ] User ({}) tried to equip non-existent title {}\n",
			user->GetUserLogName(), prefixID));
		return false;
	}

	if (!databaseManager.HasUserTitle(userID, prefixID)) {
		serverConsole.Print(PrefixType::Warn, std::format("[ TitleManager ] User ({}) tried to equip locked title [{}] '{}'\n",
			user->GetUserLogName(), prefixID, cfg->name));
		return false;
	}

	if (!databaseManager.SetUserPrefixID(userID, prefixID)) {
		serverConsole.Print(PrefixType::Error, std::format("[ TitleManager ] Failed to update prefixID for user ({})\n",
			user->GetUserLogName()));
		return false;
	}

	serverConsole.Print(PrefixType::Info, std::format("[ TitleManager ] User ({}) equipped title [{}] '{}'\n",
		user->GetUserLogName(), prefixID, cfg->name));

	SyncTitleUpdate(user);
	return true;
}

bool TitleManager::EquipBadge(User* user, unsigned char slot, unsigned char titleID) {
	if (user == nullptr || slot >= 5) {
		return false;
	}

	unsigned long userID = user->GetUserID();

	if (titleID > 0) {
		if (!databaseManager.HasUserTitle(userID, titleID)) {
			serverConsole.Print(PrefixType::Warn, std::format("[ TitleManager ] User ({}) tried to equip locked badge {} at slot {}\n",
				user->GetUserLogName(), (int)titleID, (int)slot));
			return false;
		}
	}

	if (!databaseManager.SetUserBadgeSlot(userID, slot, titleID)) {
		return false;
	}

	serverConsole.Print(PrefixType::Info, std::format("[ TitleManager ] User ({}) equipped badge {} at slot {}\n",
		user->GetUserLogName(), (int)titleID, (int)slot));

	SyncTitleUpdate(user);
	return true;
}

bool TitleManager::UnequipBadge(User* user, unsigned char slot) {
	return EquipBadge(user, slot, 0);
}

void TitleManager::SyncTitleUpdate(User* user) {
	if (user == nullptr) {
		return;
	}

	auto connection = user->GetConnection();
	if (connection == nullptr) {
		return;
	}

	auto updatedChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (!updatedChar.result) {
		return;
	}

	// 1. Send Packet 157 UserUpdateInfo to user himself
	packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedChar.userCharacter });

	// 2. Broadcast updated character info to other users in room or lobby
	if (user->GetCurrentRoomID() > 0) {
		Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
		if (room != nullptr) {
			for (User* roomUser : room->GetRoomUsers()) {
				if (roomUser != nullptr && roomUser != user && roomUser->GetConnection() != nullptr) {
					auto roomPacket = TCPConnection::Packet::Create(PacketSource::Server, roomUser->GetConnection(), { (unsigned char)PacketID::UserUpdateInfo });
					if (roomPacket != nullptr) {
						roomPacket->WriteUInt32_LE(user->GetUserID());
						packetManager.BuildUserCharacter(roomPacket, updatedChar.userCharacter);
						roomPacket->Send();
					}
				}
			}
		}
	}
	else {
		userManager.SendAddUserPacketToAll(user);
	}
}
