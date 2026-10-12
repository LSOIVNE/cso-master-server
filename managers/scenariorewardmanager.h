#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

class User;

struct ScenarioRewardCandidate {
	unsigned short itemID = 0;
	unsigned int points = 0; // If > 0, grants fixed Points
	std::string name = "";
	unsigned short count = 1;
	short duration = 0; // -1 = permanent, 0 = count item, > 0 = days
	unsigned int weight = 10;
	std::vector<unsigned short> packageItems; // 組合道具包 (like itembox.json PackageItems)
};

enum class ScenarioRewardEntryType {
	DirectItem,
	RandomPool
};

struct ScenarioRewardEntry {
	ScenarioRewardEntryType type = ScenarioRewardEntryType::DirectItem;
	ScenarioRewardCandidate directItem;
	std::vector<ScenarioRewardCandidate> randomItems;
	unsigned short pickCount = 1;
};

struct ScenarioMapReward {
	unsigned char mapID = 0;               // 0 = wildcard/match by name only
	std::string mapName = "";              // filename (e.g. "zs_lostcity") or token (e.g. "#cso_lostcity")
	std::string description = "";          // Human readable description
	unsigned char gameMode = 0;            // 0 = all scenario (15 & 17), 15 = Scenario only, 17 = Human Scenario only
	unsigned short bonusExp = 0;           // Clear bonus EXP
	unsigned short bonusPoints = 0;        // Clear bonus Points
	std::string titleKey = "HSCENARIO_MONSTER_DROP_ITEM_TITLE"; // Native dialog title
	std::string msgKey = "HSCENARIO_MONSTER_DROP_ITEM_GIVE";    // Native dialog message
	std::vector<ScenarioRewardEntry> rewardEntries; // Direct items + random pools (rewarditem.json style)
	bool enabled = true;
};

struct ScenarioBoxReward {
	unsigned int rewardID = 0;                  // e.g. 3000, 3001, 3002
	std::string name = "";                      // e.g. "一般殭屍補給箱"
	std::string titleKey = "";                  // Custom title key (or default)
	std::string msgKey = "";                    // Custom msg key (or default)
	std::vector<ScenarioRewardEntry> rewardEntries; // 隨機道具/Point池或固定獎勵
	bool enabled = true;
};

struct ScenarioBoxRewardSettings {
	bool enabled = true;
	std::string noticeKey = "EVT_SCENARIO_ITEM_POINT_REWARD"; // Native SystemDialog key ("恭喜您獲得{}Point.")
};

struct ScenarioRewardSettings {
	bool enabled = true;
	std::string defaultTitleKey = "HSCENARIO_MONSTER_DROP_ITEM_TITLE";
	std::string defaultMsgKey = "HSCENARIO_MONSTER_DROP_ITEM_GIVE";
};

class ScenarioRewardManager {
public:
	bool Init(const std::string& configPath = "configs/scenario_reward.json");
	bool LoadConfig(const std::string& configPath = "configs/scenario_reward.json");
	bool Reload(const std::string& configPath = "configs/scenario_reward.json") { return LoadConfig(configPath); }

	// Find matching reward for a map and mode
	const ScenarioMapReward* GetRewardForMap(unsigned char mapID, unsigned char gameModeID) const;

	// Grant rewards to user (items + inventory sync + ItemAwardNotice dialog)
	bool GrantScenarioReward(User* user, const ScenarioMapReward* reward, const std::string& mapNameForLog = "");

	// Handle in-game box pickup (Packet_Host GameEvent subtype 34)
	bool HandleBoxPickup(User* user, unsigned int rewardID);
	const ScenarioBoxReward* GetBoxReward(unsigned int rewardID) const;

	size_t GetRewardCount() const { return _rewards.size(); }
	size_t GetBoxRewardCount() const { return _boxRewards.size(); }
	bool IsEnabled() const { return _settings.enabled; }
	bool IsBoxRewardEnabled() const { return _boxSettings.enabled; }

private:
	ScenarioRewardSettings _settings;
	std::vector<ScenarioMapReward> _rewards;
	std::unique_ptr<ScenarioMapReward> _defaultReward;

	ScenarioBoxRewardSettings _boxSettings;
	std::unordered_map<unsigned int, ScenarioBoxReward> _boxRewards;
	std::unique_ptr<ScenarioBoxReward> _defaultBoxReward;
};

extern ScenarioRewardManager scenarioRewardManager;
