#pragma once

#include <string>
#include <vector>
#include <map>
#include <mutex>

class User;

struct QuestConditionConfig {
	unsigned char index = 0;
	std::string token = "";
	std::string desc = "";
	unsigned short target = 1;
};

struct QuestRewardConfig {
	unsigned char image = 0;
	unsigned int id = 0;
};

struct QuestConfig {
	unsigned int id = 0;
	unsigned char type = 0; // 0=Daily, 1=Special, 2=Honor, 3=Weekly, etc.
	unsigned char npc = 0;
	std::string name = "";
	std::string token = "";
	std::string desc = "";
	std::vector<QuestConditionConfig> conditions;
	std::vector<QuestRewardConfig> rewards;
};

struct UserQuestProgress {
	unsigned int questID = 0;
	unsigned char state = 0; // 0=InProgress (進行), 1=Complete (完成/可領獎), 2=Claimed (已獲得/已領取)
	unsigned char isTracked = 0; // 0=Untracked, 1=Tracked in 任務目錄
	unsigned short counts[6] = { 0 };
	std::string lastUpdatedDate = "";
};

class QuestManager {
public:
	bool Init();
	bool Reload();

	const QuestConfig* GetQuest(unsigned int questID) const;
	const std::map<unsigned int, QuestConfig>& GetAllQuests() const;

	bool GetUserQuestProgress(User* user, unsigned int questID, UserQuestProgress& outProgress);
	void GetUserAllQuestProgress(User* user, std::map<unsigned int, UserQuestProgress>& outMap);
	unsigned int GetUserDailySpecialQuestID(User* user);

	bool UpdateQuestProgress(User* user, unsigned int questID, unsigned char condIndex, unsigned short addCount);
	bool CompleteQuest(User* user, unsigned int questID);
	bool ResetUserQuests(User* user);
	bool ClaimReward(User* user, unsigned int questID);
	bool SetQuestTracked(User* user, unsigned int questID, unsigned char tracked);

	// Match event callbacks
	void OnPlayerKilled(User* attacker, User* victim, unsigned char weaponID, bool isHeadshot, unsigned short distance);
	void OnRoundWin(User* user);
	void OnMatchEnd(User* user, bool isWin, unsigned short kills, unsigned short deaths, unsigned int durationSec);

private:
	bool loadConfigs();

	mutable std::mutex _mutex;
	std::map<unsigned int, QuestConfig> _quests;
};

extern QuestManager questManager;
