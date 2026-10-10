#include "rewarditemmanager.h"
#include "serverconsole.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <cstdlib>

using json = nlohmann::json;

RewardItemManager rewardItemManager;

bool RewardItemManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ RewardItemManager ] Initializing RewardItem system...\n");
	return LoadConfig(configPath);
}

bool RewardItemManager::LoadConfig(const std::string& configPath) {
	std::string resolvedPath = configPath.empty() ? "configs/rewarditem.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Error, format("[ RewardItemManager ] Config file not found: {}\n", resolvedPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ RewardItemManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root = json::parse(file);

		// 1. Settings
		if (root.contains("settings") && root["settings"].is_object()) {
			const auto& s = root["settings"];
			if (s.contains("defaultPoints")) _settings.defaultPoints = s["defaultPoints"].get<unsigned long long>();
			if (s.contains("defaultCBoxCount")) _settings.defaultCBoxCount = s["defaultCBoxCount"].get<unsigned short>();
			if (s.contains("defaultDecoderCount")) _settings.defaultDecoderCount = s["defaultDecoderCount"].get<unsigned short>();
			if (s.contains("defaultTitleKey")) _settings.defaultTitleKey = s["defaultTitleKey"].get<std::string>();
			if (s.contains("defaultMsgKey")) _settings.defaultMsgKey = s["defaultMsgKey"].get<std::string>();
		}

		// 2. RewardItems
		_rewardItems.clear();
		if (root.contains("rewardItems") && root["rewardItems"].is_array()) {
			for (const auto& itemJson : root["rewardItems"]) {
				RewardItemConfig item;
				item.itemID = itemJson.value("itemID", (unsigned short)0);
				item.name = itemJson.value("name", "");
				item.requiredLevel = itemJson.value("requiredLevel", (unsigned char)0);
				item.titleKey = itemJson.value("titleKey", "NEW_LEVELGIFT_GIVE_TITLE");
				item.msgKey = itemJson.value("msgKey", "NEW_LEVELGIFT_GIVE_MSG");
				item.failMsgKey = itemJson.value("failMsgKey", "");

				// Required items
				if (itemJson.contains("requireItems") && itemJson["requireItems"].is_array()) {
					for (const auto& reqJson : itemJson["requireItems"]) {
						RequiredItemEntry req;
						req.itemID = reqJson.value("itemID", (unsigned short)0);
						req.count = reqJson.value("count", (unsigned short)1);
						req.name = reqJson.value("name", "");
						if (req.itemID > 0 && req.count > 0) {
							item.requireItems.push_back(req);
						}
					}
				}

				// Unified rewards
				if (itemJson.contains("rewards") && itemJson["rewards"].is_array()) {
					for (const auto& rJson : itemJson["rewards"]) {
						RewardEntry entry;
						if (rJson.contains("randomItems") && rJson["randomItems"].is_array()) {
							entry.type = RewardEntryType::RandomPool;
							entry.pickCount = rJson.value("pickCount", (unsigned short)1);
							for (const auto& cJson : rJson["randomItems"]) {
								RewardCandidate cand;
								cand.name = cJson.value("name", "");
								cand.weight = cJson.value("weight", (unsigned int)100);
								if (cJson.contains("points")) {
									cand.type = RewardEntryType::Points;
									cand.amount = cJson["points"].get<unsigned long long>();
								}
								else if (cJson.contains("exp")) {
									cand.type = RewardEntryType::Exp;
									cand.amount = cJson["exp"].get<unsigned long long>();
								}
								else {
									cand.type = RewardEntryType::Item;
									cand.itemID = cJson.value("itemID", (unsigned short)0);
									cand.duration = cJson.value("duration", (short)0);
									cand.count = cJson.value("count", (unsigned short)1);
								}
								entry.randomItems.push_back(cand);
							}
						}
						else if (rJson.contains("points")) {
							entry.type = RewardEntryType::Points;
							entry.amount = rJson["points"].get<unsigned long long>();
							entry.name = rJson.value("name", format("{} Point", entry.amount));
						}
						else if (rJson.contains("exp")) {
							entry.type = RewardEntryType::Exp;
							entry.amount = rJson["exp"].get<unsigned long long>();
							entry.name = rJson.value("name", format("{} EXP", entry.amount));
						}
						else {
							entry.type = RewardEntryType::Item;
							entry.itemID = rJson.value("itemID", (unsigned short)0);
							entry.duration = rJson.value("duration", (short)0);
							entry.count = rJson.value("count", (unsigned short)1);
							entry.name = rJson.value("name", "");
						}
						item.rewards.push_back(entry);
					}
				}

				if (item.itemID > 0) {
					_rewardItems[item.itemID] = item;
				}
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ RewardItemManager ] Loaded {} reward items from {}\n",
			_rewardItems.size(), resolvedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ RewardItemManager ] JSON parse error: {}\n", e.what()));
		return false;
	}
}

bool RewardItemManager::HasRewardItem(unsigned short itemID) const {
	return _rewardItems.find(itemID) != _rewardItems.end();
}

const RewardItemConfig* RewardItemManager::GetRewardItem(unsigned short itemID) const {
	auto it = _rewardItems.find(itemID);
	if (it != _rewardItems.end()) {
		return &it->second;
	}
	return nullptr;
}

bool RewardItemManager::RollRewards(unsigned short itemID, RolledRewards& out) const {
	out.items.clear();
	out.points = 0;
	out.exp = 0;

	auto it = _rewardItems.find(itemID);
	if (it == _rewardItems.end()) {
		// Fallback to default settings
		out.points = _settings.defaultPoints;
		out.exp = 0;
		out.titleKey = _settings.defaultTitleKey;
		out.msgKey = _settings.defaultMsgKey;
		if (_settings.defaultCBoxCount > 0) {
			out.items.push_back({ 166, 0, _settings.defaultCBoxCount, "C-Box" });
		}
		if (_settings.defaultDecoderCount > 0) {
			out.items.push_back({ 173, 0, _settings.defaultDecoderCount, "電子解碼器" });
		}
		return true;
	}

	const auto& cfg = it->second;
	out.titleKey = cfg.titleKey;
	out.msgKey = cfg.msgKey;

	// Process unified rewards
	for (const auto& entry : cfg.rewards) {
		if (entry.type == RewardEntryType::Points) {
			out.points += entry.amount;
		}
		else if (entry.type == RewardEntryType::Exp) {
			out.exp += entry.amount;
		}
		else if (entry.type == RewardEntryType::Item) {
			RewardItemSubItem sub;
			sub.itemID = entry.itemID;
			sub.duration = entry.duration;
			sub.count = entry.count;
			sub.name = entry.name;
			out.items.push_back(sub);
		}
		else if (entry.type == RewardEntryType::RandomPool) {
			if (entry.randomItems.empty() || entry.pickCount == 0) continue;

			std::vector<RewardCandidate> candidates = entry.randomItems;
			unsigned short toPick = entry.pickCount;
			if (toPick > candidates.size()) toPick = (unsigned short)candidates.size();

			for (unsigned short p = 0; p < toPick && !candidates.empty(); p++) {
				unsigned int totalWeight = 0;
				for (const auto& c : candidates) {
					totalWeight += (c.weight > 0 ? c.weight : 1);
				}
				if (totalWeight == 0) break;

				unsigned int roll = (unsigned int)(rand() % totalWeight);
				unsigned int acc = 0;
				for (auto cIt = candidates.begin(); cIt != candidates.end(); ++cIt) {
					acc += (cIt->weight > 0 ? cIt->weight : 1);
					if (roll < acc) {
						if (cIt->type == RewardEntryType::Points) {
							out.points += cIt->amount;
						}
						else if (cIt->type == RewardEntryType::Exp) {
							out.exp += cIt->amount;
						}
						else {
							RewardItemSubItem granted;
							granted.itemID = cIt->itemID;
							granted.duration = cIt->duration;
							granted.count = cIt->count;
							granted.name = cIt->name;
							out.items.push_back(granted);
						}
						candidates.erase(cIt);
						break;
					}
				}
			}
		}
	}

	return true;
}
