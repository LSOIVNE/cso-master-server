#pragma once
#include "user.h"
#include "databasemanager.h"
#include <string>
#include <vector>
#include <unordered_map>
#include <chrono>
#include <mutex>

struct BingoPrizePoolItem {
	unsigned short itemID = 0;
	std::string name = "";
	unsigned char count = 1;
	unsigned char duration = 0; // days, 0 = permanent
	int weight = 10;
};

struct BingoSettings {
	bool active = true;
	unsigned long resetCost = 19;
	unsigned long shuffleCost = 35;
	int maxShuffleCompletedLines = 0; // must be <= this to shuffle (< 1)
	int maxShuffleOpenedNumbers = 10;  // must be <= this to shuffle (< 11)
	int testRollNumber = -1; // -1 = random, >= 0 = forced win number for testing
};

struct PendingBingoRoll {
	unsigned char winNumber = 0;
	int hitSlotIdx = -1; // -1 if duplicate or not on board
	std::vector<unsigned char> newlyCompletedLines;
	std::chrono::steady_clock::time_point timestamp;
};

class BingoManager {
public:
	BingoManager();
	~BingoManager();

	bool Init(const std::string& configPath = "configs/bingo.json");
	bool LoadConfig(const std::string& configPath);

	bool IsActive() const { return _settings.active; }
	const BingoSettings& GetSettings() const { return _settings; }

	bool GetOrInitBoard(unsigned long userID, BingoBoard& outBoard);
	bool GenerateNewBoard(unsigned long userID, BingoBoard& outBoard);
	bool ResetBoard(TCPConnection::pointer connection, User* user);
	bool ShuffleBoard(TCPConnection::pointer connection, User* user);
	bool OnBoxOpened(TCPConnection::pointer connection, User* user, unsigned short boxID);
	bool OnClientRequestSyncBoard(TCPConnection::pointer connection, User* user);

	// Packet sending helpers
	void SendPacket_Bingo_SyncBoard(TCPConnection::pointer connection, const BingoBoard& board);
	void SendPacket_Bingo_RollNumber(TCPConnection::pointer connection, unsigned char winNumber);
	void SendPacket_Bingo_LineCompleted(TCPConnection::pointer connection, const std::vector<unsigned char>& completedLines);
	void SendPacket_Bingo_Result(TCPConnection::pointer connection, bool success);
	void SendEventActive(TCPConnection::pointer connection);

	int CountCompletedLines(const BingoBoard& board) const;
	int CountOpenedNumbers(const BingoBoard& board) const;
	std::vector<unsigned char> CheckNewlyCompletedLines(BingoBoard& board);
	void GrantBingoPrize(User* user, const BingoPrize& prize, InventoryItem* outItem = nullptr);

private:
	BingoSettings _settings;
	std::vector<BingoPrizePoolItem> _prizePool;
	std::unordered_map<unsigned long, PendingBingoRoll> _pendingRolls;
	std::mutex _pendingMutex;
};

extern BingoManager bingoManager;
