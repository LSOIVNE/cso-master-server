#pragma once
#include "user.h"
#include "databasemanager.h"
#include <string>
#include <vector>
#include <mutex>

struct BaseballHitWeights {
	int out = 38;
	int single = 32;
	int double_hit = 18;
	int triple = 8;
	int homeRun = 4;
};

struct BaseballScoreRewardTier {
	int minScore = 0;
	int maxScore = 0;
	unsigned short itemID = 0;
	unsigned short count = 1;
	unsigned int duration = 0;
	std::string name = "";
};

struct BaseballEventSettings {
	bool active = true;
	unsigned int playtimeSecondsPerBall = 1200;
	unsigned char maxBalls = 99;
	BaseballHitWeights hitWeights;
};

class BaseballEventManager {
public:
	BaseballEventManager();
	~BaseballEventManager();

	bool Init(const std::string& configPath = "configs/baseballevent.json");
	bool LoadConfig(const std::string& configPath);

	bool IsActive() const { return _settings.active; }
	void SetActive(bool active) { _settings.active = active; }
	const BaseballEventSettings& GetSettings() const { return _settings; }

	void ParsePacket(User* user, TCPConnection::Packet::pointer packet);
	void OnMatchEnd(User* user, unsigned int durationSeconds);

	// Packet sending helpers
	void SendPacket_Status(TCPConnection::pointer connection, const UserBaseballData& data);
	void SendPacket_BattingResult(TCPConnection::pointer connection, const UserBaseballData& data, unsigned char hitResult);

	// User operations
	bool GetOrInitUserData(User* user, UserBaseballData& outData);
	bool CheckDailyReset(User* user, UserBaseballData& data);
	bool AddUserBalls(User* user, int deltaBalls);
	bool ResetUserToday(User* user);
	bool SetUserScore(User* user, unsigned char todayScore, unsigned short totalScore);

	void GrantPrize(User* user, unsigned short itemID, unsigned short count, unsigned int durationDays, InventoryItem* outItem = nullptr);

private:
	BaseballEventSettings _settings;
	std::vector<BaseballScoreRewardTier> _dailyRewards;
	std::mutex _mutex;
};

extern BaseballEventManager baseballEventManager;
