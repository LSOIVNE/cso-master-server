#include "bingomanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <random>
#include <algorithm>
#include <ctime>

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

bool BingoManager::ResetBoard(TCPConnection::pointer connection, User* user) {
	if (!user || !userManager.IsUserLoggedIn(user)) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	const UserCharacterResult& ucRes = user->GetUserCharacter(USERCHARACTER_FLAG_CASH);
	if (!ucRes.result || ucRes.userCharacter.cash < _settings.resetCost) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) lacks cash to reset bingo board (has {}, needs {})!\n",
			user->GetUserLogName(), ucRes.userCharacter.cash, _settings.resetCost));
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	if (!databaseManager.DeductUserFunds(userID, false, _settings.resetCost)) {
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	// Sync deducted cash to client
	UserCharacterResult updatedUC = user->GetUserCharacter(USERCHARACTER_FLAG_CASH);
	if (updatedUC.result) {
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedUC.userCharacter });
	}

	BingoBoard newBoard;
	if (!GenerateNewBoard(userID, newBoard)) {
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	if (!databaseManager.ResetUserBingoBoard(userID, newBoard)) {
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) successfully reset bingo board (spent {} cash)\n",
		user->GetUserLogName(), _settings.resetCost));

	SendPacket_Bingo_Result(connection, true);
	SendPacket_Bingo_SyncBoard(connection, newBoard);
	return true;
}

bool BingoManager::ShuffleBoard(TCPConnection::pointer connection, User* user) {
	if (!user || !userManager.IsUserLoggedIn(user)) {
		return false;
	}

	unsigned long userID = user->GetUserID();
	BingoBoard board;
	if (!GetOrInitBoard(userID, board)) {
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	int completedLines = CountCompletedLines(board);
	int openedNumbers = CountOpenedNumbers(board);

	if (completedLines > _settings.maxShuffleCompletedLines || openedNumbers > _settings.maxShuffleOpenedNumbers) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) cannot shuffle: completedLines={}, openedNumbers={}\n",
			user->GetUserLogName(), completedLines, openedNumbers));
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	const UserCharacterResult& ucRes = user->GetUserCharacter(USERCHARACTER_FLAG_CASH);
	if (!ucRes.result || ucRes.userCharacter.cash < _settings.shuffleCost) {
		serverConsole.Print(PrefixType::Warn, format("[ BingoManager ] User ({}) lacks cash to shuffle bingo board (has {}, needs {})!\n",
			user->GetUserLogName(), ucRes.userCharacter.cash, _settings.shuffleCost));
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	if (!databaseManager.DeductUserFunds(userID, false, _settings.shuffleCost)) {
		SendPacket_Bingo_Result(connection, false);
		return false;
	}

	UserCharacterResult updatedUC = user->GetUserCharacter(USERCHARACTER_FLAG_CASH);
	if (updatedUC.result) {
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, updatedUC.userCharacter });
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
		// Grant prizes
		auto inv = user->GetUserInventory();
		unsigned short maxSlotID = 0;
		for (const auto& it : inv) {
			if (it.slotID > maxSlotID) maxSlotID = it.slotID;
		}
		time_t now = time(nullptr);

		for (unsigned char lineIdx : newlyCompleted) {
			if (lineIdx < board.prizes.size()) {
				const auto& prize = board.prizes[lineIdx];
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = prize.itemID;
				newItem.count = prize.count;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = (prize.duration == 0) ? 0 : (unsigned long)(now + (unsigned long)prize.duration * 86400);
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });
			}
		}

		packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
		SendPacket_Bingo_LineCompleted(connection, newlyCompleted);
	}

	serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) successfully shuffled bingo board (spent {} cash)\n",
		user->GetUserLogName(), _settings.shuffleCost));

	SendPacket_Bingo_Result(connection, true);
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
	std::random_device rd;
	std::mt19937 g(rd());
	std::uniform_int_distribution<int> dist(0, 99);
	unsigned char winNumber = (unsigned char)dist(g);

	serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) rolled bingo number {} from box {}\n",
		user->GetUserLogName(), (int)winNumber, boxID));

	// Check if winNumber hits any slot on board
	bool hit = false;
	for (auto& slot : board.slots) {
		if (slot.number == winNumber) {
			hit = true;
			if (slot.opened == 0) {
				slot.opened = 1;
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
		auto inv = user->GetUserInventory();
		unsigned short maxSlotID = 0;
		for (const auto& it : inv) {
			if (it.slotID > maxSlotID) maxSlotID = it.slotID;
		}
		time_t now = time(nullptr);

		for (unsigned char lineIdx : newlyCompleted) {
			if (lineIdx < board.prizes.size()) {
				const auto& prize = board.prizes[lineIdx];
				databaseManager.UpdateUserBingoPrize(userID, lineIdx, 1);

				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = prize.itemID;
				newItem.count = prize.count;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = (prize.duration == 0) ? 0 : (unsigned long)(now + (unsigned long)prize.duration * 86400);
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });

				serverConsole.Print(PrefixType::Info, format("[ BingoManager ] User ({}) completed Bingo Line {}! Won itemID {}\n",
					user->GetUserLogName(), (int)lineIdx, prize.itemID));
			}
		}

		packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
	}

	// 1. Send RollNumber animation
	SendPacket_Bingo_RollNumber(connection, winNumber);

	// 2. If lines completed, send LineCompleted animation
	if (!newlyCompleted.empty()) {
		SendPacket_Bingo_LineCompleted(connection, newlyCompleted);
	}

	// 3. Full board sync
	SendPacket_Bingo_SyncBoard(connection, board);
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
	packet->WriteUInt32_LE(1); // Bit 0 = 1 (Bingo active!)

	packet->Send();
}
