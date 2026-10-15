#pragma once
#include "user.h"
#include "databasemanager.h"
#include <string>
#include <vector>
#include <map>
#include <mutex>

struct CoinEventSettings {
	bool active = true;
	unsigned int playtimeSecondsPerCoin = 1200;
	unsigned char maxCoins = 3;
	unsigned int minPlayers = 4;
};

class CoinEventManager {
public:
	CoinEventManager();
	~CoinEventManager();

	bool Init(const std::string& configPath = "configs/coinevent.json");
	bool LoadConfig(const std::string& configPath);
	bool InitPoolFromDatabase();

	bool IsActive() const { return _settings.active; }
	void SetActive(bool active) { _settings.active = active; }
	const CoinEventSettings& GetSettings() const { return _settings; }

	void ParsePacket(User* user, TCPConnection::Packet::pointer packet);
	void OnSecondTick();
	void ProcessInGameTick(User* user, unsigned int deltaSeconds);
	void OnMatchEnd(User* user, unsigned int durationSeconds = 0);
	void OnUserLogout(User* user);

	// Packet sending helpers
	void SendPacket_Settings(TCPConnection::pointer connection);
	void SendPacket_Status(TCPConnection::pointer connection);
	void SendPacket_Coins(TCPConnection::pointer connection, unsigned char coinCount);
	void SendPacket_DrawResult(TCPConnection::pointer connection, unsigned char slotIdx, bool isWin);

	// User operations
	bool GetUserCoinData(unsigned long userID, UserCoinEventData& outData);
	bool SaveUserCoinData(const UserCoinEventData& data);
	bool AddUserCoins(User* user, int deltaCoins);
	bool SetUserCoins(User* user, unsigned char coins);

	void GrantPrize(User* user, unsigned short itemID, unsigned short count, unsigned int durationDays, InventoryItem* outItem = nullptr);
	void FlushUserData(unsigned long userID);
	void FlushAllDirty();

private:
	CoinEventSettings _settings;
	std::vector<CoinEventPoolItem> _pool;
	std::map<unsigned long, UserCoinEventData> _userCache;
	std::map<unsigned long, bool> _dirtyUsers;
	std::mutex _mutex;
};

extern CoinEventManager coinEventManager;
