#include "questmanager.h"
#include "packet_questmanager.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "titlemanager.h"
#include "databasemanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <format>
#include <filesystem>
#include <ctime>

using namespace std;
namespace fs = std::filesystem;

QuestManager questManager;

bool QuestManager::Init() {
	return loadConfigs();
}

bool QuestManager::Reload() {
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

		for (const auto& item : j) {
			QuestConfig cfg;
			cfg.id = item.value("id", 0u);
			cfg.type = (unsigned char)item.value("type", 0);
			cfg.npc = (unsigned char)item.value("npc", 0);
			cfg.name = item.value("name", "");
			cfg.token = item.value("token", "");
			cfg.desc = item.value("desc", "");

			if (item.contains("conditions")) {
				for (const auto& c : item["conditions"]) {
					QuestConditionConfig cond;
					cond.index = (unsigned char)c.value("index", 0);
					cond.token = c.value("token", "");
					cond.desc = c.value("desc", "");
					cond.target = (unsigned short)c.value("target", 1);
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

		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] Successfully loaded {} quests from {}\n",
			_quests.size(), validPath));
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

	// Default progress: InProgress (0), 0 counts
	outProgress.questID = questID;
	outProgress.state = 0; // InProgress (進行)
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

	// Initialize record in DB so it stays assigned for today
	UserQuestRecord rec;
	rec.questID = assignedID;
	rec.state = 0;
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

	return (info.usedCount >= 3) ? 0 : (unsigned char)(3 - info.usedCount);
}

bool QuestManager::ResetSpecialQuest(User* user) {
	if (user == NULL) return false;

	unsigned char remaining = GetUserSpecialQuestResetCount(user);
	if (remaining == 0) {
		serverConsole.Print(PrefixType::Warn, format("[ QuestManager ] User ({}) tried to reset special quest, but reset count exhausted!\n",
			user->GetUserLogName()));
		return false;
	}

	unsigned int currentSpecialID = GetUserDailySpecialQuestID(user);

	// Pick a new special quest (1001 ~ 1019) distinct from current
	unsigned int newSpecialID = currentSpecialID;
	for (int attempts = 0; attempts < 10; ++attempts) {
		unsigned int candidate = 1001 + (unsigned int)(rand() % 19);
		if (candidate != currentSpecialID) {
			newSpecialID = candidate;
			break;
		}
	}
	if (newSpecialID == currentSpecialID) {
		newSpecialID = (currentSpecialID >= 1019) ? 1001 : (currentSpecialID + 1);
	}

	// Delete old special quest record from DB
	databaseManager.DeleteUserSpecialQuest(user->GetUserID());

	// Save new special quest record
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

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) reset special quest from {} to {}, remaining resets: {}\n",
		user->GetUserLogName(), currentSpecialID, newSpecialID, (int)(remaining - 1)));
	return true;
}

std::vector<QuestRewardItem> QuestManager::GetQuestPreviewRewards(unsigned int questID) {
	std::vector<QuestRewardItem> rewards;
	const QuestConfig* q = GetQuest(questID);
	if (!q) return rewards;

	if (q->type == 1) { // Special Quest
		QuestRewardItem rItem;
		rItem.type = 0; // Item
		rItem.itemID = 173; // 電子解碼器
		rItem.count = 2;
		rItem.period = 0;
		rewards.push_back(rItem);

		QuestRewardItem rPts;
		rPts.type = 1; // Points
		rPts.amount = 3000;
		rewards.push_back(rPts);

		QuestRewardItem rExp;
		rExp.type = 2; // EXP
		rExp.amount = 1000;
		rewards.push_back(rExp);
	}
	else if (q->type == 0) { // Daily Quest
		QuestRewardItem rItem;
		rItem.type = 0;
		rItem.itemID = 173;
		rItem.count = 1;
		rItem.period = 0;
		rewards.push_back(rItem);

		QuestRewardItem rPts;
		rPts.type = 1;
		rPts.amount = 2000;
		rewards.push_back(rPts);

		QuestRewardItem rExp;
		rExp.type = 2;
		rExp.amount = 500;
		rewards.push_back(rExp);
	}
	else if (q->type == 3) { // Weekly Quest
		QuestRewardItem rItem;
		rItem.type = 0;
		rItem.itemID = 173;
		rItem.count = 3;
		rItem.period = 0;
		rewards.push_back(rItem);

		QuestRewardItem rPts;
		rPts.type = 1;
		rPts.amount = 5000;
		rewards.push_back(rPts);

		QuestRewardItem rExp;
		rExp.type = 2;
		rExp.amount = 1500;
		rewards.push_back(rExp);
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
		unsigned short honorPts = titleCfg ? titleCfg->honorPoints : 10;
		QuestRewardItem r;
		r.type = 3; // Honor points
		r.amount = honorPts;
		rewards.push_back(r);
		return rewards;
	}

	if (rewardIndex == 1) { // Point 獎勵 (例如每日任務Point獎勵, 特殊任務Point獎勵, 每週任務Point獎勵)
		unsigned int pts = (q->type == 3) ? 5000 : (q->type == 1 ? 3000 : 2000);
		QuestRewardItem rPts;
		rPts.type = 1; // Points
		rPts.amount = pts;
		rewards.push_back(rPts);
	}
	else { // 隨機道具獎勵 + EXP
		unsigned short decoderCount = (q->type == 3) ? 3 : (q->type == 1 ? 2 : 1);
		unsigned int exp = (q->type == 3) ? 1500 : (q->type == 1 ? 1000 : 500);

		QuestRewardItem rItem;
		rItem.type = 0; // Item
		rItem.itemID = 173; // 電子解碼器
		rItem.count = (unsigned char)decoderCount;
		rItem.period = 0;
		rewards.push_back(rItem);

		QuestRewardItem rExp;
		rExp.type = 2; // EXP
		rExp.amount = exp;
		rewards.push_back(rExp);
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
		packet_QuestManager.SendPacket_Quest_UpdateQuest(user->GetConnection(), questID, 1);
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
	packet_QuestManager.SendPacket_Quest_UpdateQuest(user->GetConnection(), questID, 1);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] GM/Cheat: User ({}) forced completed Quest [{}] '{}'!\n",
		user->GetUserLogName(), questID, q->name));
	return true;
}

bool QuestManager::ResetUserQuests(User* user) {
	if (user == NULL) return false;

	databaseManager.ResetDailyQuests(user->GetUserID());
	packet_QuestManager.SendPacket_Quest_InitList(user);

	serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) daily quests reset!\n",
		user->GetUserLogName()));
	return true;
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

bool QuestManager::ClaimReward(User* user, unsigned int questID) {
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
		titleManager.UnlockTitle(user, titleID, true);

		const TitleConfig* titleCfg = titleManager.GetTitle(titleID);
		unsigned short honorPts = titleCfg ? titleCfg->honorPoints : 10;

		QuestRewardItem r;
		r.type = 3; // Honor points
		r.amount = honorPts;
		rewardsToSend.push_back(r);

		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) claimed Honor Quest [{}] '{}' -> Unlocked Title {}\n",
			user->GetUserLogName(), questID, q->name, titleID));
	}
	else if (q->type == 0 || q->type == 3 || q->type == 1) {
		unsigned long long addedExp = (q->type == 3) ? 1500 : (q->type == 1 ? 1000 : 500);
		unsigned long long addedPoints = (q->type == 3) ? 5000 : (q->type == 1 ? 3000 : 2000);
		unsigned short decoderCount = (q->type == 3) ? 3 : (q->type == 1 ? 2 : 1);

		databaseManager.AddUserFunds(user->GetUserID(), true, addedPoints);
		databaseManager.SaveUserBattleResult(user->GetUserID(), addedExp, 0, 0, 0, false, 0, false);

		// Grant Decoders (173: 電子解碼器)
		auto currentInv = user->GetUserInventory();
		bool alreadyOwned = false;
		unsigned short maxSlotID = 0;
		for (const auto& inv : currentInv) {
			if (inv.slotID > maxSlotID) maxSlotID = inv.slotID;
			if (inv.itemID == 173) {
				alreadyOwned = true;
			}
		}
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(user->GetUserID(), 173, decoderCount);
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = 173;
			newItem.count = decoderCount;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)time(nullptr);
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(user->GetUserID(), { newItem });
		}

		// Sync inventory to client
		packetManager.SendPacket_Inventory(user->GetConnection(), user->GetUserInventory());

		// Sync updated character stats to client
		UserCharacterResult ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });

		QuestRewardItem rExp;
		rExp.type = 2; // EXP
		rExp.amount = (unsigned int)addedExp;
		rewardsToSend.push_back(rExp);

		QuestRewardItem rPts;
		rPts.type = 1; // Points
		rPts.amount = (unsigned int)addedPoints;
		rewardsToSend.push_back(rPts);

		QuestRewardItem rItem;
		rItem.type = 0; // Item
		rItem.itemID = 5202;
		rItem.count = (unsigned char)decoderCount;
		rItem.period = 0;
		rewardsToSend.push_back(rItem);

		const char* qTypeName = (q->type == 3) ? "Weekly" : ((q->type == 1) ? "Special" : "Daily");
		serverConsole.Print(PrefixType::Info, format("[ QuestManager ] User ({}) claimed {} Quest [{}] '{}' (+{} EXP, +{} Points, +{} Decoders)\n",
			user->GetUserLogName(), qTypeName, questID, q->name, addedExp, addedPoints, decoderCount));
	}

	// Send Subtype 41 to trigger native congratulations dialog & sound
	packet_QuestManager.SendPacket_Quest_ClaimRewardReply(user->GetConnection(), questID, 1, rewardsToSend);

	return true;
}

void QuestManager::OnPlayerKilled(User* attacker, User* victim, unsigned char weaponID, bool isHeadshot, unsigned short distance) {
	if (attacker == NULL) return;

	// Daily kill quests
	UpdateQuestProgress(attacker, 5, 0, 1);  // 5 kills
	UpdateQuestProgress(attacker, 11, 0, 1); // 5 kills
	UpdateQuestProgress(attacker, 13, 0, 1); // 5 kills
	UpdateQuestProgress(attacker, 14, 0, 1); // 10 kills
	UpdateQuestProgress(attacker, 15, 0, 1); // 10 kills
	UpdateQuestProgress(attacker, 16, 0, 1); // 5 kills
	UpdateQuestProgress(attacker, 17, 0, 1); // 5 kills
	UpdateQuestProgress(attacker, 18, 0, 1); // 5 kills
	UpdateQuestProgress(attacker, 19, 0, 1); // 50 kills
	UpdateQuestProgress(attacker, 21, 0, 1); // 7 kills

	// Update active special quest progress
	unsigned int specialQID = GetUserDailySpecialQuestID(attacker);
	if (specialQID >= 1001 && specialQID <= 1019) {
		UpdateQuestProgress(attacker, specialQID, 0, 1);
	}

	if (isHeadshot) {
		UpdateQuestProgress(attacker, 2, 0, 1);    // Daily 2: 5 headshots
		UpdateQuestProgress(attacker, 4, 0, 1);    // Daily 4: 3 headshots
		UpdateQuestProgress(attacker, 2003, 0, 1); // Honor 2003: 1000 headshots (爆頭王)
	}

	// Knife kill (CSW_KNIFE = 29, or legacy weaponID == 1)
	if (weaponID == 29 || weaponID == 1) {
		UpdateQuestProgress(attacker, 8, 0, 1);    // Daily 8: Knife/pistol kill
		UpdateQuestProgress(attacker, 2006, 0, 1); // Honor 2006: 1000 knife kills (刀鋒戰士)
	}

	// Pistol kill (CSW: 1=P228, 2=GLOCK, 10=ELITE, 11=FIVESEVEN, 16=USP, 26=DEAGLE)
	if (weaponID == 1 || weaponID == 2 || weaponID == 10 || weaponID == 11 || weaponID == 16 || weaponID == 26) {
		UpdateQuestProgress(attacker, 8, 0, 1);    // Daily 8: Knife/pistol kill
	}

	// Honor Kill Milestones
	UpdateQuestProgress(attacker, 2007, 0, 1); // 5000 kills
	UpdateQuestProgress(attacker, 2008, 0, 1); // 5000 kills
	UpdateQuestProgress(attacker, 2010, 0, 1); // 10000 kills
	UpdateQuestProgress(attacker, 2024, 0, 1); // 10000 kills
	UpdateQuestProgress(attacker, 2031, 0, 1); // 1000 kills
	UpdateQuestProgress(attacker, 2032, 0, 1); // 1000 kills
	UpdateQuestProgress(attacker, 2061, 0, 1); // 3000 kills
	UpdateQuestProgress(attacker, 2063, 0, 1); // 3000 kills
	UpdateQuestProgress(attacker, 2064, 0, 1); // 10000 kills
	UpdateQuestProgress(attacker, 2067, 0, 1); // 3000 kills
	UpdateQuestProgress(attacker, 2074, 0, 1); // 5000 kills
}

void QuestManager::OnRoundWin(User* user) {
	if (user == NULL) return;

	UpdateQuestProgress(user, 1, 0, 1);  // Daily 1: 3 round wins
	UpdateQuestProgress(user, 10, 0, 1); // Daily 10: 3 round wins
}

void QuestManager::OnMatchEnd(User* user, bool isWin, unsigned short kills, unsigned short deaths, unsigned int durationSec) {
	if (user == NULL) return;

	// Matches played
	UpdateQuestProgress(user, 7, 0, 1);    // Daily 7: 1 match played
	UpdateQuestProgress(user, 12, 0, 1);   // Daily 12: 1 match played
	UpdateQuestProgress(user, 2001, 0, 1); // Honor 2001: 1 match played (新兵戰士)

	if (isWin) {
		UpdateQuestProgress(user, 3, 0, 1);    // Daily 3: 1 match win
		UpdateQuestProgress(user, 9, 0, 1);    // Daily 9: 1 match win
		UpdateQuestProgress(user, 501, 0, 1);  // Weekly 501: 7 match wins
		UpdateQuestProgress(user, 502, 0, 1);  // Weekly 502: 5 match wins
		UpdateQuestProgress(user, 503, 0, 1);  // Weekly 503: 5 match wins
		UpdateQuestProgress(user, 504, 0, 1);  // Weekly 504: 5 match wins
		UpdateQuestProgress(user, 2013, 0, 1); // Honor 2013: 100 wins
		UpdateQuestProgress(user, 2017, 0, 1); // Honor 2017: 50 wins
		UpdateQuestProgress(user, 2040, 0, 1); // Honor 2040: 100 wins
		UpdateQuestProgress(user, 2041, 0, 1); // Honor 2041: 100 wins
		UpdateQuestProgress(user, 2042, 0, 1); // Honor 2042: 100 wins
		UpdateQuestProgress(user, 2043, 0, 1); // Honor 2043: 100 wins
		UpdateQuestProgress(user, 2044, 0, 1); // Honor 2044: 100 wins
		UpdateQuestProgress(user, 2047, 0, 1); // Honor 2047: 100 wins
		UpdateQuestProgress(user, 2048, 0, 1); // Honor 2048: 100 wins
		UpdateQuestProgress(user, 2050, 0, 1); // Honor 2050: 100 wins
	}
}
