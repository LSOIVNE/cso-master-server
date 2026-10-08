#include "questmanager.h"
#include "packet_questmanager.h"
#include "packet_umsgmanager.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_roommanager.h"
#include "titlemanager.h"
#include "databasemanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "room.h"
#include "serverconfig.h"
#include "serverconsole.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <format>
#include <filesystem>
#include <ctime>
#include <random>

using namespace std;
namespace fs = std::filesystem;

static std::mt19937& GetQuestRNG() {
	static thread_local std::mt19937 rng(std::random_device{}());
	return rng;
}

map<unsigned char, string> QuestManager::_mapTable;
map<string, unsigned char> QuestManager::_mapNameToID;
map<unsigned char, string> QuestManager::_mapTokenTable;
map<string, string> QuestManager::_mapFileToToken;
map<unsigned char, string> QuestManager::_modeTable;
map<string, unsigned char> QuestManager::_modeNameToID;

bool QuestManager::loadGameModeList() {
	const vector<string> searchPaths = {
		"configs/gamemodelist.csv",
		"../configs/gamemodelist.csv",
		"bin/configs/gamemodelist.csv",
		"D:/agy/CounterStrike/CSOTW/cso-master-server/configs/gamemodelist.csv"
	};

	string validPath = "";
	for (const auto& path : searchPaths) {
		if (fs::exists(path)) {
			validPath = path;
			break;
		}
	}

	if (validPath.empty()) {
		serverConsole.Print(PrefixType::Warn, "[ QuestManager ] gamemodelist.csv not found in configs, using defaults.\n");
		return false;
	}

	ifstream file(validPath);
	if (!file.is_open()) return false;

	_modeTable.clear();
	_modeNameToID.clear();

	string line;
	while (getline(file, line)) {
		if (line.empty()) continue;
		if (line[0] == '#' || line[0] == ';') {
			// Skip header
			continue;
		}

		stringstream ss(line);
		string idStr, nameStr;
		if (getline(ss, idStr, ',') && getline(ss, nameStr, ',')) {
			try {
				unsigned char modeID = (unsigned char)stoul(idStr);
				_modeTable[modeID] = nameStr;

				string alias = nameStr;
				if (alias.starts_with("#CSO_GameMode_")) {
					alias = alias.substr(14);
				}
				else if (alias.starts_with("#CSO_")) {
					alias = alias.substr(5);
				}
				transform(alias.begin(), alias.end(), alias.begin(), [](unsigned char c) { return (char)tolower(c); });
				_modeNameToID[alias] = modeID;

				// Common aliases
				if (alias == "original" || alias == "lightori") _modeNameToID["orig"] = modeID;
				if (alias == "deathmatch") { _modeNameToID["dm"] = modeID; }
				if (alias == "teamdeathmatch") { _modeNameToID["tdm"] = modeID; }
				if (alias == "zombi") { _modeNameToID["zombie"] = modeID; _modeNameToID["zombie_classic"] = modeID; }
				if (alias == "zombi_expand") { _modeNameToID["zombie_mutation"] = modeID; }
				if (alias == "zombi_3") { _modeNameToID["zombie_hero"] = modeID; }
				if (alias == "scenario") { _modeNameToID["zombie_scenario"] = modeID; }
			}
			catch (...) {}
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] Successfully loaded {} game modes from {}\n", _modeTable.size(), validPath));
	return true;
}

bool QuestManager::loadMapList() {
	const vector<string> searchPaths = {
		"configs/maplist.csv",
		"../configs/maplist.csv",
		"bin/configs/maplist.csv",
		"D:/agy/CounterStrike/CSOTW/cso-master-server/configs/maplist.csv"
	};

	string validPath = "";
	for (const auto& path : searchPaths) {
		if (fs::exists(path)) {
			validPath = path;
			break;
		}
	}

	if (validPath.empty()) {
		serverConsole.Print(PrefixType::Warn, "[ QuestManager ] maplist.csv not found in configs, using defaults.\n");
		return false;
	}

	ifstream file(validPath);
	if (!file.is_open()) return false;

	_mapTable.clear();
	_mapNameToID.clear();
	_mapTokenTable.clear();
	_mapFileToToken.clear();

	string line;
	while (getline(file, line)) {
		if (line.empty()) continue;
		if (line[0] == '#' || line[0] == ';') {
			continue;
		}

		stringstream ss(line);
		string idStr, nameStr, fileStr;
		if (getline(ss, idStr, ',') && getline(ss, nameStr, ',') && getline(ss, fileStr, ',')) {
			try {
				unsigned char mapID = (unsigned char)stoul(idStr);
				// Trim quotes or spaces
				fileStr.erase(remove(fileStr.begin(), fileStr.end(), '\"'), fileStr.end());
				while (!fileStr.empty() && isspace((unsigned char)fileStr.front())) fileStr.erase(fileStr.begin());
				while (!fileStr.empty() && isspace((unsigned char)fileStr.back())) fileStr.pop_back();

				nameStr.erase(remove(nameStr.begin(), nameStr.end(), '\"'), nameStr.end());
				while (!nameStr.empty() && isspace((unsigned char)nameStr.front())) nameStr.erase(nameStr.begin());
				while (!nameStr.empty() && isspace((unsigned char)nameStr.back())) nameStr.pop_back();

				_mapTable[mapID] = fileStr;
				_mapTokenTable[mapID] = nameStr;

				string lowerFile = fileStr;
				transform(lowerFile.begin(), lowerFile.end(), lowerFile.begin(), [](unsigned char c) { return (char)tolower(c); });
				_mapNameToID[lowerFile] = mapID;
				_mapFileToToken[lowerFile] = nameStr;
			}
			catch (...) {}
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] Successfully loaded {} maps from {}\n", _mapTable.size(), validPath));
	return true;
}

string QuestManager::GetMapFileName(unsigned char mapID) {
	auto it = _mapTable.find(mapID);
	if (it != _mapTable.end()) {
		return it->second;
	}
	return "map_" + to_string((int)mapID);
}

unsigned char QuestManager::GetMapIDByName(const string& mapName) {
	string lower = mapName;
	transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
	auto it = _mapNameToID.find(lower);
	if (it != _mapNameToID.end()) {
		return it->second;
	}
	return 0xFF;
}

string QuestManager::GetMapToken(unsigned char mapID) {
	auto it = _mapTokenTable.find(mapID);
	if (it != _mapTokenTable.end()) {
		return it->second;
	}
	return "";
}

string QuestManager::GetMapTokenByName(const string& mapName) {
	string lower = mapName;
	transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
	auto it = _mapFileToToken.find(lower);
	if (it != _mapFileToToken.end()) {
		return it->second;
	}
	auto idIt = _mapNameToID.find(lower);
	if (idIt != _mapNameToID.end()) {
		return GetMapToken(idIt->second);
	}
	return "";
}

string QuestManager::GetGameModeName(unsigned char modeID) {
	auto it = _modeTable.find(modeID);
	if (it != _modeTable.end()) {
		return it->second;
	}
	return "mode_" + to_string((int)modeID);
}

unsigned char QuestManager::GetGameModeIDByName(const string& modeName) {
	string lower = modeName;
	transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char c) { return (char)tolower(c); });
	auto it = _modeNameToID.find(lower);
	if (it != _modeNameToID.end()) {
		return it->second;
	}
	return 0xFF;
}

static vector<int> ParseModeList(const nlohmann::json& node) {
	vector<int> res;
	if (node.is_number()) {
		res.push_back(node.get<int>());
	}
	else if (node.is_string()) {
		string s = node.get<string>();
		transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		// Group aliases
		if (s == "bot") { res.push_back(3); res.push_back(4); res.push_back(5); return res; }
		if (s == "pvp") { res.push_back(0); res.push_back(1); res.push_back(2); return res; }
		if (s == "zombie" || s == "zombi") { res.push_back(8); res.push_back(9); res.push_back(14); return res; }

		unsigned char foundID = QuestManager::GetGameModeIDByName(s);
		if (foundID != 0xFF) {
			res.push_back(foundID);
		}
		else {
			try { res.push_back(stoi(s)); } catch (...) {}
		}
	}
	else if (node.is_array()) {
		for (const auto& el : node) {
			auto sub = ParseModeList(el);
			res.insert(res.end(), sub.begin(), sub.end());
		}
	}
	return res;
}

static vector<string> ParseStringList(const nlohmann::json& node) {
	vector<string> res;
	if (node.is_string()) {
		string s = node.get<string>();
		transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
		res.push_back(s);
	}
	else if (node.is_number()) {
		res.push_back(to_string(node.get<int>()));
	}
	else if (node.is_array()) {
		for (const auto& el : node) {
			auto sub = ParseStringList(el);
			res.insert(res.end(), sub.begin(), sub.end());
		}
	}
	return res;
}

QuestManager questManager;

bool QuestManager::Init() {
	loadGameModeList();
	loadMapList();
	initDefaultRewardPools();
	return loadConfigs();
}

bool QuestManager::Reload() {
	loadGameModeList();
	loadMapList();
	return loadConfigs();
}

bool QuestManager::loadConfigs() {
	lock_guard<mutex> lock(_mutex);
	_quests.clear();

	const vector<string> searchPaths = {
		"configs/quest.json",
		"../configs/quest.json",
		"bin/configs/quest.json",
		"D:/agy/CounterStrike/CSOTW/cso-master-server/configs/quest.json"
	};

	string validPath = "";
	for (const auto& path : searchPaths) {
		if (fs::exists(path)) {
			validPath = path;
			break;
		}
	}

	if (validPath.empty()) {
		serverConsole.Print(PrefixType::Error, "[ QuestManager ] Cannot open quest.json from any search path!\n");
		return false;
	}

	try {
		ifstream f(validPath);
		if (!f.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ QuestManager ] Failed to open {}\n", validPath));
			return false;
		}

		nlohmann::json j;
		f >> j;

		auto parsePool = [](const nlohmann::json& arr, vector<QuestRewardPoolEntry>& outPool) {
			if (!arr.is_array()) return;
			vector<QuestRewardPoolEntry> temp;
			for (const auto& item : arr) {
				QuestRewardPoolEntry e;
				e.itemID = (unsigned short)item.value("itemID", 0);
				e.count = (unsigned short)item.value("count", 1);
				e.duration = (unsigned short)item.value("duration", 0);
				e.weight = item.value("weight", 1);
				if (e.itemID > 0 && e.weight > 0) {
					temp.push_back(e);
				}
			}
			if (!temp.empty()) {
				outPool = std::move(temp);
			}
		};

		if (j.is_object()) {
			if (j.contains("Settings")) {
				const auto& s = j["Settings"];
				_enableDailyQuests = s.value("enable_daily", true);
				_enableSpecialQuests = s.value("enable_special", true);
				_enableWeeklyQuests = s.value("enable_weekly", true);
				_enableHonorQuests = s.value("enable_honor", true);
			}
			if (j.contains("enable_daily")) _enableDailyQuests = j["enable_daily"].get<bool>();
			if (j.contains("enable_special")) _enableSpecialQuests = j["enable_special"].get<bool>();
			if (j.contains("enable_weekly")) _enableWeeklyQuests = j["enable_weekly"].get<bool>();
			if (j.contains("enable_honor")) _enableHonorQuests = j["enable_honor"].get<bool>();

			if (j.contains("DailyRewards")) {
				parsePool(j["DailyRewards"], _dailyRewardPool);
			}
			if (j.contains("SpecialRewards")) {
				parsePool(j["SpecialRewards"], _specialRewardPool);
			}
			if (j.contains("WeeklyRewards")) {
				parsePool(j["WeeklyRewards"], _weeklyRewardPool);
			}
		}

		const nlohmann::json& questsArr = j.is_array() ? j : (j.contains("Quests") ? j["Quests"] : nlohmann::json::array());

		for (const auto& item : questsArr) {
			QuestConfig cfg;
			cfg.id = item.value("id", 0u);
			cfg.type = (unsigned char)item.value("type", 0);
			cfg.npc = (unsigned char)item.value("npc", 0);
			cfg.name = item.value("name", "");
			cfg.token = item.value("token", "");
			cfg.desc = item.value("desc", "");

			if (item.contains("enabled")) {
				cfg.enabled = item["enabled"].get<bool>();
				cfg.hasExplicitEnabled = true;
			}
			else if (item.contains("active")) {
				cfg.enabled = item["active"].get<bool>();
				cfg.hasExplicitEnabled = true;
			}

			if (item.contains("allowed_modes")) cfg.allowedModes = ParseModeList(item["allowed_modes"]);
			else if (item.contains("modes")) cfg.allowedModes = ParseModeList(item["modes"]);

			if (item.contains("disallowed_modes")) cfg.disallowedModes = ParseModeList(item["disallowed_modes"]);

			if (item.contains("allowed_maps")) cfg.allowedMaps = ParseStringList(item["allowed_maps"]);
			else if (item.contains("maps")) cfg.allowedMaps = ParseStringList(item["maps"]);

			if (item.contains("candidate_maps")) cfg.candidateMaps = ParseStringList(item["candidate_maps"]);

			if (item.contains("disallowed_maps")) cfg.disallowedMaps = ParseStringList(item["disallowed_maps"]);

			if (item.contains("min_players")) cfg.minPlayers = item["min_players"].get<int>();

			if (item.contains("conditions")) {
				for (const auto& c : item["conditions"]) {
					QuestConditionConfig cond;
					cond.index = (unsigned char)c.value("index", 0);
					cond.token = c.value("token", "");
					cond.desc = c.value("desc", "");
					cond.target = (unsigned short)c.value("target", 1);

					cond.event = c.value("event", "");

					if (c.contains("allowed_modes")) cond.allowedModes = ParseModeList(c["allowed_modes"]);
					else if (c.contains("modes")) cond.allowedModes = ParseModeList(c["modes"]);

					if (c.contains("disallowed_modes")) cond.disallowedModes = ParseModeList(c["disallowed_modes"]);

					if (c.contains("allowed_maps")) cond.allowedMaps = ParseStringList(c["allowed_maps"]);
					else if (c.contains("maps")) cond.allowedMaps = ParseStringList(c["maps"]);

					if (c.contains("disallowed_maps")) cond.disallowedMaps = ParseStringList(c["disallowed_maps"]);

					if (c.contains("min_players")) cond.minPlayers = c["min_players"].get<int>();

					if (c.contains("target_type")) cond.targetType = c["target_type"].get<int>();
					if (c.value("require_human", false)) cond.targetType = 1;
					if (c.value("require_bot", false)) cond.targetType = 2;
					if (c.value("require_zombie", false)) cond.targetType = 3;

					if (c.contains("headshot")) cond.headshot = c["headshot"].get<int>();
					if (c.value("require_headshot", false)) cond.headshot = 1;

					if (c.contains("weapons")) {
						for (const auto& w : c["weapons"]) {
							if (w.is_number()) cond.allowedWeapons.push_back((unsigned char)w.get<int>());
						}
					}
					if (c.contains("weapon_category")) cond.weaponCategory = c["weapon_category"].get<string>();

					if (c.contains("bosses")) cond.allowedBosses = ParseStringList(c["bosses"]);
					else if (c.contains("boss")) cond.allowedBosses = ParseStringList(c["boss"]);

					cond.consecutive = c.value("consecutive", 0);
					cond.comboInLife = c.value("combo_in_life", false);
					cond.streakInLife = c.value("streak_in_life", 0);

					cfg.conditions.push_back(cond);
				}
			}

			if (item.contains("rewards")) {
				for (const auto& r : item["rewards"]) {
					QuestRewardConfig rw;
					rw.image = (unsigned char)r.value("image", 0);
					rw.id = r.value("id", 0u);
					cfg.rewards.push_back(rw);
				}
			}

			_quests[cfg.id] = cfg;
		}

		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] Successfully loaded {} quests and reward pools (Daily: {}, Special: {}, Weekly: {}) from {}\n",
			_quests.size(), _dailyRewardPool.size(), _specialRewardPool.size(), _weeklyRewardPool.size(), validPath));
		return true;
	}
	catch (const exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ QuestManager ] Failed to parse quest.json: {}\n", e.what()));
		return false;
	}
}

const QuestConfig* QuestManager::GetQuest(unsigned int questID) const {
	lock_guard<mutex> lock(_mutex);
	auto it = _quests.find(questID);
	return (it != _quests.end()) ? &it->second : nullptr;
}

const map<unsigned int, QuestConfig>& QuestManager::GetAllQuests() const {
	return _quests;
}

bool QuestManager::IsQuestProgressEnabled(const QuestConfig& qCfg) const {
	if (qCfg.hasExplicitEnabled) {
		return qCfg.enabled;
	}
	switch (qCfg.type) {
		case 0: return _enableDailyQuests;
		case 1: return _enableSpecialQuests;
		case 2: return _enableHonorQuests;
		case 3: return _enableWeeklyQuests;
		default: return true;
	}
}

bool QuestManager::GetUserQuestProgress(User* user, unsigned int questID, UserQuestProgress& outProgress) {
	if (user == NULL || questID == 0) return false;

	map<unsigned int, UserQuestRecord> dbQuests;
	databaseManager.GetUserQuests(user->GetUserID(), dbQuests);

	auto it = dbQuests.find(questID);
	if (it != dbQuests.end()) {
		outProgress.questID = questID;
		outProgress.state = it->second.state;
		outProgress.isTracked = it->second.isTracked;
		outProgress.counts[0] = it->second.count0;
		outProgress.counts[1] = it->second.count1;
		outProgress.counts[2] = it->second.count2;
		outProgress.counts[3] = it->second.count3;
		outProgress.counts[4] = it->second.count4;
		outProgress.counts[5] = it->second.count5;
		outProgress.lastUpdatedDate = it->second.lastUpdatedDate;
		return true;
	}

	// Default progress: InProgress (0), 0 counts. Special quest defaults to Unopened (3)
	outProgress.questID = questID;
	outProgress.state = (questID >= 1001 && questID <= 1019) ? 3 : 0;
	outProgress.isTracked = (questID <= 3) ? 1 : 0; // Default track first 3 daily quests
	for (int i = 0; i < 6; ++i) outProgress.counts[i] = 0;

	// Check if Honor quest already unlocked in TitleManager
	if (questID >= 2001 && questID <= 2075) {
		unsigned short titleID = (unsigned short)(questID - 2000);
		if (databaseManager.HasUserTitle(user->GetUserID(), titleID)) {
			outProgress.state = 2; // Claimed / 已獲得
			const QuestConfig* q = GetQuest(questID);
			if (q) {
				for (size_t c = 0; c < q->conditions.size() && c < 6; ++c) {
					outProgress.counts[c] = q->conditions[c].target;
				}
			}
		}
	}

	return true;
}

void QuestManager::GetUserAllQuestProgress(User* user, map<unsigned int, UserQuestProgress>& outMap) {
	outMap.clear();
	if (user == NULL) return;

	// Reset daily quests that have expired
	databaseManager.ResetDailyQuests(user->GetUserID());

	map<unsigned int, UserQuestRecord> dbQuests;
	databaseManager.GetUserQuests(user->GetUserID(), dbQuests);

	for (const auto& [qid, rec] : dbQuests) {
		UserQuestProgress prog;
		prog.questID = qid;
		prog.state = rec.state;
		prog.isTracked = rec.isTracked;
		prog.counts[0] = rec.count0;
		prog.counts[1] = rec.count1;
		prog.counts[2] = rec.count2;
		prog.counts[3] = rec.count3;
		prog.counts[4] = rec.count4;
		prog.counts[5] = rec.count5;
		prog.lastUpdatedDate = rec.lastUpdatedDate;
		outMap[qid] = prog;
	}
}

unsigned int QuestManager::GetUserDailySpecialQuestID(User* user) {
	if (user == NULL) return 1001;

	map<unsigned int, UserQuestRecord> dbQuests;
	databaseManager.GetUserQuests(user->GetUserID(), dbQuests);

	for (const auto& [qid, rec] : dbQuests) {
		if (qid >= 1001 && qid <= 1019) {
			return qid;
		}
	}

	// Deterministically assign one based on user ID and day of year
	time_t now = time(nullptr);
	int dayIndex = (int)(now / 86400);
	unsigned int assignedID = 1001 + (unsigned int)((user->GetUserID() * 7 + dayIndex) % 19);

	// Initialize record in DB so it stays assigned for today (state = 3 Unopened cover)
	UserQuestRecord rec;
	rec.questID = assignedID;
	rec.state = 3;
	rec.isTracked = 0;
	databaseManager.SaveUserQuest(user->GetUserID(), rec);

	return assignedID;
}

unsigned char QuestManager::GetUserSpecialQuestResetCount(User* user) {
	if (user == NULL) return 0;
	lock_guard<mutex> lock(_mutex);

	time_t now = time(nullptr);
	int currentDay = (int)(now / 86400);

	auto& info = _userSpecialResetCounts[user->GetUserID()];
	if (info.dayIndex != currentDay) {
		info.dayIndex = currentDay;
		info.usedCount = 0;
	}

	return (info.usedCount >= 5) ? 0 : (unsigned char)(5 - info.usedCount);
}

void QuestManager::ResetUserSpecialQuestResetCount(User* user) {
	if (user == NULL) return;
	{
		lock_guard<mutex> lock(_mutex);
		time_t now = time(nullptr);
		int currentDay = (int)(now / 86400);
		auto& info = _userSpecialResetCounts[user->GetUserID()];
		info.dayIndex = currentDay;
		info.usedCount = 0;
	}
	SyncUserQuestStats(user);
}

void QuestManager::GetUserQuestStats(User* user, UserQuestStats& outStats) {
	if (user == NULL) return;

	databaseManager.GetUserQuestStats(user->GetUserID(), outStats);
	outStats.resetSpecialQuestCount = GetUserSpecialQuestResetCount(user);
}

void QuestManager::SyncUserQuestStats(User* user) {
	if (user == NULL) return;
	auto connection = user->GetConnection();
	if (connection == NULL) return;

	UserQuestStats stats;
	GetUserQuestStats(user, stats);
	packet_QuestManager.SendPacket_Quest_UpdateStats(connection, stats);
}

bool QuestManager::ResetSpecialQuest(User* user) {
	if (user == NULL) return false;

	unsigned char remaining = GetUserSpecialQuestResetCount(user);
	if (remaining == 0) {
		serverConsole.Print(PrefixType::Warn, format("[ QuestManager ] User ({}) tried to reset special quest, but reset count exhausted!\n",
			user->GetUserLogName()));
		return false;
	}

	// 檢查並扣減 1 個 378 (特殊任務重置券)
	unsigned long userID = user->GetUserID();
	unsigned short slotID = 0;
	bool deleted = false;
	if (!databaseManager.ConsumeUserInventoryItem(userID, 378, 1, slotID, deleted)) {
		serverConsole.Print(PrefixType::Warn, format("[ QuestManager ] User ({}) tried to reset special quest, but has no Item 378 (特殊任務重置券)!\n",
			user->GetUserLogName()));
		return false;
	}

	// 扣除成功，即時同步背包 Delta 給客戶端
	auto connection = user->GetConnection();
	if (connection != NULL) {
		if (deleted) {
			packetManager.SendPacket_InventoryDelta(connection, {}, { slotID });
		}
		else {
			const auto inv = databaseManager.GetUserInventory(userID);
			for (const auto& it : inv) {
				if (it.slotID == slotID) {
					packetManager.SendPacket_InventoryDelta(connection, { it }, {});
					break;
				}
			}
		}
	}

	unsigned int currentSpecialID = GetUserDailySpecialQuestID(user);

	// Pick a new special quest (1001 ~ 1019) distinct from current
	unsigned int newSpecialID = currentSpecialID;
	std::uniform_int_distribution<unsigned int> distSpecial(1001, 1019);
	for (int attempts = 0; attempts < 10; ++attempts) {
		unsigned int candidate = distSpecial(GetQuestRNG());
		if (candidate != currentSpecialID) {
			newSpecialID = candidate;
			break;
		}
	}
	if (newSpecialID == currentSpecialID) {
		newSpecialID = (currentSpecialID >= 1019) ? 1001 : (currentSpecialID + 1);
	}

	// Clear cached rolled reward and assigned map for the old special quest
	ClearRolledQuestReward(user->GetUserID(), currentSpecialID);
	{
		lock_guard<mutex> lock(_mutex);
		_userSpecialQuestAssignedMaps.erase(make_pair(user->GetUserID(), currentSpecialID));
	}

	// Delete old special quest record from DB
	databaseManager.DeleteUserSpecialQuest(user->GetUserID());

	// Save new special quest record (state = 0 InProgress, refreshed directly in-place)
	UserQuestRecord rec;
	rec.questID = newSpecialID;
	rec.state = 0;
	rec.isTracked = 0;
	databaseManager.SaveUserQuest(user->GetUserID(), rec);

	// Update reset counter
	{
		lock_guard<mutex> lock(_mutex);
		_userSpecialResetCounts[user->GetUserID()].usedCount++;
	}

	// 即時下發 Subtype 14，驅動客戶端 CQuestDetailPanel 立即刷新按鈕文字 "重置特殊任務(%d)"
	SyncUserQuestStats(user);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) consumed 1 Reset Ticket (378) and reset special quest from {} to {}, remaining resets: {}\n",
		user->GetUserLogName(), currentSpecialID, newSpecialID, (int)GetUserSpecialQuestResetCount(user)));
	return true;
}

void QuestManager::initDefaultRewardPools() {
	_dailyRewardPool = {
		// 經典隨機武器 (1日)
		{ 4, 1, 1, 10 },    // 夜鷹 .50C (1日)
		{ 11, 1, 1, 10 },   // ES C90 (1日)
		{ 14, 1, 1, 10 },   // 麥格農狙擊槍 AWP (1日)
		{ 15, 1, 1, 10 },   // M4A1 卡賓 (1日)
		{ 21, 1, 1, 10 },   // CV-47 (AK-47) (1日)
		{ 36, 1, 1, 10 },   // K1A (1日)
		{ 37, 1, 1, 10 },   // MP7A1 (1日)
		{ 38, 1, 1, 10 },   // USAS12 (1日)
		{ 149, 1, 1, 10 },  // 森蚺左輪 (1日)
		{ 245, 1, 1, 8 },   // 紅白雙星 (1日)
		{ 282, 1, 1, 8 },   // 虎狩MP5 (1日)
		{ 261, 1, 1, 6 },   // 噬血榴炮-M79 (1日)
		{ 270, 1, 1, 6 },   // 金蠍雙擊 (1日)
		// 勳章與道具
		{ 1014, 1, 0, 12 }, // 黃銅經驗值勳章 (數量 1)
		{ 1013, 1, 0, 8 },  // 白銀經驗值勳章 (數量 1)
		{ 173, 1, 0, 6 }    // 電子解碼器 (數量 1)
	};

	_specialRewardPool = {
		// 高級隨機武器 (3日)
		{ 109, 1, 3, 10 },  // 毀滅者 MG3 (3日)
		{ 146, 1, 3, 10 },  // 死亡騎士 SKULL-5 (3日)
		{ 170, 1, 3, 8 },   // 黃金CV-47 (3日)
		{ 171, 1, 3, 8 },   // 迷彩M4A1 (3日)
		{ 242, 1, 3, 8 },   // 黑白雙煞 (3日)
		{ 283, 1, 3, 8 },   // 飛鷹十字弓 (3日)
		{ 304, 1, 3, 8 },   // 黑色旋風-M14 (3日)
		{ 337, 1, 3, 8 },   // 疾風獵鷹-WA2000 (3日)
		{ 365, 1, 3, 6 },   // 戰慄加農砲 (3日)
		{ 398, 1, 3, 6 },   // 喋血亂舞 (3日)
		{ 407, 1, 3, 8 },   // 玩命骷髏 SKULL-1 (3日)
		{ 427, 1, 3, 6 },   // 闇獄閻羅 SKULL-3 (3日)
		// 勳章、重置券與解碼器
		{ 1012, 1, 0, 10 }, // 黃金經驗值勳章 (數量 1)
		{ 378, 1, 0, 10 },  // 特殊任務重置券 (數量 1)
		{ 173, 2, 0, 8 }    // 電子解碼器 (數量 2)
	};

	_weeklyRewardPool = {
		// 點數/主力武器 (7日)
		{ 140, 1, 7, 8 },   // 幽冥暴君 SKULL-9 (7日)
		{ 141, 1, 7, 8 },   // 噬血狂怒 SKULL-7 (7日)
		{ 367, 1, 7, 8 },   // 貝西摩斯之爪 SKULL-5 (7日)
		{ 398, 1, 7, 8 },   // 喋血亂舞 (7日)
		{ 427, 1, 7, 8 },   // 闇獄閻羅 SKULL-3 (7日)
		{ 437, 1, 7, 8 },   // 索命羅剎 SKULL-11 (7日)
		{ 482, 1, 7, 8 },   // 噬魂者 BALROG-V (7日)
		{ 495, 1, 7, 6 },   // 隼雷 (7日)
		{ 383, 1, 7, 8 },   // 末日審判-SL8 EX (7日)
		// 耐久材料 / 設計圖 / 零件
		{ 5105, 1, 0, 8 },  // 90日耐久材料 (數量 1)
		{ 5106, 1, 0, 5 },  // 長期耐久材料 (數量 1)
		{ 1033, 1, 0, 10 }, // 一般設計圖交換卷 (數量 1)
		{ 5204, 2, 0, 8 },  // 精密強化零件 (數量 2)
		{ 5218, 1, 0, 4 },  // 完美傷害強化零件 (數量 1)
		// 解碼器
		{ 173, 3, 0, 8 },   // 電子解碼器 (數量 3)
		{ 173, 5, 0, 4 },   // 電子解碼器 (數量 5)
		{ 396, 2, 0, 4 }    // A世代解碼器 (數量 2)
	};
}

RolledQuestReward QuestManager::GetOrRollQuestReward(unsigned long userID, unsigned int questID) {
	lock_guard<mutex> lock(_mutex);
	auto key = make_pair(userID, questID);
	auto it = _userRolledRewards.find(key);
	if (it != _userRolledRewards.end() && it->second.itemID != 0) {
		return it->second;
	}

	const QuestConfig* q = nullptr;
	auto qit = _quests.find(questID);
	if (qit != _quests.end()) {
		q = &qit->second;
	}

	RolledQuestReward res;
	res.itemID = 173; // fallback 電子解碼器 (杜絕 itemID == 0)
	res.count = 1;
	res.duration = 0;
	res.isRevealed = false;

	if (q) {
		const std::vector<QuestRewardPoolEntry>* pool = nullptr;
		if (q->type == 0) {
			pool = &_dailyRewardPool;
		}
		else if (q->type == 1) {
			pool = &_specialRewardPool;
		}
		else if (q->type == 3) {
			pool = &_weeklyRewardPool;
		}

		if (pool && !pool->empty()) {
			int totalWeight = 0;
			for (const auto& e : *pool) totalWeight += e.weight;
			if (totalWeight > 0) {
				std::uniform_int_distribution<int> distWeight(0, totalWeight - 1);
				int r = distWeight(GetQuestRNG());
				for (const auto& e : *pool) {
					if (r < e.weight) {
						res.itemID = e.itemID;
						res.count = e.count;
						res.duration = e.duration;
						break;
					}
					r -= e.weight;
				}
			}
		}
	}

	// 確保 itemID 絕對不可為 0
	if (res.itemID == 0) {
		res.itemID = 173;
		res.count = 1;
		res.duration = 0;
	}

	// 保留先前的透視狀態標記（若已存在節點）
	if (it != _userRolledRewards.end()) {
		res.isRevealed = it->second.isRevealed;
	}

	_userRolledRewards[key] = res;
	return res;
}

bool QuestManager::IsQuestRewardRevealed(unsigned long userID, unsigned int questID) {
	lock_guard<mutex> lock(_mutex);
	auto it = _userRolledRewards.find(make_pair(userID, questID));
	if (it != _userRolledRewards.end()) {
		return it->second.isRevealed;
	}
	return false;
}

void QuestManager::SetQuestRewardRevealed(unsigned long userID, unsigned int questID, bool revealed) {
	lock_guard<mutex> lock(_mutex);
	auto key = make_pair(userID, questID);
	auto it = _userRolledRewards.find(key);
	if (it != _userRolledRewards.end()) {
		it->second.isRevealed = revealed;
	}
	else {
		RolledQuestReward res;
		res.itemID = 173;
		res.count = 1;
		res.duration = 0;
		res.isRevealed = revealed;
		_userRolledRewards[key] = res;
	}
}

void QuestManager::ClearRolledQuestReward(unsigned long userID, unsigned int questID) {
	lock_guard<mutex> lock(_mutex);
	_userRolledRewards.erase(make_pair(userID, questID));
}

void QuestManager::ClearUserRolledRewards(unsigned long userID) {
	lock_guard<mutex> lock(_mutex);
	for (auto it = _userRolledRewards.begin(); it != _userRolledRewards.end(); ) {
		if (it->first.first == userID) {
			it = _userRolledRewards.erase(it);
		}
		else {
			++it;
		}
	}
}

void QuestManager::GrantQuestRewardItem(User* user, unsigned short itemID, unsigned short count, unsigned short duration) {
	if (user == NULL || itemID == 0) return;
	unsigned long userID = user->GetUserID();
	auto currentInv = databaseManager.GetUserInventory(userID);

	bool alreadyOwned = false;
	InventoryItem ownedItem;
	unsigned short maxSlotID = 0;

	for (const auto& item : currentInv) {
		if (item.slotID > maxSlotID) maxSlotID = item.slotID;
		if (item.itemID == itemID) {
			alreadyOwned = true;
			ownedItem = item;
		}
	}

	time_t now = time(nullptr);

	if (duration == 0) { // 數量型道具
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(userID, itemID, count);
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = (count > 0) ? count : 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}
	else { // 期限型道具 (天數)
		if (alreadyOwned) {
			if (ownedItem.expirationTime == 0) {
				// Already permanent, keep permanent
			}
			else {
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)duration * 86400;
				databaseManager.UpdateUserInventoryExpiration(userID, itemID, newExp);
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = (unsigned long)now + (unsigned long)duration * 86400;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}
}

std::vector<QuestRewardItem> QuestManager::GetQuestPreviewRewards(unsigned int questID) {
	std::vector<QuestRewardItem> rewards;
	const QuestConfig* q = GetQuest(questID);
	if (!q) return rewards;

	if (q->type == 1) { // Special Quest
		QuestRewardItem rPts;
		rPts.type = 1; // Points
		rPts.amount = 800;
		rewards.push_back(rPts);
	}
	else if (q->type == 0) { // Daily Quest
		QuestRewardItem rPts;
		rPts.type = 1;
		rPts.amount = 200;
		rewards.push_back(rPts);
	}
	else if (q->type == 3) { // Weekly Quest
		QuestRewardItem rPts;
		rPts.type = 1;
		rPts.amount = 1500;
		rewards.push_back(rPts);
	}
	return rewards;
}

std::vector<QuestRewardItem> QuestManager::GetQuestRewardPreview(unsigned int questID, unsigned char rewardIndex) {
	std::vector<QuestRewardItem> rewards;
	const QuestConfig* q = GetQuest(questID);
	if (!q) return rewards;

	if (q->type == 2) { // Honor Quest (榮譽任務)
		unsigned short titleID = (unsigned short)(questID - 2000);
		const TitleConfig* titleCfg = titleManager.GetTitle(titleID);
		unsigned int honorPts = titleCfg ? titleCfg->honorPoints : 10;
		QuestRewardItem r;
		r.type = 4; // Honor points (客戶端協議 type 4 對應內部 type 6: 榮譽點數圖標與數量)
		r.amount = honorPts;
		rewards.push_back(r);
		return rewards;
	}

	if (rewardIndex == 1) { // Point 獎勵 (每日 200, 特殊 800, 每週 1500)
		unsigned int pts = (q->type == 3) ? 1500 : (q->type == 1 ? 800 : 200);
		QuestRewardItem rPts;
		rPts.type = 1; // Points
		rPts.amount = pts;
		rewards.push_back(rPts);
	}

	return rewards;
}

bool QuestManager::UpdateQuestProgress(User* user, unsigned int questID, unsigned char condIndex, unsigned short addCount) {
	if (user == NULL || questID == 0 || addCount == 0) return false;

	const QuestConfig* q = GetQuest(questID);
	if (!q || condIndex >= q->conditions.size()) return false;

	UserQuestProgress prog;
	GetUserQuestProgress(user, questID, prog);

	// If already complete or claimed, do not modify (only 0 is InProgress)
	if (prog.state != 0) return false;

	unsigned short target = q->conditions[condIndex].target;
	if (prog.counts[condIndex] >= target) return false;

	prog.counts[condIndex] = (unsigned short)min<unsigned int>(prog.counts[condIndex] + addCount, target);

	// Check if all conditions are now met
	bool allDone = true;
	for (size_t c = 0; c < q->conditions.size(); ++c) {
		if (prog.counts[c] < q->conditions[c].target) {
			allDone = false;
			break;
		}
	}

	if (allDone) {
		prog.state = 1; // Complete / Ready to claim!
	}

	// Save to DB
	UserQuestRecord rec;
	rec.questID = questID;
	rec.state = prog.state;
	rec.isTracked = prog.isTracked;
	rec.count0 = prog.counts[0];
	rec.count1 = prog.counts[1];
	rec.count2 = prog.counts[2];
	rec.count3 = prog.counts[3];
	rec.count4 = prog.counts[4];
	rec.count5 = prog.counts[5];
	databaseManager.SaveUserQuest(user->GetUserID(), rec);

	// Push condition update to client
	packet_QuestManager.SendPacket_Quest_UpdateCondition(
		user->GetConnection(), questID, condIndex, target, prog.counts[condIndex]);

	// If quest just completed, push state 1 (Complete) to client
	if (prog.state == 1) {
		if (q->type == 0) {
			databaseManager.RecordDailyQuestCompleted(user->GetUserID());
		}
		else if (q->type == 1) {
			databaseManager.RecordSpecialQuestCompleted(user->GetUserID());
		}
		packet_QuestManager.SendPacket_Quest_UpdateQuest(user->GetConnection(), questID, 1);
		SyncUserQuestStats(user);
		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) completed Quest [{}] '{}'! Ready to claim!\n",
			user->GetUserLogName(), questID, q->name));
	}

	return true;
}

bool QuestManager::CompleteQuest(User* user, unsigned int questID) {
	if (user == NULL || questID == 0) return false;

	const QuestConfig* q = GetQuest(questID);
	if (!q) return false;

	UserQuestProgress prog;
	GetUserQuestProgress(user, questID, prog);

	UserQuestRecord rec;
	rec.questID = questID;
	rec.state = 1; // Complete / Ready to claim!
	rec.isTracked = prog.isTracked;
	for (size_t c = 0; c < q->conditions.size() && c < 6; ++c) {
		unsigned short target = q->conditions[c].target;
		if (c == 0) rec.count0 = target;
		else if (c == 1) rec.count1 = target;
		else if (c == 2) rec.count2 = target;
		else if (c == 3) rec.count3 = target;
		else if (c == 4) rec.count4 = target;
		else if (c == 5) rec.count5 = target;
		packet_QuestManager.SendPacket_Quest_UpdateCondition(user->GetConnection(), questID, (unsigned char)c, target, target);
	}
	databaseManager.SaveUserQuest(user->GetUserID(), rec);

	// 重置該任務在內存的隨機獎勵抽選與透視狀態，便於反覆測試
	ClearRolledQuestReward(user->GetUserID(), questID);

	// 記錄完成統計 (每日/特殊)
	if (q->type == 0) {
		databaseManager.RecordDailyQuestCompleted(user->GetUserID());
	}
	else if (q->type == 1) {
		databaseManager.RecordSpecialQuestCompleted(user->GetUserID());
	}

	// 重新同步完整任務列表，讓客戶端重置所有按鈕與選項狀態（避免領取後選項消失），包含最新統計數據
	packet_QuestManager.SendPacket_Quest_InitList(user);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] GM/Cheat: User ({}) forced completed Quest [{}] '{}'!\n",
		user->GetUserLogName(), questID, q->name));
	return true;
}

bool QuestManager::ResetSingleQuest(User* user, unsigned int questID) {
	if (user == NULL || questID == 0) return false;

	const QuestConfig* q = GetQuest(questID);
	if (!q) return false;

	databaseManager.ResetUserQuestRecord(user->GetUserID(), questID);
	ClearRolledQuestReward(user->GetUserID(), questID);
	packet_QuestManager.SendPacket_Quest_InitList(user);
	SyncUserQuestStats(user);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) reset Quest [{}] '{}' to InProgress!\n",
		user->GetUserLogName(), questID, q->name));
	return true;
}

bool QuestManager::ResetDailyQuests(User* user, unsigned int specificQID) {
	if (user == NULL) return false;
	if (specificQID > 0) {
		return ResetSingleQuest(user, specificQID);
	}

	databaseManager.ResetUserDailyQuests(user->GetUserID());
	ClearUserRolledRewards(user->GetUserID());
	packet_QuestManager.SendPacket_Quest_InitList(user);
	SyncUserQuestStats(user);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) all daily quests reset to InProgress!\n",
		user->GetUserLogName()));
	return true;
}

bool QuestManager::ResetWeeklyQuests(User* user, unsigned int specificQID) {
	if (user == NULL) return false;
	if (specificQID > 0) {
		return ResetSingleQuest(user, specificQID);
	}

	databaseManager.ResetUserWeeklyQuests(user->GetUserID());
	ClearUserRolledRewards(user->GetUserID());
	packet_QuestManager.SendPacket_Quest_InitList(user);
	SyncUserQuestStats(user);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) all weekly quests reset to InProgress!\n",
		user->GetUserLogName()));
	return true;
}

bool QuestManager::ForceResetSpecialQuest(User* user, unsigned int targetSpecialID) {
	if (user == NULL) return false;

	unsigned long userID = user->GetUserID();

	if (targetSpecialID > 0) {
		const QuestConfig* q = GetQuest(targetSpecialID);
		if (!q || q->type != 1) {
			serverConsole.Print(PrefixType::Warn, format("[ QuestManager ] ForceResetSpecialQuest invalid target ID {}!\n", targetSpecialID));
			return false;
		}

		databaseManager.DeleteUserSpecialQuest(userID);
		ClearUserRolledRewards(userID);
		{
			lock_guard<mutex> lock(_mutex);
			_userSpecialQuestAssignedMaps.erase(make_pair(userID, targetSpecialID));
		}

		UserQuestRecord rec;
		rec.questID = targetSpecialID;
		rec.state = 0; // InProgress
		rec.isTracked = 0;
		databaseManager.SaveUserQuest(userID, rec);

		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) forced special quest to [{}] '{}' (InProgress)!\n",
			user->GetUserLogName(), targetSpecialID, q->name));
	}
	else {
		unsigned int currentSpecialID = GetUserDailySpecialQuestID(user);
		databaseManager.ResetUserQuestRecord(userID, currentSpecialID);
		ClearRolledQuestReward(userID, currentSpecialID);
		{
			lock_guard<mutex> lock(_mutex);
			_userSpecialQuestAssignedMaps.erase(make_pair(userID, currentSpecialID));
		}

		UserQuestRecord rec;
		rec.questID = currentSpecialID;
		rec.state = 0; // InProgress
		rec.isTracked = 0;
		databaseManager.SaveUserQuest(userID, rec);

		const QuestConfig* q = GetQuest(currentSpecialID);
		string qName = q ? q->name : "特殊任務";
		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) current special quest [{}] '{}' reset to InProgress!\n",
			user->GetUserLogName(), currentSpecialID, qName));
	}

	packet_QuestManager.SendPacket_Quest_InitList(user);
	SyncUserQuestStats(user);
	return true;
}

bool QuestManager::ResetAllQuests(User* user) {
	if (user == NULL) return false;

	unsigned long userID = user->GetUserID();
	databaseManager.ResetUserDailyQuests(userID);
	databaseManager.ResetUserWeeklyQuests(userID);
	unsigned int currentSpecialID = GetUserDailySpecialQuestID(user);
	databaseManager.ResetUserQuestRecord(userID, currentSpecialID);
	ClearUserRolledRewards(userID);
	{
		lock_guard<mutex> lock(_mutex);
		for (auto it = _userSpecialQuestAssignedMaps.begin(); it != _userSpecialQuestAssignedMaps.end(); ) {
			if (it->first.first == userID) it = _userSpecialQuestAssignedMaps.erase(it);
			else ++it;
		}
	}

	packet_QuestManager.SendPacket_Quest_InitList(user);
	SyncUserQuestStats(user);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) all daily, weekly, and special quests reset to InProgress!\n",
		user->GetUserLogName()));
	return true;
}

bool QuestManager::ResetUserQuests(User* user) {
	return ResetDailyQuests(user, 0);
}

bool QuestManager::SetQuestTracked(User* user, unsigned int questID, unsigned char tracked) {
	if (user == NULL || questID == 0) return false;

	databaseManager.SetUserQuestTracked(user->GetUserID(), questID, tracked);

	UserQuestProgress prog;
	GetUserQuestProgress(user, questID, prog);
	prog.isTracked = tracked;

	// Send Subtype 11 with state & isTracked updated
	packet_QuestManager.SendPacket_Quest_UpdateQuest(user->GetConnection(), questID, prog.state, tracked, true);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) SetQuestTracked questID: {}, tracked: {}\n",
		user->GetUserLogName(), questID, (int)tracked));
	return true;
}

bool QuestManager::ClaimReward(User* user, unsigned int questID, unsigned char rewardIndex) {
	if (user == NULL || questID == 0) return false;

	const QuestConfig* q = GetQuest(questID);
	if (!q) {
		serverConsole.Print(PrefixType::Warn, format("[ QuestManager ] User ({}) tried to claim non-existent Quest {}\n",
			user->GetUserLogName(), questID));
		return false;
	}

	UserQuestProgress prog;
	GetUserQuestProgress(user, questID, prog);

	// Must be state 1 (Complete / Ready to claim)
	if (prog.state != 1) {
		serverConsole.Print(PrefixType::Warn, format("[ QuestManager ] User ({}) tried to claim Quest [{}] '{}' with invalid state {}\n",
			user->GetUserLogName(), questID, q->name, (int)prog.state));
		return false;
	}

	// 1. Mark as Claimed (state = 2) in DB
	prog.state = 2;
	UserQuestRecord rec;
	rec.questID = questID;
	rec.state = 2;
	rec.isTracked = prog.isTracked;
	rec.count0 = prog.counts[0];
	rec.count1 = prog.counts[1];
	rec.count2 = prog.counts[2];
	rec.count3 = prog.counts[3];
	rec.count4 = prog.counts[4];
	rec.count5 = prog.counts[5];
	databaseManager.SaveUserQuest(user->GetUserID(), rec);

	// 2. Notify client that quest is claimed (Subtype 11, state = 2)
	packet_QuestManager.SendPacket_Quest_UpdateQuest(user->GetConnection(), questID, 2);

	// 3. Grant rewards & push ClaimRewardReply (Subtype 41)
	std::vector<QuestRewardItem> rewardsToSend;

	if (q->type == 2) {
		// Honor Quest: Unlock medal & grant honor points
		unsigned short titleID = (unsigned short)(questID - 2000);
		const TitleConfig* titleCfg = titleManager.GetTitle(titleID);
		unsigned int honorPts = titleCfg ? titleCfg->honorPoints : 10;

		// 1) 寫入榮譽點數並解鎖稱號 (傳入 syncToClient = false，不彈出 Facebook Medal 勛章視窗，改為標準獎勵視窗)
		if (databaseManager.HasUserTitle(user->GetUserID(), titleID)) {
			databaseManager.AddUserHonorPoints(user->GetUserID(), (unsigned short)honorPts);
		}
		else {
			titleManager.UnlockTitle(user, titleID, false);
		}
		titleManager.SyncTitleUpdate(user);

		QuestRewardItem r;
		r.type = 4; // Honor points (客戶端協議 type 4 對應內部 type 6: 榮譽點數圖標與數量)
		r.amount = honorPts;
		rewardsToSend.push_back(r);

		// 2) 彈出官方獲獎視窗 (ItemAcceptDlg，帶 Honor 項目，觸發 honor.tga 與「榮譽點數%d點」)
		packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
			user->GetConnection(), {}, 0, 0, honorPts, "QUEST_REWARD_TITLE", "QUEST_REWARD_MSG");

		// 3) 即時同步更新後的角色數據 (含 honorPoints 與 achievements 稱號遮罩)
		UserCharacterResult ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });

		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) claimed Honor Quest [{}] '{}' -> Title {} (+{} Honor Points)\n",
			user->GetUserLogName(), questID, q->name, titleID, honorPts));
	}
	else if (q->type == 0 || q->type == 3 || q->type == 1) {
		const char* qTypeName = (q->type == 3) ? "Weekly" : ((q->type == 1) ? "Special" : "Daily");

		// 統計累計更新
		if (q->type == 0) {
			databaseManager.RecordDailyQuestCompleted(user->GetUserID());
		}
		else if (q->type == 1) {
			databaseManager.RecordSpecialQuestCompleted(user->GetUserID());
		}

		if (rewardIndex == 1) {
			// 玩家選擇 Reward 1: Point 獎勵 (純 Point，不發放 EXP，不發放隨機道具)
			unsigned long long addedPoints = (q->type == 3) ? 1500 : (q->type == 1 ? 800 : 200);
			databaseManager.AddUserFunds(user->GetUserID(), true, addedPoints);

			QuestRewardItem rPts;
			rPts.type = 1; // Points
			rPts.amount = (unsigned int)addedPoints;
			rewardsToSend.push_back(rPts);

			// 彈出官方原生獲獎視窗 (帶 Point 項目，觸發 point.tga 與 "%d點Point")
			packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
				user->GetConnection(), {}, (unsigned int)addedPoints, 0, 0, "QUEST_REWARD_TITLE", "QUEST_REWARD_MSG");

			serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) claimed {} Quest [{}] '{}' Choice: Points (+{} Points)\n",
				user->GetUserLogName(), qTypeName, questID, q->name, addedPoints));
		}
		else {
			// 玩家選擇 Reward 0: 隨機道具獎勵 (純 Item，不發放 Point，不發放 EXP)
			RolledQuestReward rolled = GetOrRollQuestReward(user->GetUserID(), questID);

			// Grant rolled item into inventory
			GrantQuestRewardItem(user, rolled.itemID, rolled.count, rolled.duration);

			// Sync inventory to client
			packetManager.SendPacket_Inventory(user->GetConnection(), user->GetUserInventory());

			QuestRewardItem rItem;
			rItem.type = 0; // Item
			rItem.itemID = rolled.itemID;
			rItem.count = (unsigned char)rolled.count;
			rItem.period = (unsigned char)rolled.duration;
			rewardsToSend.push_back(rItem);

			// 彈出官方獲獎視窗 (帶 Item)
			AwardItem award;
			award.itemID = rolled.itemID;
			award.count = rolled.count;
			award.duration = rolled.duration;
			award.flags = 0;
			packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
				user->GetConnection(), { award }, 0, 0, 0, "QUEST_REWARD_TITLE", "QUEST_REWARD_MSG");

			serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) claimed {} Quest [{}] '{}' Choice: Item (Item {} [x{}, {}d])\n",
				user->GetUserLogName(), qTypeName, questID, q->name, rolled.itemID, rolled.count, rolled.duration));
		}

		// Sync updated character stats to client
		UserCharacterResult ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });

		// 領獎完畢，清除該任務已快取的隨機抽選結果
		ClearRolledQuestReward(user->GetUserID(), questID);
	}

	// Send Subtype 41 to update client quest detail state
	packet_QuestManager.SendPacket_Quest_ClaimRewardReply(user->GetConnection(), questID, 1, rewardsToSend);

	// 4. 即時下發最新任務面板統計數據 (Subtype 14)，包含榮譽點數、特殊任務連續天數、今日每日完成數等
	SyncUserQuestStats(user);

	return true;
}

bool QuestManager::IsBombDefusalMap(unsigned char mapID) {
	string name = GetMapFileName(mapID);
	return name.starts_with("de_");
}

bool QuestManager::IsHostageRescueMap(unsigned char mapID) {
	string name = GetMapFileName(mapID);
	return name.starts_with("cs_");
}

bool QuestManager::IsZombieScenarioMap(unsigned char mapID) {
	string name = GetMapFileName(mapID);
	return name.starts_with("zs_");
}

unsigned char QuestManager::GetSpecialQuestRequiredMode(unsigned int specialQID) {
	switch (specialQID) {
		case 1001: return 0;  // [一般模式] {0} 戰鬥
		case 1002: return 1;  // [死鬥模式] {0} 拯救同伴
		case 1003: return 2;  // [團隊死鬥] {0} 拯救同伴
		case 1004: return 3;  // [一般模式<虛擬對手>] {0} 戰鬥
		case 1005: return 4;  // [死鬥模式<虛擬對手>] {0} 拯救同伴
		case 1006: return 5;  // [團隊死鬥<虛擬對手>] {0} 拯救同伴
		case 1007: return 8;  // [殭屍經典] {0} 獵殺殭屍
		case 1008: return 9;  // [殭屍異變] {0} 獵殺殭屍
		case 1009: return 14; // [殭屍英雄] {0} 獵殺殭屍
		case 1010: return 11; // [大亂鬥] {0} 渾沌時代
		case 1011: return 13; // [大亂鬥殲滅] {0} 渾沌時代
		case 1012: return 15; // [災厄之章] {0}的拯救行動
		case 1013: return 17; // [戰神之章] {0}的反擊
		case 1014: return 18; // [機甲爭霸] {0} 占領
		case 1015: return 22; // [槍王之王活動] {0} 火力支援!
		case 1016: return 19; // [悠遊模式] {0} 支援
		case 1017: return 24; // 殭屍經典(虛擬對手){0}殭屍樣本採集!!
		case 1018: return 21; // [即刻救援] {0} 作戰
		case 1019: return 26; // [夜襲生存戰] {0} 生存
		default: return 0xFF;
	}
}

string QuestManager::GetUserSpecialQuestAssignedMap(User* user, unsigned int specialQID) {
	if (user == NULL || specialQID == 0) return "";
	lock_guard<mutex> lock(_mutex);
	auto key = make_pair(user->GetUserID(), specialQID);
	auto it = _userSpecialQuestAssignedMaps.find(key);
	if (it != _userSpecialQuestAssignedMaps.end() && !it->second.empty()) {
		return it->second;
	}

	const QuestConfig* q = nullptr;
	auto qIt = _quests.find(specialQID);
	if (qIt != _quests.end()) {
		q = &qIt->second;
	}

	string picked = "";
	if (q && !q->candidateMaps.empty()) {
		std::uniform_int_distribution<size_t> dist(0, q->candidateMaps.size() - 1);
		picked = q->candidateMaps[dist(GetQuestRNG())];
	}
	else if (q && !q->allowedMaps.empty()) {
		std::uniform_int_distribution<size_t> dist(0, q->allowedMaps.size() - 1);
		picked = q->allowedMaps[dist(GetQuestRNG())];
	}
	else {
		// Fallback based on quest ID
		if (specialQID == 1012) picked = "zs_lostcity";
		else if (specialQID == 1013 || specialQID == 1018) picked = "hs_desertstorm";
		else if (specialQID == 1014) picked = "bzm_bigtree";
		else if (specialQID == 1019) picked = "zsh_deadcity";
		else picked = "de_dust2";
	}

	_userSpecialQuestAssignedMaps[key] = picked;
	return picked;
}

string QuestManager::GetUserSpecialQuestMapToken(User* user, unsigned int specialQID) {
	string assignedMap = GetUserSpecialQuestAssignedMap(user, specialQID);
	if (assignedMap.empty()) return "";

	string token = GetMapTokenByName(assignedMap);
	if (!token.empty()) return token;

	if (assignedMap.starts_with("#")) return assignedMap;
	return assignedMap;
}

void QuestManager::OnPlayerDied(User* victim) {
	if (victim == NULL) return;
	lock_guard<mutex> lock(_mutex);
	auto it = _playerLifeStats.find(victim->GetUserID());
	if (it != _playerLifeStats.end()) {
		it->second.consecutiveHeadshots = 0;
		it->second.hadPistolKill = false;
		it->second.hadKnifeKill = false;
		it->second.killStreak = 0;
	}
}

bool QuestManager::CheckQuestRule(const QuestConfig& qCfg, const QuestConditionConfig& cond, const GameEventContext& ctx, User* user) {
	// 0. 戰局統計開關檢查 (Quest Battle Tracking Enabled Check)
	if (!IsQuestProgressEnabled(qCfg)) {
		return false;
	}

	// 1. 事件名稱比對 (Event Matching)
	string evt = cond.event;
	if (evt.empty()) {
		evt = "kill";
	}
	if (evt != "any" && evt != ctx.eventName) {
		if (evt == "c4_any" && (ctx.eventName == "c4_plant" || ctx.eventName == "c4_defuse")) {
			// Matched
		}
		else {
			return false;
		}
	}

	// 2. 模式限制 (Allowed Modes / Disallowed Modes)
	vector<int> allowedModes = cond.allowedModes;
	if (allowedModes.empty()) allowedModes = qCfg.allowedModes;

	if (allowedModes.empty() && qCfg.id >= 1001 && qCfg.id <= 1019) {
		unsigned char spMode = GetSpecialQuestRequiredMode(qCfg.id);
		if (spMode != 0xFF) allowedModes.push_back(spMode);
	}

	if (!allowedModes.empty()) {
		bool modeFound = false;
		for (int m : allowedModes) {
			if (m == (int)ctx.gameModeID) { modeFound = true; break; }
		}
		if (!modeFound) return false;
	}

	vector<int> disallowedModes = cond.disallowedModes;
	disallowedModes.insert(disallowedModes.end(), qCfg.disallowedModes.begin(), qCfg.disallowedModes.end());
	for (int m : disallowedModes) {
		if (m == (int)ctx.gameModeID) return false;
	}

	// 3. 地圖限制 (Allowed Maps / Disallowed Maps)
	string mapLower = ctx.mapFileName;
	transform(mapLower.begin(), mapLower.end(), mapLower.begin(), [](unsigned char c) { return (char)tolower(c); });
	string mapIDStr = to_string((int)ctx.mapID);

	auto MatchMapEntry = [&](const string& ruleMap) -> bool {
		string r = ruleMap;
		transform(r.begin(), r.end(), r.begin(), [](unsigned char c) { return (char)tolower(c); });
		if (r == mapLower) return true;
		if (r == mapIDStr) return true;
		if (mapLower.starts_with(r)) return true;
		if (mapLower.find(r) != string::npos) return true;
		return false;
	};

	// 針對特殊任務（1001 ~ 1019），必須在當日為該玩家抽選鎖定的特定地圖完成
	if (user != NULL && qCfg.id >= 1001 && qCfg.id <= 1019) {
		string assigned = GetUserSpecialQuestAssignedMap(user, qCfg.id);
		if (!assigned.empty()) {
			if (!MatchMapEntry(assigned)) return false;
		}
	}

	vector<string> allowedMaps = cond.allowedMaps;
	if (allowedMaps.empty()) allowedMaps = qCfg.allowedMaps;
	if (!allowedMaps.empty()) {
		bool mapFound = false;
		for (const auto& m : allowedMaps) {
			if (MatchMapEntry(m)) { mapFound = true; break; }
		}
		if (!mapFound) return false;
	}

	vector<string> disallowedMaps = cond.disallowedMaps;
	disallowedMaps.insert(disallowedMaps.end(), qCfg.disallowedMaps.begin(), qCfg.disallowedMaps.end());
	for (const auto& m : disallowedMaps) {
		if (MatchMapEntry(m)) return false;
	}

	// 4. 最少人數門檻 (Min Players)
	int minPlayers = cond.minPlayers >= 0 ? cond.minPlayers : (qCfg.minPlayers >= 0 ? qCfg.minPlayers : serverConfig.questMinPlayers);
	if (minPlayers > 0 && cond.targetType != 2 && !ctx.isBotMode) {
		if (ctx.humanPlayers < (size_t)minPlayers) return false;
	}

	// 5. 受害者類型 (Target Type: 1=真人玩家, 2=BOT, 3=殭屍, 4=人類)
	if (cond.targetType == 1 && !ctx.isHumanVictim) return false;
	if (cond.targetType == 2 && !ctx.isBotVictim) return false;
	if (cond.targetType == 3 && !ctx.isZombieVictim) return false;
	if (cond.targetType == 4 && ctx.isZombieVictim) return false;

	// 6. 爆頭要求 (Headshot: 1=必須爆頭, 2=必須非爆頭)
	if (cond.headshot == 1 && !ctx.isHeadshot) return false;
	if (cond.headshot == 2 && ctx.isHeadshot) return false;

	// 7. 武器要求 (Allowed Weapons / Weapon Category)
	if (!cond.allowedWeapons.empty()) {
		bool weaponFound = false;
		for (unsigned char w : cond.allowedWeapons) {
			if (w == ctx.weaponID) { weaponFound = true; break; }
		}
		if (!weaponFound) return false;
	}

	if (!cond.weaponCategory.empty()) {
		string wc = cond.weaponCategory;
		transform(wc.begin(), wc.end(), wc.begin(), [](unsigned char c) { return (char)tolower(c); });
		if (wc == "knife") {
			if (ctx.weaponID != 29 && ctx.weaponID != 1) return false;
		}
		else if (wc == "grenade") {
			if (ctx.weaponID != 4) return false;
		}
		else if (wc == "pistol") {
			bool isPistol = (ctx.weaponID == 1 || ctx.weaponID == 2 || ctx.weaponID == 10 ||
			                 ctx.weaponID == 11 || ctx.weaponID == 16 || ctx.weaponID == 17 || ctx.weaponID == 26);
			if (!isPistol) return false;
		}
	}

	// 8. Boss 匹配
	if (!cond.allowedBosses.empty()) {
		bool bossMatched = false;
		string ctxBoss = ctx.bossName;
		transform(ctxBoss.begin(), ctxBoss.end(), ctxBoss.begin(), [](unsigned char c) { return (char)tolower(c); });
		for (string b : cond.allowedBosses) {
			transform(b.begin(), b.end(), b.begin(), [](unsigned char c) { return (char)tolower(c); });
			if (b == ctxBoss || ctxBoss.find(b) != string::npos) { bossMatched = true; break; }
		}
		if (!bossMatched) return false;
	}

	// 9. 特殊狀態機機制
	if (user != NULL) {
		PlayerLifeStats& lifeStats = _playerLifeStats[user->GetUserID()];

		if (cond.consecutive > 0) {
			if (ctx.eventName == "kill") {
				if (cond.headshot == 1) {
					if (ctx.isHeadshot) {
						lifeStats.consecutiveHeadshots++;
						if (lifeStats.consecutiveHeadshots < (unsigned short)cond.consecutive) return false;
					}
					else {
						lifeStats.consecutiveHeadshots = 0;
						return false;
					}
				}
			}
		}

		if (cond.comboInLife) {
			bool isPistol = (ctx.weaponID == 1 || ctx.weaponID == 2 || ctx.weaponID == 10 ||
			                 ctx.weaponID == 11 || ctx.weaponID == 16 || ctx.weaponID == 17 || ctx.weaponID == 26);
			bool isKnife = (ctx.weaponID == 29 || ctx.weaponID == 1);
			if (isPistol) lifeStats.hadPistolKill = true;
			if (isKnife) lifeStats.hadKnifeKill = true;
			if (!lifeStats.hadPistolKill || !lifeStats.hadKnifeKill) return false;
		}

		if (cond.streakInLife > 0) {
			lifeStats.killStreak++;
			if (lifeStats.killStreak < (unsigned short)cond.streakInLife) return false;
		}
	}

	return true;
}

void QuestManager::DispatchQuestEvent(User* user, const GameEventContext& ctx) {
	if (user == NULL) return;

	map<unsigned int, UserQuestProgress> progresses;
	GetUserAllQuestProgress(user, progresses);

	unsigned int activeSpecialID = GetUserDailySpecialQuestID(user);

	for (const auto& [qid, prog] : progresses) {
		if (prog.state != 0) continue;

		if (qid >= 1001 && qid <= 1019 && qid != activeSpecialID) continue;

		const QuestConfig* qCfg = GetQuest(qid);
		if (!qCfg) continue;

		for (const auto& cond : qCfg->conditions) {
			if (CheckQuestRule(*qCfg, cond, ctx, user)) {
				UpdateQuestProgress(user, qid, cond.index, 1);
			}
		}
	}
}

void QuestManager::OnPlayerKilled(User* attacker, User* victim, unsigned char weaponID, bool isHeadshot, unsigned short distance) {
	if (attacker == NULL) return;

	Room* room = roomManager.GetRoomByRoomID(attacker->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "kill";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();
	ctx.isBotMode = (settings.gameModeID == GameMode::Bot || settings.gameModeID == GameMode::BotDM || settings.gameModeID == GameMode::BotTDM || settings.gameModeID == 24);

	ctx.isHumanVictim = (victim != NULL && victim != attacker);
	ctx.isBotVictim = (victim == NULL);
	ctx.isZombieVictim = (settings.gameModeID == 8 || settings.gameModeID == 9 || settings.gameModeID == 14);
	ctx.isHeadshot = isHeadshot;
	ctx.weaponID = weaponID;
	ctx.distance = distance;

	DispatchQuestEvent(attacker, ctx);
}

void QuestManager::OnRoundWin(User* user) {
	if (user == NULL) return;

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "round_win";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();
	ctx.isBotMode = (settings.gameModeID == GameMode::Bot || settings.gameModeID == GameMode::BotDM || settings.gameModeID == GameMode::BotTDM || settings.gameModeID == 24);
	ctx.isWin = true;

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnMatchEnd(User* user, bool isWin, unsigned short kills, unsigned short deaths, unsigned int durationSec) {
	if (user == NULL) return;

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();

	{
		lock_guard<mutex> lock(_mutex);
		_playerLifeStats.erase(user->GetUserID());
	}

	GameEventContext ctx;
	ctx.eventName = isWin ? "match_win" : "match_end";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();
	ctx.isBotMode = (settings.gameModeID == GameMode::Bot || settings.gameModeID == GameMode::BotDM || settings.gameModeID == GameMode::BotTDM || settings.gameModeID == 24);
	ctx.isWin = isWin;
	ctx.kills = kills;
	ctx.deaths = deaths;
	ctx.durationSec = durationSec;

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnBombEvent(User* user, bool isDefuse) {
	if (user == NULL) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = isDefuse ? "c4_defuse" : "c4_plant";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();
	ctx.isBotMode = (settings.gameModeID == GameMode::Bot || settings.gameModeID == GameMode::BotDM || settings.gameModeID == GameMode::BotTDM || settings.gameModeID == 24);

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnHostageRescued(User* user, unsigned short count) {
	if (user == NULL || count == 0) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "hostage_rescue";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();
	ctx.isBotMode = (settings.gameModeID == GameMode::Bot || settings.gameModeID == GameMode::BotDM || settings.gameModeID == GameMode::BotTDM || settings.gameModeID == 24);

	for (unsigned short i = 0; i < count; ++i) {
		DispatchQuestEvent(user, ctx);
	}
}

void QuestManager::OnMoraleBoost(User* user, unsigned char level) {
	if (user == NULL) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "morale_boost";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnSupplyBoxPicked(User* user) {
	if (user == NULL) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "supply_box";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnWallUpgraded(User* user) {
	if (user == NULL) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "wall_upgrade";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnBossKilled(User* user, const string& bossName) {
	if (user == NULL) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "boss_kill";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();
	ctx.bossName = bossName;

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnSpecialSkillKill(User* user, unsigned int skillID) {
	if (user == NULL) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "skill_kill";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();

	DispatchQuestEvent(user, ctx);
}

void QuestManager::OnPointCaptured(User* user) {
	if (user == NULL) return;
	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) return;

	const RoomSettings& settings = room->GetRoomSettings();
	GameEventContext ctx;
	ctx.eventName = "point_capture";
	ctx.gameModeID = settings.gameModeID;
	ctx.mapID = settings.mapID;
	ctx.mapFileName = GetMapFileName(settings.mapID);
	ctx.totalPlayers = room->GetRoomUsers().size();
	ctx.humanPlayers = room->GetRoomUsers().size();

	DispatchQuestEvent(user, ctx);
}
