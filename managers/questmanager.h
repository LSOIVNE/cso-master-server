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

	// === 規則引擎 (Rule Engine) 配置 ===
	std::string event = ""; // "kill", "round_win", "match_end", "c4_plant", "c4_defuse", "c4_any", "hostage_rescue", "morale_boost", "supply_box", "wall_upgrade", "boss_kill", "skill_kill", "point_capture", "any"

	std::vector<int> allowedModes;          // 允許模式列表 (若為空則不限模式)
	std::vector<int> disallowedModes;       // 禁止模式列表
	std::vector<std::string> allowedMaps;   // 允許地圖列表 (支援前綴如 "de_", 檔案名如 "de_dust2", 或 ID "5")
	std::vector<std::string> disallowedMaps;// 禁止地圖列表

	int minPlayers = -1; // 最少人數門檻 (-1 表示使用全域或任務層級設定)
	int targetType = 0;  // 0: 不限, 1: 僅限真人玩家(非BOT), 2: 僅限BOT, 3: 僅限殭屍, 4: 僅限人類
	int headshot = 0;    // 0: 不限, 1: 必須爆頭, 2: 必須非爆頭

	std::vector<unsigned char> allowedWeapons; // 允許武器 ID 列表
	std::string weaponCategory = "";          // "any", "knife", "pistol", "grenade"
	std::vector<std::string> allowedBosses;   // Boss 名稱列表 (如 ["starman", "titan"])

	int consecutive = 0;      // 連續達成次數要求 (如 BOT 模式連續 7 次爆頭)
	bool comboInLife = false; // 同一次生命組合擊殺 (如手槍+近戰)
	int streakInLife = 0;     // 同一次生命連殺次數要求
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

	// 任務層級模式與地圖限制 (作為該任務所有條件的預設限制)
	std::vector<int> allowedModes;
	std::vector<int> disallowedModes;
	std::vector<std::string> allowedMaps;
	std::vector<std::string> disallowedMaps;
	std::vector<std::string> candidateMaps; // 候選地圖池 (用於每日特殊任務等隨機抽取單一地圖的任務)
	int minPlayers = -1;
	bool enabled = true;                    // 戰局統計是否開啟 (預設 true)
	bool hasExplicitEnabled = false;        // 是否在 JSON 中明確配置了 enabled

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

struct QuestRewardItem {
	unsigned char type = 0; // 0=Item, 1=Points, 2=EXP, 3=Title, 4=Honor
	unsigned short itemID = 0;
	unsigned char count = 0;
	unsigned char period = 0;
	unsigned int amount = 0;
};

struct RolledQuestReward {
	unsigned short itemID = 0;
	unsigned short count = 1;
	unsigned short duration = 0; // 0=count-based; >0=days
	bool isRevealed = false;
};

struct QuestRewardPoolEntry {
	unsigned short itemID = 0;
	unsigned short count = 1;
	unsigned short duration = 0;
	int weight = 1;
};

struct GameEventContext {
	std::string eventName = "";
	unsigned char gameModeID = 0;
	bool isBotMode = false;
	unsigned char mapID = 0;
	std::string mapFileName = "";
	size_t totalPlayers = 0;
	size_t humanPlayers = 0;

	// 擊殺專用
	bool isHumanVictim = false;
	bool isBotVictim = false;
	bool isZombieVictim = false;
	bool isHeadshot = false;
	unsigned char weaponID = 0;
	std::string bossName = "";
	unsigned short distance = 0;

	// 比賽/回合專用
	bool isWin = false;
	unsigned short kills = 0;
	unsigned short deaths = 0;
	unsigned int durationSec = 0;
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
	unsigned char GetUserSpecialQuestResetCount(User* user);
	void ResetUserSpecialQuestResetCount(User* user);
	void GetUserQuestStats(User* user, struct UserQuestStats& outStats);
	void SyncUserQuestStats(User* user);
	bool ResetSpecialQuest(User* user);
	std::vector<QuestRewardItem> GetQuestPreviewRewards(unsigned int questID);
	std::vector<QuestRewardItem> GetQuestRewardPreview(unsigned int questID, unsigned char rewardIndex);

	RolledQuestReward GetOrRollQuestReward(unsigned long userID, unsigned int questID);
	bool IsQuestRewardRevealed(unsigned long userID, unsigned int questID);
	void SetQuestRewardRevealed(unsigned long userID, unsigned int questID, bool revealed);
	void ClearRolledQuestReward(unsigned long userID, unsigned int questID);
	void ClearUserRolledRewards(unsigned long userID);
	void GrantQuestRewardItem(User* user, unsigned short itemID, unsigned short count, unsigned short duration);

	bool UpdateQuestProgress(User* user, unsigned int questID, unsigned char condIndex, unsigned short addCount);
	bool CompleteQuest(User* user, unsigned int questID);
	bool ResetUserQuests(User* user);
	bool ResetSingleQuest(User* user, unsigned int questID);
	bool ResetDailyQuests(User* user, unsigned int specificQID = 0);
	bool ResetWeeklyQuests(User* user, unsigned int specificQID = 0);
	bool ForceResetSpecialQuest(User* user, unsigned int targetSpecialID = 0);
	bool SetSpecialQuestUnopened(User* user);
	bool ResetAllQuests(User* user);
	bool ClaimReward(User* user, unsigned int questID, unsigned char rewardIndex = 0);
	bool SetQuestTracked(User* user, unsigned int questID, unsigned char tracked);

	// Match event callbacks
	void OnPlayerKilled(User* attacker, User* victim, unsigned char weaponID, bool isHeadshot, unsigned short distance);
	void OnPlayerDied(User* victim);
	void OnRoundWin(User* user);
	void OnMatchEnd(User* user, bool isWin, unsigned short kills, unsigned short deaths, unsigned int durationSec);

	// Specialized game events
	void OnBombEvent(User* user, bool isDefuse);
	void OnHostageRescued(User* user, unsigned short count = 1);
	void OnMoraleBoost(User* user, unsigned char level = 10);
	void OnSupplyBoxPicked(User* user);
	void OnWallUpgraded(User* user);
	void OnBossKilled(User* user, const std::string& bossName);
	void OnSpecialSkillKill(User* user, unsigned int skillID = 0);
	void OnPointCaptured(User* user);

	// Rule Engine & CSV Metadata Helpers
	static std::string GetMapFileName(unsigned char mapID);
	static unsigned char GetMapIDByName(const std::string& mapName);
	static std::string GetMapToken(unsigned char mapID);
	static std::string GetMapTokenByName(const std::string& mapName);
	static std::string GetGameModeName(unsigned char modeID);
	static unsigned char GetGameModeIDByName(const std::string& modeName);
	static unsigned char GetSpecialQuestRequiredMode(unsigned int specialQID);
	static bool IsBombDefusalMap(unsigned char mapID);
	static bool IsHostageRescueMap(unsigned char mapID);
	static bool IsZombieScenarioMap(unsigned char mapID);

	std::string GetUserSpecialQuestAssignedMap(User* user, unsigned int specialQID);
	std::string GetUserSpecialQuestMapToken(User* user, unsigned int specialQID);

	bool IsQuestProgressEnabled(const QuestConfig& qCfg) const;

	void DispatchQuestEvent(User* user, const GameEventContext& ctx);
	bool CheckQuestRule(const QuestConfig& qCfg, const QuestConditionConfig& cond, const GameEventContext& ctx, User* user);

private:
	bool loadGameModeList();
	bool loadMapList();
	bool loadConfigs();
	void initDefaultRewardPools();

	bool _enableDailyQuests = true;
	bool _enableSpecialQuests = true;
	bool _enableWeeklyQuests = true;
	bool _enableHonorQuests = true;

	struct UserSpecialResetInfo {
		int dayIndex = 0;
		unsigned char usedCount = 0;
	};

	struct PlayerLifeStats {
		unsigned short consecutiveHeadshots = 0; // For Quest 3
		bool hadPistolKill = false;              // For Quest 8
		bool hadKnifeKill = false;               // For Quest 8
		unsigned short killStreak = 0;           // For Quest 9
	};

	mutable std::mutex _mutex;
	std::map<unsigned int, QuestConfig> _quests;
	std::map<unsigned long, UserSpecialResetInfo> _userSpecialResetCounts;
	std::vector<QuestRewardPoolEntry> _dailyRewardPool;
	std::vector<QuestRewardPoolEntry> _specialRewardPool;
	std::vector<QuestRewardPoolEntry> _weeklyRewardPool;
	std::map<std::pair<unsigned long, unsigned int>, RolledQuestReward> _userRolledRewards;
	std::map<unsigned long, PlayerLifeStats> _playerLifeStats;
	std::map<std::pair<unsigned long, unsigned int>, std::string> _userSpecialQuestAssignedMaps;

	static std::map<unsigned char, std::string> _mapTable;
	static std::map<std::string, unsigned char> _mapNameToID;
	static std::map<unsigned char, std::string> _mapTokenTable;
	static std::map<std::string, std::string> _mapFileToToken;
	static std::map<unsigned char, std::string> _modeTable;
	static std::map<std::string, unsigned char> _modeNameToID;
};

extern QuestManager questManager;
