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
	unsigned char state = 1; // 0=Inactive, 1=InProgress, 2=Complete, 3=Claimed
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

	bool UpdateQuestProgress(User* user, unsigned int questID, unsigned char condIndex, unsigned short addCount);
	bool CompleteQuest(User* user, unsigned int questID);
	bool ResetUserQuests(User* user);
	bool ClaimReward(User* user, unsigned int questID);

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
