#pragma once

#include <string>
#include <vector>
#include <unordered_map>
#include <memory>

class User;

struct ScenarioRewardCandidate {
	unsigned short itemID = 0;
	std::string name = "";
	unsigned short count = 1;
	short duration = 0; // -1 = permanent, 0 = count item, > 0 = days
	unsigned int weight = 10;
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

	size_t GetRewardCount() const { return _rewards.size(); }
	bool IsEnabled() const { return _settings.enabled; }

private:
	ScenarioRewardSettings _settings;
	std::vector<ScenarioMapReward> _rewards;
	std::unique_ptr<ScenarioMapReward> _defaultReward;
};

extern ScenarioRewardManager scenarioRewardManager;
