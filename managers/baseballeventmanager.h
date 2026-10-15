#pragma once
#include "user.h"
#include "databasemanager.h"
#include <string>
#include <vector>
#include <map>
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
	bool countByScore = false;
	unsigned short countPerScore = 1;
};

struct BaseballEventSettings {
	bool active = true;
	unsigned int playtimeSecondsPerBall = 1200;
	unsigned char maxBalls = 99;
	unsigned int minPlayers = 4;
	std::string testResetTime = "";
	std::string awardTitle = "棒球一個入手!";
	std::string awardContent = "感謝您參與CSO棒球經典賽活動,恭喜您獲得以下獎勵,祝您游戲愉快!";
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
	void OnSecondTick();
	void ProcessInGameTick(User* user, unsigned int deltaSeconds);
	void OnMatchEnd(User* user, unsigned int durationSeconds = 0);
	void OnUserLogout(User* user);
	void OnUserLogin(User* user);
	void OnUserEnterLobby(User* user);

	// Packet sending helpers
	void SendPacket_Status(TCPConnection::pointer connection, const UserBaseballData& data);
	void SendPacket_BattingResult(TCPConnection::pointer connection, const UserBaseballData& data, unsigned char hitResult);

	// User operations
	bool GetOrInitUserData(User* user, UserBaseballData& outData);
	bool CheckDailyReset(User* user, UserBaseballData& data);
	bool ForceDailyReset(User* user);
	bool AddUserBalls(User* user, int deltaBalls);
	bool ResetUserToday(User* user);
	bool SetUserScore(User* user, unsigned char todayScore, unsigned short totalScore);

	void GrantPrize(User* user, unsigned short itemID, unsigned short count, unsigned int durationDays, InventoryItem* outItem = nullptr);
	void FlushUserData(unsigned long userID);
	void FlushAllDirty();

private:
	BaseballEventSettings _settings;
	std::vector<BaseballScoreRewardTier> _dailyRewards;
	std::map<unsigned long, UserBaseballData> _userCache;
	std::map<unsigned long, bool> _dirtyUsers;
	std::mutex _mutex;
};

extern BaseballEventManager baseballEventManager;
