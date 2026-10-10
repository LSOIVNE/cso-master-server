#pragma once
#include <string>
#include <vector>
#include <unordered_map>

enum class RewardEntryType {
	Item,
	Points,
	Exp,
	RandomPool
};

struct RewardCandidate {
	RewardEntryType type = RewardEntryType::Item;
	unsigned short itemID = 0;
	short duration = 0; // -1 = perm, > 0 = days, 0 = count
	unsigned short count = 1;
	unsigned long long amount = 0; // for points or exp
	std::string name = "";
	unsigned int weight = 100;
};

struct RewardEntry {
	RewardEntryType type = RewardEntryType::Item;
	unsigned short itemID = 0;
	short duration = 0;
	unsigned short count = 1;
	unsigned long long amount = 0; // for points or exp
	std::string name = "";

	// For RandomPool
	unsigned short pickCount = 1;
	std::vector<RewardCandidate> randomItems;
};

struct RequiredItemEntry {
	unsigned short itemID = 0;
	unsigned short count = 1;
	std::string name = "";
};

struct RewardItemConfig {
	unsigned short itemID = 0;
	std::string name = "";
	unsigned char requiredLevel = 0;
	std::string titleKey = "NEW_LEVELGIFT_GIVE_TITLE";
	std::string msgKey = "NEW_LEVELGIFT_GIVE_MSG";
	std::string failMsgKey = "";
	std::vector<RequiredItemEntry> requireItems;
	std::vector<RewardEntry> rewards;
};

struct RewardItemSettings {
	unsigned long long defaultPoints = 10000;
	unsigned short defaultCBoxCount = 5;
	unsigned short defaultDecoderCount = 5;
	std::string defaultTitleKey = "NEW_LEVELGIFT_GIVE_TITLE";
	std::string defaultMsgKey = "NEW_LEVELGIFT_GIVE_MSG";
};

struct RewardItemSubItem {
	unsigned short itemID = 0;
	short duration = 0; // -1 = perm, > 0 = days, 0 = count
	unsigned short count = 1;
	std::string name = "";
};

struct RolledRewards {
	std::vector<RewardItemSubItem> items;
	unsigned long long points = 0;
	unsigned long long exp = 0;
	std::string titleKey = "NEW_LEVELGIFT_GIVE_TITLE";
	std::string msgKey = "NEW_LEVELGIFT_GIVE_MSG";
};

class RewardItemManager {
public:
	bool Init(const std::string& configPath = "configs/rewarditem.json");
	bool LoadConfig(const std::string& configPath = "configs/rewarditem.json");
	bool Reload(const std::string& configPath = "configs/rewarditem.json") { return LoadConfig(configPath); }
	size_t GetRewardItemCount() const { return _rewardItems.size(); }

	bool HasRewardItem(unsigned short itemID) const;
	const RewardItemConfig* GetRewardItem(unsigned short itemID) const;
	const RewardItemSettings& GetSettings() const { return _settings; }

	// Rolls fixed rewards + random items (or falls back to default settings)
	bool RollRewards(unsigned short itemID, RolledRewards& out) const;

private:
	RewardItemSettings _settings;
	std::unordered_map<unsigned short, RewardItemConfig> _rewardItems;
};

extern RewardItemManager rewardItemManager;
