#include "coineventmanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "serverconsole.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_umsgmanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <ctime>
#include <random>
#include <chrono>

using json = nlohmann::json;

CoinEventManager coinEventManager;

static void WriteLengthStringRaw(TCPConnection::Packet::pointer packet, const std::string& str) {
	packet->WriteUInt8((unsigned char)str.size());
	for (char c : str) {
		packet->WriteInt8(c);
	}
}

CoinEventManager::CoinEventManager() {
}

CoinEventManager::~CoinEventManager() {
	FlushAllDirty();
}

bool CoinEventManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ CoinEventManager ] Initializing Coin Event system...\n");
	return LoadConfig(configPath);
}

bool CoinEventManager::InitPoolFromDatabase() {
	std::lock_guard<std::mutex> lock(_mutex);
	std::vector<CoinEventPoolItem> dbPool;
	if (databaseManager.GetCoinEventPool(dbPool) && !dbPool.empty()) {
		// Sync remaining counts and latest winners from DB into configured pool
		for (auto& item : _pool) {
			for (const auto& dbItem : dbPool) {
				if (item.slotIdx == dbItem.slotIdx) {
					item.remainCount = dbItem.remainCount;
					item.latestWinner = dbItem.latestWinner;
					break;
				}
			}
		}
		// Also update DB with stripped image names from config
		databaseManager.InitOrUpdateCoinEventPool(_pool);
		serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] Loaded and synced {} pool items with database.\n", dbPool.size()));
	}
	else {
		// Initialize DB pool from config
		databaseManager.InitOrUpdateCoinEventPool(_pool);
		serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] Initialized database pool with {} items from config.\n", _pool.size()));
	}

	return true;
}

bool CoinEventManager::LoadConfig(const std::string& configPath) {
	std::string resolvedPath = configPath.empty() ? "configs/coinevent.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Error, format("[ CoinEventManager ] Config file not found: {}\n", resolvedPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ CoinEventManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root;
		file >> root;

		if (root.contains("Settings")) {
			const auto& s = root["Settings"];
			_settings.active = s.value("Active", true);
			_settings.playtimeSecondsPerCoin = s.value("PlaytimeSecondsPerCoin", 1200);
			_settings.maxCoins = (unsigned char)s.value("MaxCoins", 3);
			_settings.minPlayers = s.value("MinPlayers", 4);
		}

		_pool.clear();
		if (root.contains("Items") && root["Items"].is_array()) {
			for (const auto& it : root["Items"]) {
				CoinEventPoolItem item;
				item.slotIdx = (unsigned char)it.value("SlotIdx", 0);
				item.imageName = it.value("ImageName", "");
				item.itemName = it.value("ItemName", "");
				item.totalCount = it.value("TotalCount", 100);
				item.remainCount = item.totalCount;
				item.itemID = (unsigned short)it.value("ItemID", 0);
				item.count = (unsigned short)it.value("Count", 1);
				item.duration = it.value("Duration", 0);
				item.winRate = it.value("WinRate", 10.0);
				item.latestWinner = "";
				_pool.push_back(item);
			}
		}

		// Sort pool by slotIdx
		std::sort(_pool.begin(), _pool.end(), [](const CoinEventPoolItem& a, const CoinEventPoolItem& b) {
			return a.slotIdx < b.slotIdx;
		});

		serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] Loaded config with {} pool items (minPlayers: {}).\n",
			_pool.size(), _settings.minPlayers));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ CoinEventManager ] JSON parse error: {}\n", e.what()));
		return false;
	}
}

void CoinEventManager::ParsePacket(User* user, TCPConnection::Packet::pointer packet) {
	if (!user || !packet) return;

	auto connection = user->GetConnection();
	if (!connection) return;

	unsigned char subAction = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] User ({}) sent Coin Event action {}\n", user->GetUserLogName(), (int)subAction));

	switch (subAction) {
		case 31: { // Client opens dialog and requests settings
			SendPacket_Settings(connection);
			SendPacket_Status(connection);
			UserCoinEventData coinData;
			GetUserCoinData(user->GetUserID(), coinData);
			SendPacket_Coins(connection, coinData.coinCount);
			break;
		}
		case 32: { // Client 2-second periodic poll
			SendPacket_Status(connection);
			UserCoinEventData coinData;
			GetUserCoinData(user->GetUserID(), coinData);
			SendPacket_Coins(connection, coinData.coinCount);
			break;
		}
		case 30: { // Client clicks draw button for a specific item slot (0..4)
			unsigned char slotIdx = packet->ReadUInt8();
			if (slotIdx >= _pool.size()) {
				serverConsole.Print(PrefixType::Warn, format("[ CoinEventManager ] User ({}) invalid slotIdx {}\n", user->GetUserLogName(), (int)slotIdx));
				return;
			}

			UserCoinEventData coinData;
			GetUserCoinData(user->GetUserID(), coinData);

			if (coinData.coinCount == 0) {
				serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] User ({}) tried to draw without coins!\n", user->GetUserLogName()));
				SendPacket_DrawResult(connection, slotIdx, false);
				SendPacket_Status(connection);
				SendPacket_Coins(connection, 0);
				return;
			}

			// Deduct 1 coin
			coinData.coinCount--;
			SaveUserCoinData(coinData);

			bool isWin = false;
			CoinEventPoolItem wonItemCopy;
			std::string userNick = "";
			{
				const UserCharacterResult& ucr = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
				if (ucr.result) {
					userNick = ucr.userCharacter.nickName;
				}
				if (userNick.empty()) {
					userNick = user->GetUserLogName();
				}
			}

			{
				std::lock_guard<std::mutex> lock(_mutex);
				if (slotIdx < _pool.size()) {
					CoinEventPoolItem& poolItem = _pool[slotIdx];
					if (poolItem.remainCount > 0 && poolItem.winRate > 0.0) {
						static thread_local std::mt19937_64 rng((unsigned long long)std::chrono::high_resolution_clock::now().time_since_epoch().count());
						std::uniform_real_distribution<double> dist(0.0, 100.0);
						double roll = dist(rng);
						if (roll < poolItem.winRate) {
							isWin = true;
							poolItem.remainCount--;
							poolItem.latestWinner = userNick;
							wonItemCopy = poolItem;
							databaseManager.UpdateCoinEventPoolSlot(poolItem.slotIdx, poolItem.remainCount, poolItem.latestWinner);
						}
					}
				}
			}

			if (isWin) {
				InventoryItem prizeItem{};
				GrantPrize(user, wonItemCopy.itemID, wonItemCopy.count, wonItemCopy.duration, &prizeItem);
				if (prizeItem.slotID != 0) {
					packetManager.SendPacket_InventoryDelta(connection, { prizeItem }, {});
				}

				serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] User ({}) WON slot {} (itemID {})!\n", user->GetUserLogName(), (int)slotIdx, wonItemCopy.itemID));
			}
			else {
				serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] User ({}) did not win slot {}\n", user->GetUserLogName(), (int)slotIdx));
			}

			SendPacket_DrawResult(connection, slotIdx, isWin);
			SendPacket_Status(connection);
			SendPacket_Coins(connection, coinData.coinCount);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ CoinEventManager ] User ({}) sent unknown action {}\n", user->GetUserLogName(), (int)subAction));
			break;
		}
	}
}

void CoinEventManager::OnSecondTick() {
	if (!_settings.active) return;

	static unsigned int secondCounter = 0;
	secondCounter++;

	// Periodic flush of dirty cached users every 60 seconds
	if (secondCounter % 60 == 0) {
		FlushAllDirty();
	}

	// In-game playtime accumulation and periodic reminder check
	for (Room* room : roomManager.GetRooms()) {
		if (!room) continue;
		if (room->GetRoomSettings().roomState != RoomState::Playing && room->GetGameMatch() == nullptr) {
			continue;
		}

		// Count in-game active users in this room
		std::vector<User*> inGameUsers;
		for (User* u : room->GetRoomUsers()) {
			if (u && u->GetConnection() && u->GetUserStatus() == UserStatus::InGame) {
				inGameUsers.push_back(u);
			}
		}

		// Require minimum players in room to start accumulating
		if (inGameUsers.size() < _settings.minPlayers) {
			continue;
		}

		for (User* u : inGameUsers) {
			ProcessInGameTick(u, 1);
		}
	}
}

void CoinEventManager::ProcessInGameTick(User* user, unsigned int deltaSeconds) {
	if (!user || !_settings.active) return;
	unsigned long userID = user->GetUserID();

	UserCoinEventData coinData;
	GetUserCoinData(userID, coinData);

	// 金幣滿了就不繼續發信息了，也不繼續累積
	if (coinData.coinCount >= _settings.maxCoins) {
		return;
	}

	coinData.playtimeSeconds += deltaSeconds;
	unsigned char earnedCoins = 0;

	while (coinData.playtimeSeconds >= _settings.playtimeSecondsPerCoin) {
		coinData.playtimeSeconds -= _settings.playtimeSecondsPerCoin;
		if (coinData.coinCount < _settings.maxCoins) {
			coinData.coinCount++;
			earnedCoins++;
		}
		if (coinData.coinCount >= _settings.maxCoins) {
			break;
		}
	}

	bool shouldSaveImmediate = false;

	if (earnedCoins > 0) {
		shouldSaveImmediate = true;
		auto conn = user->GetConnection();
		if (conn) {
			SendPacket_Coins(conn, coinData.coinCount);
			std::string msg = format("抓錢活動：遊戲時間累積滿 20 分鐘，獲得 {} 個硬幣！目前持有硬幣：{}/{} 個",
				(int)earnedCoins, (int)coinData.coinCount, (int)_settings.maxCoins);
			packet_UMsgManager.SendPacket_UMsg_ServerMessageText(conn, msg);
		}
	}

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[userID] = coinData;
		_dirtyUsers[userID] = true;
	}

	if (shouldSaveImmediate) {
		databaseManager.SaveUserCoinData(coinData);
		std::lock_guard<std::mutex> lock(_mutex);
		_dirtyUsers[userID] = false;
	}
}

void CoinEventManager::OnMatchEnd(User* user, unsigned int /*durationSeconds*/) {
	if (!user || !_settings.active) return;
	FlushUserData(user->GetUserID());

	auto conn = user->GetConnection();
	if (conn) {
		UserCoinEventData coinData;
		GetUserCoinData(user->GetUserID(), coinData);
		SendPacket_Coins(conn, coinData.coinCount);
	}
}

void CoinEventManager::OnUserLogout(User* user) {
	if (!user) return;
	unsigned long userID = user->GetUserID();
	FlushUserData(userID);
	std::lock_guard<std::mutex> lock(_mutex);
	_userCache.erase(userID);
	_dirtyUsers.erase(userID);
}

void CoinEventManager::FlushUserData(unsigned long userID) {
	UserCoinEventData coinData;
	bool dirty = false;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _userCache.find(userID);
		if (it != _userCache.end()) {
			coinData = it->second;
			auto dirtyIt = _dirtyUsers.find(userID);
			if (dirtyIt != _dirtyUsers.end() && dirtyIt->second) {
				dirty = true;
				dirtyIt->second = false;
			}
		}
	}
	if (dirty) {
		databaseManager.SaveUserCoinData(coinData);
	}
}

void CoinEventManager::FlushAllDirty() {
	std::vector<UserCoinEventData> toSave;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		for (auto& pair : _dirtyUsers) {
			if (pair.second) {
				auto it = _userCache.find(pair.first);
				if (it != _userCache.end()) {
					toSave.push_back(it->second);
				}
				pair.second = false;
			}
		}
	}
	for (const auto& data : toSave) {
		databaseManager.SaveUserCoinData(data);
	}
}

void CoinEventManager::SendPacket_Settings(TCPConnection::pointer connection) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	std::lock_guard<std::mutex> lock(_mutex);
	packet->WriteUInt8(3);  // gameType: Coin Event
	packet->WriteUInt8(31); // subAction: Settings
	packet->WriteUInt8(_settings.active ? 1 : 0);
	packet->WriteUInt8((unsigned char)_pool.size());

	for (const auto& item : _pool) {
		WriteLengthStringRaw(packet, item.imageName);
		WriteLengthStringRaw(packet, Utf8ToCp950(item.itemName));
		packet->WriteUInt32_LE(item.totalCount);
	}

	packet->Send();
}

void CoinEventManager::SendPacket_Status(TCPConnection::pointer connection) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	std::lock_guard<std::mutex> lock(_mutex);
	packet->WriteUInt8(3);  // gameType: Coin Event
	packet->WriteUInt8(33); // subAction: Status / Remains & Winners

	unsigned char mask = 0;
	for (size_t i = 0; i < _pool.size() && i < 5; ++i) {
		mask |= (1 << (unsigned char)i);
	}
	packet->WriteUInt8(mask);

	for (size_t i = 0; i < _pool.size() && i < 5; ++i) {
		const auto& item = _pool[i];
		packet->WriteUInt32_LE(item.remainCount);
		WriteLengthStringRaw(packet, Utf8ToCp950(item.latestWinner));
	}

	packet->Send();
}

void CoinEventManager::SendPacket_Coins(TCPConnection::pointer connection, unsigned char coinCount) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(3);  // gameType: Coin Event
	packet->WriteUInt8(35); // subAction: 35 (Coins count update)
	packet->WriteUInt8(coinCount);

	packet->Send();
}

void CoinEventManager::SendPacket_DrawResult(TCPConnection::pointer connection, unsigned char slotIdx, bool isWin) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(3);  // gameType: Coin Event
	packet->WriteUInt8(30); // subAction: DrawResult
	packet->WriteUInt8(slotIdx);
	packet->WriteUInt8(isWin ? 1 : 0);

	packet->Send();
}

bool CoinEventManager::GetUserCoinData(unsigned long userID, UserCoinEventData& outData) {
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _userCache.find(userID);
		if (it != _userCache.end()) {
			outData = it->second;
			return true;
		}
	}

	bool res = databaseManager.GetUserCoinData(userID, outData);
	if (res) {
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[userID] = outData;
	}
	return res;
}

bool CoinEventManager::SaveUserCoinData(const UserCoinEventData& data) {
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[data.userID] = data;
		_dirtyUsers[data.userID] = false;
	}
	return databaseManager.SaveUserCoinData(data);
}

bool CoinEventManager::AddUserCoins(User* user, int deltaCoins) {
	if (!user) return false;

	UserCoinEventData coinData;
	GetUserCoinData(user->GetUserID(), coinData);

	int newCount = (int)coinData.coinCount + deltaCoins;
	if (newCount < 0) newCount = 0;
	if (newCount > (int)_settings.maxCoins) newCount = (int)_settings.maxCoins;

	coinData.coinCount = (unsigned char)newCount;
	SaveUserCoinData(coinData);

	if (user->GetConnection()) {
		SendPacket_Coins(user->GetConnection(), coinData.coinCount);
	}
	return true;
}

bool CoinEventManager::SetUserCoins(User* user, unsigned char coins) {
	if (!user) return false;

	UserCoinEventData coinData;
	GetUserCoinData(user->GetUserID(), coinData);

	if (coins > _settings.maxCoins) coins = _settings.maxCoins;
	coinData.coinCount = coins;
	SaveUserCoinData(coinData);

	if (user->GetConnection()) {
		SendPacket_Coins(user->GetConnection(), coinData.coinCount);
	}
	return true;
}

void CoinEventManager::GrantPrize(User* user, unsigned short itemID, unsigned short count, unsigned int durationDays, InventoryItem* outItem) {
	if (!user) return;
	unsigned long userID = user->GetUserID();

	// Special handling for C-Box (itemID 166): Stored in user_characters.cboxCount, never in inventory
	if (itemID == 166) {
		unsigned short addCount = count > 0 ? count : 1;
		unsigned long newTotal = 0;
		user->AddCBoxCount(addCount, &newTotal);
		auto conn = user->GetConnection();
		if (conn) {
			packet_UpdateInfoManager.SendPacket_UserUpdateInfo_CBoxCount(conn, userID, (unsigned short)newTotal);
			auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
		}
		if (outItem != nullptr) {
			*outItem = InventoryItem{};
		}
		return;
	}

	auto inv = user->GetUserInventory();

	unsigned short maxSlotID = 0;
	bool alreadyOwned = false;
	InventoryItem ownedItem{};
	for (const auto& it : inv) {
		if (it.slotID > maxSlotID) {
			maxSlotID = it.slotID;
		}
		if (it.itemID == itemID) {
			alreadyOwned = true;
			ownedItem = it;
		}
	}

	const auto* itemDef = itemDefinitionManager.GetItem(itemID);
	bool isPeriodItem = (durationDays != 0) || (itemDef != nullptr && itemDef->isPeriod);
	time_t now = time(nullptr);

	if (!isPeriodItem && durationDays == 0) {
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(userID, itemID, count);
			if (outItem != nullptr) {
				ownedItem.count += count;
				*outItem = ownedItem;
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
			if (outItem != nullptr) {
				*outItem = newItem;
			}
		}
	}
	else {
		bool isPermItem = (durationDays == 0);
		if (alreadyOwned) {
			if (ownedItem.expirationTime == 0) {
				serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] User ({}) already owns permanent itemID {}, skipping duplicate.\n",
					user->GetUserLogName(), itemID));
			}
			else if (isPermItem) {
				ownedItem.expirationTime = 0;
				databaseManager.UpdateUserInventoryExpiration(userID, itemID, 0);
				if (outItem != nullptr) {
					*outItem = ownedItem;
				}
			}
			else {
				unsigned long addSeconds = (unsigned long)durationDays * 86400;
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				ownedItem.expirationTime = baseTime + addSeconds;
				databaseManager.UpdateUserInventoryExpiration(userID, itemID, ownedItem.expirationTime);
				if (outItem != nullptr) {
					*outItem = ownedItem;
				}
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count > 0 ? count : 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = isPermItem ? 0 : (unsigned long)(now + (unsigned long)durationDays * 86400);
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
			if (outItem != nullptr) {
				*outItem = newItem;
			}
		}
	}
}
