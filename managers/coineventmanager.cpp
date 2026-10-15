#include "coineventmanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "packetmanager.h"
#include "packet_umsgmanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <ctime>

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
		serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] Loaded {} pool items from database.\n", dbPool.size()));
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
				item.winRate = it.value("WinRate", 10);
				item.latestWinner = "";
				_pool.push_back(item);
			}
		}

		// Sort pool by slotIdx
		std::sort(_pool.begin(), _pool.end(), [](const CoinEventPoolItem& a, const CoinEventPoolItem& b) {
			return a.slotIdx < b.slotIdx;
		});

		serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] Loaded config with {} pool items.\n", _pool.size()));
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
				// No coins available
				serverConsole.Print(PrefixType::Info, format("[ CoinEventManager ] User ({}) tried to draw without coins!\n", user->GetUserLogName()));
				SendPacket_DrawResult(connection, slotIdx, false);
				SendPacket_Status(connection);
				SendPacket_Coins(connection, 0);
				return;
			}

			// Deduct 1 coin
			coinData.coinCount--;
			databaseManager.SaveUserCoinData(coinData);

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
					if (poolItem.remainCount > 0) {
						int roll = rand() % 100;
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

				std::string winNotice = format("[抓錢活動] 恭喜玩家 [{}] 獲得 [{}]！", userNick, wonItemCopy.itemName);
				packet_UMsgManager.SendPacket_UMsg_Chat(connection, Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(winNotice));
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

void CoinEventManager::OnMatchEnd(User* user, unsigned int durationSeconds) {
	if (!user || !_settings.active || durationSeconds == 0) return;

	UserCoinEventData coinData;
	GetUserCoinData(user->GetUserID(), coinData);

	coinData.playtimeSeconds += durationSeconds;
	unsigned char earnedCoins = 0;

	while (coinData.playtimeSeconds >= _settings.playtimeSecondsPerCoin) {
		coinData.playtimeSeconds -= _settings.playtimeSecondsPerCoin;
		if (coinData.coinCount < _settings.maxCoins) {
			coinData.coinCount++;
			earnedCoins++;
		}
		else {
			auto conn = user->GetConnection();
			if (conn) {
				packet_UMsgManager.SendPacket_UMsg_Chat(conn, Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950("[抓錢活動] 硬幣欄位已滿(3個)，無法再累積硬幣，請盡速使用！"));
			}
			break;
		}
	}

	databaseManager.SaveUserCoinData(coinData);

	auto conn = user->GetConnection();
	if (conn) {
		if (earnedCoins > 0) {
			std::string msg = format("[抓錢活動] 遊戲累積滿20分鐘，獲得 {} 個硬幣！目前持有：{} 個", (int)earnedCoins, (int)coinData.coinCount);
			packet_UMsgManager.SendPacket_UMsg_Chat(conn, Packet_UMsgType::ChannelChat, Utf8ToCp950("系統"), Utf8ToCp950(msg));
		}
		SendPacket_Coins(conn, coinData.coinCount);
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
	packet->WriteUInt8(35); // subAction: Coins
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
	return databaseManager.GetUserCoinData(userID, outData);
}

bool CoinEventManager::AddUserCoins(User* user, int deltaCoins) {
	if (!user) return false;

	UserCoinEventData coinData;
	GetUserCoinData(user->GetUserID(), coinData);

	int newCount = (int)coinData.coinCount + deltaCoins;
	if (newCount < 0) newCount = 0;
	if (newCount > (int)_settings.maxCoins) newCount = (int)_settings.maxCoins;

	coinData.coinCount = (unsigned char)newCount;
	databaseManager.SaveUserCoinData(coinData);

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
	databaseManager.SaveUserCoinData(coinData);

	if (user->GetConnection()) {
		SendPacket_Coins(user->GetConnection(), coinData.coinCount);
	}
	return true;
}

void CoinEventManager::GrantPrize(User* user, unsigned short itemID, unsigned short count, unsigned int durationDays, InventoryItem* outItem) {
	if (!user) return;
	unsigned long userID = user->GetUserID();
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
