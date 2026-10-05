#include "bingomanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_umsgmanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <random>
#include <algorithm>
#include <ctime>
#include <thread>

using json = nlohmann::json;

BingoManager bingoManager;

static const std::vector<std::vector<unsigned char>> BINGO_LINES = {
	// 5 Horizontal rows (Lines 0 ~ 4)
	{ 0,  1,  2,  3,  4 },
	{ 5,  6,  7,  8,  9 },
	{ 10, 11, 12, 13, 14 },
	{ 15, 16, 17, 18, 19 },
	{ 20, 21, 22, 23, 24 },
	// 5 Vertical columns (Lines 5 ~ 9)
	{ 0,  5, 10, 15, 20 },
	{ 1,  6, 11, 16, 21 },
	{ 2,  7, 12, 17, 22 },
	{ 3,  8, 13, 18, 23 },
	{ 4,  9, 14, 19, 24 },
	// 2 Diagonals (Lines 10 ~ 11)
	{ 0,  6, 12, 18, 24 }, // Main diagonal (TL to BR)
	{ 4,  8, 12, 16, 20 }  // Anti diagonal (TR to BL)
};

BingoManager::BingoManager() {
}

BingoManager::~BingoManager() {
}

bool BingoManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ BingoManager ] Initializing Bonus Bingo system...\n");
	return LoadConfig(configPath);
}

bool BingoManager::LoadConfig(const std::string& configPath) {
	std::vector<std::string> searchPaths = {
		configPath,
		"configs/bingo.json",
		"configs\\bingo.json",
		"bin/configs/bingo.json",
		"bin\\configs\\bingo.json",
		"../configs/bingo.json"
	};

	std::string resolvedPath = "";
	for (const auto& path : searchPaths) {
		if (std::filesystem::exists(path)) {
			resolvedPath = path;
			break;
		}
	}

	if (resolvedPath.empty()) {
		serverConsole.Print(PrefixType::Error, format("[ BingoManager ] Failed to locate bingo config file! (tried {})\n", configPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ BingoManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root = json::parse(file);

		// 1. Settings
		if (root.contains("Settings") && root["Settings"].is_object()) {
			const auto& s = root["Settings"];
			_settings.active = s.value("Active", true);
			_settings.resetCost = s.value("ResetCost", 19ul);
			_settings.shuffleCost = s.value("ShuffleCost", 35ul);
			_settings.maxShuffleCompletedLines = s.value("MaxShuffleCompletedLines", 0);
			_settings.maxShuffleOpenedNumbers = s.value("MaxShuffleOpenedNumbers", 10);
			_settings.testRollNumber = s.value("TestRollNumber", -1);
		}

		// 2. Prizes pool
		_prizePool.clear();
		if (root.contains("Prizes") && root["Prizes"].is_array()) {
			for (const auto& p : root["Prizes"]) {
				BingoPrizePoolItem item;
				item.itemID = p.value("ItemID", (unsigned short)0);
				item.name = p.value("Name", "");
				item.count = p.value("Count", (unsigned char)1);
				item.duration = p.value("Duration", (unsigned char)0);
				item.weight = p.value("Weight", 10);
				_prizePool.push_back(item);
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ BingoManager ] Loaded {} bingo prizes from {}\n",
			_prizePool.size(), resolvedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ BingoManager ] Error parsing {}: {}\n", resolvedPath, e.what()));
		return false;
	}
}

bool BingoManager::GetOrInitBoard(unsigned long userID, BingoBoard& outBoard) {
	if (databaseManager.GetUserBingoBoard(userID, outBoard)) {
		return true;
	}

	if (GenerateNewBoard(userID, outBoard)) {
		return databaseManager.SaveUserBingoBoard(outBoard);
	}

	return false;
}

bool BingoManager::GenerateNewBoard(unsigned long userID, BingoBoard& outBoard) {
	outBoard.userID = userID;
	outBoard.status = 0;
	outBoard.canPlay = true;

	// 1. Generate 25 distinct numbers (0 ~ 99)
	std::vector<unsigned char> numberPool(100);
	for (int i = 0; i < 100; ++i) {
		numberPool[i] = (unsigned char)i;
	}

	std::random_device rd;
	std::mt19937 g(rd());
	std::shuffle(numberPool.begin(), numberPool.end(), g);

	outBoard.slots.clear();
	for (int i = 0; i < 25; ++i) {
		BingoSlot slot;
		slot.slotIdx = (unsigned char)i;
		slot.number = numberPool[i];
		slot.opened = 0;
		outBoard.slots.push_back(slot);
	}

	// 2. Generate 12 prizes without replacement
	outBoard.prizes.clear();
	std::vector<BingoPrizePoolItem> available = _prizePool;

	for (int i = 0; i < 12; ++i) {
		if (available.empty()) {
			break;
		}

		int totalWeight = 0;
		for (const auto& it : available) {
			totalWeight += it.weight;
		}

		if (totalWeight <= 0) {
			totalWeight = 1;
		}

		std::uniform_int_distribution<int> dist(0, totalWeight - 1);
		int roll = dist(g);
		int acc = 0;
		size_t chosenIdx = 0;

		for (size_t j = 0; j < available.size(); ++j) {
			acc += available[j].weight;
			if (roll < acc) {
				chosenIdx = j;
				break;
			}
		}

		BingoPrize prize;
		prize.lineIdx = (unsigned char)i;
		prize.itemID = available[chosenIdx].itemID;
		prize.count = available[chosenIdx].count;
		prize.duration = available[chosenIdx].duration;
		prize.opened = 0;
		outBoard.prizes.push_back(prize);

		available.erase(available.begin() + chosenIdx);
	}

	return (outBoard.slots.size() == 25 && outBoard.prizes.size() == 12);
}

int BingoManager::CountCompletedLines(const BingoBoard& board) const {
	int count = 0;
	for (size_t i = 0; i < BINGO_LINES.size(); ++i) {
		if (i < board.prizes.size() && board.prizes[i].opened != 0) {
			count++;
			continue;
		}

		bool allOpened = true;
		for (unsigned char slotIdx : BINGO_LINES[i]) {
			if (slotIdx >= board.slots.size() || board.slots[slotIdx].opened == 0) {
				allOpened = false;
				break;
			}
		}
		if (allOpened) {
			count++;
		}
	}
	return count;
}

int BingoManager::CountOpenedNumbers(const BingoBoard& board) const {
	int count = 0;
	for (const auto& s : board.slots) {
		if (s.opened != 0) {
			count++;
		}
	}
	return count;
}

std::vector<unsigned char> BingoManager::CheckNewlyCompletedLines(BingoBoard& board) {
	std::vector<unsigned char> newlyCompleted;

	for (size_t i = 0; i < BINGO_LINES.size(); ++i) {
		if (i >= board.prizes.size()) {
			break;
		}

		if (board.prizes[i].opened != 0) {
			continue; // Already claimed
		}

		bool allOpened = true;
		for (unsigned char slotIdx : BINGO_LINES[i]) {
			if (slotIdx >= board.slots.size() || board.slots[slotIdx].opened == 0) {
				allOpened = false;
				break;
			}
		}

		if (allOpened) {
			board.prizes[i].opened = 1;
			newlyCompleted.push_back((unsigned char)i);
		}
	}

	return newlyCompleted;
}

void BingoManager::GrantBingoPrize(User* user, const BingoPrize& prize) {
	if (!user) return;
	unsigned long userID = user->GetUserID();
	auto inv = user->GetUserInventory();

	unsigned short maxSlotID = 0;
	bool alreadyOwned = false;
	InventoryItem ownedItem;
	for (const auto& it : inv) {
		if (it.slotID > maxSlotID) {
			maxSlotID = it.slotID;
		}
		if (it.itemID == prize.itemID) {
			alreadyOwned = true;
			ownedItem = it;
		}
	}

	const auto* itemDef = itemDefinitionManager.GetItem(prize.itemID);
	bool isPeriodItem = (prize.duration != 0) || (itemDef != nullptr && itemDef->isPeriod);
	time_t now = time(nullptr);

	if (!isPeriodItem && prize.duration == 0) {
		// Count-based consumable item (e.g. spray, coupons, decoders)
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(userID, prize.itemID, prize.count);
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = prize.itemID;
			newItem.count = prize.count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}
	else {
		// Duration / permanent equipment
		bool isPermItem = (prize.duration == 0);
		if (alreadyOwned) {
			if (ownedItem.expirationTime == 0) {
				// Already permanent, keep permanent (skip duplicate creation)
				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) already owns permanent itemID {}, skipping duplicate.\n",
					user->GetUserLogName(), prize.itemID));
			}
			else if (isPermItem) {
				// Upgrade existing timed item to permanent
				databaseManager.UpdateUserInventoryExpiration(userID, prize.itemID, 0);
				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) upgraded itemID {} to permanent!\n",
					user->GetUserLogName(), prize.itemID));
			}
			else {
				// Extend expiration of existing timed item
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)prize.duration * 86400;
				databaseManager.UpdateUserInventoryExpiration(userID, prize.itemID, newExp);
				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) extended itemID {} expiration to {}.\n",
					user->GetUserLogName(), prize.itemID, newExp));
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = prize.itemID;
			newItem.count = 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = isPermItem ? 0 : (unsigned long)(now + (unsigned long)prize.duration * 86400);
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}
}

bool BingoManager::ResetBoard(TCPConnection::pointer connection, User* user) {
	if (!user || !userManager.IsUserLoggedIn(user)) {
		return false;
	}

	if (!IsActive()) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) attempted to reset bingo board, but Bingo event is inactive!\n", user->GetUserLogName()));
		return false;
	}

	unsigned long userID = user->GetUserID();
	const UserCharacterResult& ucRes = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (!ucRes.result || ucRes.userCharacter.cash < _settings.resetCost) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) lacks cash to reset bingo board (has {}, needs {})!\n",
			user->GetUserLogName(), ucRes.userCharacter.cash, _settings.resetCost));
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, "樂豆點不足，無法重置賓果盤！");
		return false;
	}

	if (!databaseManager.DeductUserFunds(userID, false, _settings.resetCost)) {
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, "樂豆點不足，無法重置賓果盤！");
		return false;
	}

	// Sync deducted cash to client HUD immediately (requires USERCHARACTER_FLAG_ALL for 0x400 flag)
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (charResult.result) {
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
	}

	BingoBoard newBoard;
	if (!GenerateNewBoard(userID, newBoard)) {
		return false;
	}

	if (!databaseManager.ResetUserBingoBoard(userID, newBoard)) {
		return false;
	}

	{
		std::lock_guard<std::mutex> lock(_pendingMutex);
		_pendingRolls.erase(userID);
	}

	serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) successfully reset bingo board (spent {} cash)\n",
		user->GetUserLogName(), _settings.resetCost));

	SendPacket_Bingo_SyncBoard(connection, newBoard);
	return true;
}

bool BingoManager::ShuffleBoard(TCPConnection::pointer connection, User* user) {
	if (!user || !userManager.IsUserLoggedIn(user)) {
		return false;
	}

	if (!IsActive()) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) attempted to shuffle bingo board, but Bingo event is inactive!\n", user->GetUserLogName()));
		return false;
	}

	unsigned long userID = user->GetUserID();
	BingoBoard board;
	if (!GetOrInitBoard(userID, board)) {
		return false;
	}

	int completedLines = CountCompletedLines(board);
	int openedNumbers = CountOpenedNumbers(board);

	if (completedLines > _settings.maxShuffleCompletedLines || openedNumbers > _settings.maxShuffleOpenedNumbers) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) cannot shuffle: completedLines={}, openedNumbers={}\n",
			user->GetUserLogName(), completedLines, openedNumbers));
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::WarningMessage, "BINGO_SHUFFLE_NUMBERS_FAILED_SHUFFLE_LIMIT_EXCEED");
		return false;
	}

	const UserCharacterResult& ucRes = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (!ucRes.result || ucRes.userCharacter.cash < _settings.shuffleCost) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) lacks cash to shuffle bingo board (has {}, needs {})!\n",
			user->GetUserLogName(), ucRes.userCharacter.cash, _settings.shuffleCost));
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, "樂豆點不足，無法移位賓果盤！");
		return false;
	}

	if (!databaseManager.DeductUserFunds(userID, false, _settings.shuffleCost)) {
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, "樂豆點不足，無法移位賓果盤！");
		return false;
	}

	// Sync deducted cash to client HUD immediately (requires USERCHARACTER_FLAG_ALL for 0x400 flag)
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	if (charResult.result) {
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
	}

	// Shuffle the 25 slot contents
	struct SlotPair {
		unsigned char number;
		unsigned char opened;
	};
	std::vector<SlotPair> pairs;
	for (const auto& s : board.slots) {
		pairs.push_back({ s.number, s.opened });
	}

	std::random_device rd;
	std::mt19937 g(rd());
	std::shuffle(pairs.begin(), pairs.end(), g);

	for (size_t i = 0; i < board.slots.size() && i < pairs.size(); ++i) {
		board.slots[i].number = pairs[i].number;
		board.slots[i].opened = pairs[i].opened;
	}

	// Check if shuffle completed any line
	std::vector<unsigned char> newlyCompleted = CheckNewlyCompletedLines(board);

	databaseManager.SaveUserBingoBoard(board);

	if (!newlyCompleted.empty()) {
		for (unsigned char lineIdx : newlyCompleted) {
			if (lineIdx < board.prizes.size()) {
				const auto& prize = board.prizes[lineIdx];
				GrantBingoPrize(user, prize);
				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) completed Bingo Line {} via shuffle! Won itemID {}\n",
					user->GetUserLogName(), (int)lineIdx, prize.itemID));
			}
		}

		packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
		SendPacket_Bingo_LineCompleted(connection, newlyCompleted);
	}

	{
		std::lock_guard<std::mutex> lock(_pendingMutex);
		_pendingRolls.erase(userID);
	}

	serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) successfully shuffled bingo board (spent {} cash)\n",
		user->GetUserLogName(), _settings.shuffleCost));

	SendPacket_Bingo_SyncBoard(connection, board);
	return true;
}

bool BingoManager::OnBoxOpened(TCPConnection::pointer connection, User* user, unsigned short boxID) {
	if (!_settings.active || !user) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	BingoBoard board;
	if (!GetOrInitBoard(userID, board)) {
		return false;
	}

	// Roll a number 0 ~ 99
	unsigned char winNumber = 0;
	if (_settings.testRollNumber >= 0 && _settings.testRollNumber <= 99) {
		winNumber = (unsigned char)_settings.testRollNumber;
	}
	else {
		std::random_device rd;
		std::mt19937 g(rd());
		std::uniform_int_distribution<int> dist(0, 99);
		winNumber = (unsigned char)dist(g);
	}

	serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) rolled bingo number {} from box {}\n",
		user->GetUserLogName(), (int)winNumber, boxID));

	// Check if winNumber hits any slot on board
	int hitSlotIdx = -1;
	for (auto& slot : board.slots) {
		if (slot.number == winNumber) {
			if (slot.opened == 0) {
				slot.opened = 1;
				hitSlotIdx = (int)slot.slotIdx;
				databaseManager.UpdateUserBingoSlot(userID, slot.slotIdx, 1);
				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) hit new bingo slot {} (number {})!\n",
					user->GetUserLogName(), (int)slot.slotIdx, (int)winNumber));
			}
			else {
				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) hit duplicate bingo number {}!\n",
					user->GetUserLogName(), (int)winNumber));
			}
			break;
		}
	}

	// Check line completion
	std::vector<unsigned char> newlyCompleted = CheckNewlyCompletedLines(board);

	if (!newlyCompleted.empty()) {
		for (unsigned char lineIdx : newlyCompleted) {
			if (lineIdx < board.prizes.size()) {
				const auto& prize = board.prizes[lineIdx];
				databaseManager.UpdateUserBingoPrize(userID, lineIdx, 1);
				GrantBingoPrize(user, prize);

				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) completed Bingo Line {}! Won itemID {}\n",
					user->GetUserLogName(), (int)lineIdx, prize.itemID));
			}
		}

		packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
	}

	// Store pending roll for the client's upcoming Action 0 request
	{
		std::lock_guard<std::mutex> lock(_pendingMutex);
		PendingBingoRoll roll;
		roll.winNumber = winNumber;
		roll.hitSlotIdx = hitSlotIdx;
		roll.newlyCompletedLines = newlyCompleted;
		roll.timestamp = std::chrono::steady_clock::now();
		_pendingRolls[userID] = roll;
	}

	return true;
}

bool BingoManager::OnClientRequestSyncBoard(TCPConnection::pointer connection, User* user) {
	if (!user || !connection) {
		return false;
	}

	if (!IsActive()) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	BingoBoard board;
	if (!GetOrInitBoard(userID, board)) {
		return false;
	}

	PendingBingoRoll pendingRoll;
	bool hasPending = false;
	{
		std::lock_guard<std::mutex> lock(_pendingMutex);
		auto it = _pendingRolls.find(userID);
		if (it != _pendingRolls.end()) {
			auto now = std::chrono::steady_clock::now();
			auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - it->second.timestamp).count();
			if (elapsed < 30) {
				pendingRoll = it->second;
				hasPending = true;
			}
			_pendingRolls.erase(it);
		}
	}

	if (hasPending) {
		serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) syncing board with pending roll: num={}, hitSlot={}, lines={}\n",
			user->GetUserLogName(), (int)pendingRoll.winNumber, pendingRoll.hitSlotIdx, pendingRoll.newlyCompletedLines.size()));

		if (pendingRoll.hitSlotIdx >= 0) {
			BingoBoard maskedBoard = board;
			if (pendingRoll.hitSlotIdx < (int)maskedBoard.slots.size()) {
				maskedBoard.slots[pendingRoll.hitSlotIdx].opened = 0;
			}
			for (unsigned char lineIdx : pendingRoll.newlyCompletedLines) {
				if (lineIdx < maskedBoard.prizes.size()) {
					maskedBoard.prizes[lineIdx].opened = 0;
				}
			}

			// 1. Send masked board so client memory has the target slot & prize unopened
			SendPacket_Bingo_SyncBoard(connection, maskedBoard);

			// 2. Send RollNumber animation
			SendPacket_Bingo_RollNumber(connection, pendingRoll.winNumber);

			// 3. If lines completed, send LineCompleted animation
			if (!pendingRoll.newlyCompletedLines.empty()) {
				SendPacket_Bingo_LineCompleted(connection, pendingRoll.newlyCompletedLines);
			}

			// 4. Delayed sync of the real board:
			// While bingo_number.bik (1.6s) is playing, sub_100619E0 is blocked.
			// Sending the real board during playback ensures client memory gets opened = 1.
			// When animations finish, sub_100619E0 runs and maintains the dark / stamped state!
			std::thread([connection, board, userID]() {
				std::this_thread::sleep_for(std::chrono::milliseconds(400));
				User* u = userManager.GetUserByConnection(connection);
				if (u && userManager.IsUserLoggedIn(u) && u->GetUserID() == userID) {
					bingoManager.SendPacket_Bingo_SyncBoard(connection, board);
				}
			}).detach();
		}
		else {
			// Duplicate or miss: no animation needed
			SendPacket_Bingo_SyncBoard(connection, board);
			SendPacket_Bingo_RollNumber(connection, pendingRoll.winNumber);
		}
	}
	else {
		// Normal board sync (e.g. user opens bingo panel in lobby)
		SendPacket_Bingo_SyncBoard(connection, board);
	}

	return true;
}

void BingoManager::SendPacket_Bingo_SyncBoard(TCPConnection::pointer connection, const BingoBoard& board) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(0); // gameType: Bingo
	packet->WriteUInt8(0); // subAction: SyncBoard

	packet->WriteUInt8((unsigned char)board.slots.size()); // 25
	for (const auto& s : board.slots) {
		packet->WriteUInt8(s.number);
		packet->WriteUInt8(s.opened);
	}

	packet->WriteUInt8((unsigned char)board.prizes.size()); // 12
	for (const auto& p : board.prizes) {
		packet->WriteUInt16_LE(p.itemID);
		packet->WriteUInt8(p.count);
		packet->WriteUInt8(p.duration);
		packet->WriteUInt8(p.opened);
	}

	packet->Send();
}

void BingoManager::SendPacket_Bingo_RollNumber(TCPConnection::pointer connection, unsigned char winNumber) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(0); // gameType: Bingo
	packet->WriteUInt8(1); // subAction: RollNumber
	packet->WriteUInt8(winNumber);

	packet->Send();
}

void BingoManager::SendPacket_Bingo_LineCompleted(TCPConnection::pointer connection, const std::vector<unsigned char>& completedLines) {
	if (!connection || completedLines.empty()) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(0); // gameType: Bingo
	packet->WriteUInt8(2); // subAction: LineCompleted
	packet->WriteUInt8((unsigned char)completedLines.size());
	for (unsigned char idx : completedLines) {
		packet->WriteUInt8(idx);
	}

	packet->Send();
}

void BingoManager::SendPacket_Bingo_Result(TCPConnection::pointer connection, bool success) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(0); // gameType: Bingo
	packet->WriteUInt8(5); // subAction: Result
	packet->WriteUInt8(success ? 1 : 0);

	packet->Send();
}

void BingoManager::SendEventActive(TCPConnection::pointer connection) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Event });
	if (!packet) return;

	packet->WriteUInt8(1); // subType 1 (Event active mask)
	packet->WriteUInt32_LE(_settings.active ? 1 : 0); // Bit 0 = Bingo active flag!

	packet->Send();
}
