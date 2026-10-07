#include "packet_questmanager.h"
#include "questmanager.h"
#include "usermanager.h"
#include "databasemanager.h"
#include "serverconsole.h"
#include <algorithm>
#include <format>

using namespace std;

Packet_QuestManager packet_QuestManager;

void Packet_QuestManager::ParsePacket_Quest(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) return;
	auto& connection = packet->GetConnection();
	if (connection == NULL) return;

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_QuestManager ] Client ({}) sent Packet_Quest, but not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	unsigned char subtype = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ Packet_QuestManager ] User ({}) sent Packet_Quest subtype {}\n",
		user->GetUserLogName(), (int)subtype));

	switch (subtype) {
		case 31: { // ClientPreviewSpecialQuest
			unsigned int specialQID = questManager.GetUserDailySpecialQuestID(user);
			auto previewRewards = questManager.GetQuestPreviewRewards(specialQID);
			SendPacket_Quest_ClaimRewardReply(connection, specialQID, 1, previewRewards);
			serverConsole.Print(PrefixType::Info, format("[ Packet_QuestManager ] Sent PreviewRewards (Subtype 41) for Quest [{}] to User ({})\n",
				specialQID, user->GetUserLogName()));
			break;
		}
		case 32: { // ClientResetSpecialQuest
			if (questManager.ResetSpecialQuest(user)) {
				SendPacket_Quest_InitList(user);
			}
			break;
		}
		case 41: { // ClientRequestRewardPreview (點擊獎勵視窗時觸發)
			unsigned int questID = packet->ReadUInt32_LE();
			unsigned char rewardIndex = packet->ReadUInt8();
			auto previewRewards = questManager.GetQuestRewardPreview(questID, rewardIndex);
			SendPacket_Quest_ClaimRewardReply(connection, questID, 1, previewRewards);
			serverConsole.Print(PrefixType::Info, format("[ Packet_QuestManager ] Sent RewardPreview (Subtype 41) for Quest [{}] (index {}) to User ({})\n",
				questID, (int)rewardIndex, user->GetUserLogName()));
			break;
		}
		case 42: { // ClientClaimReward
			parsePacket_Quest_ClaimReward(user, packet);
			break;
		}
		case 51: { // ClientTrackQuest
			parsePacket_Quest_TrackQuest(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_QuestManager ] User ({}) sent unhandled Quest subtype {}\n",
				user->GetUserLogName(), (int)subtype));
			break;
		}
	}
}

void Packet_QuestManager::parsePacket_Quest_ClaimReward(User* user, TCPConnection::Packet::pointer packet) {
	unsigned int questID = packet->ReadUInt32_LE();
	unsigned char param = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_QuestManager ] User ({}) ClaimReward questID: {}, param: {}\n",
		user->GetUserLogName(), questID, (int)param));

	questManager.ClaimReward(user, questID);
}

void Packet_QuestManager::parsePacket_Quest_TrackQuest(User* user, TCPConnection::Packet::pointer packet) {
	unsigned int questID = packet->ReadUInt32_LE();
	unsigned char tracked = packet->ReadUInt8();

	serverConsole.Print(PrefixType::Info, format("[ Packet_QuestManager ] User ({}) TrackQuest questID: {}, tracked: {}\n",
		user->GetUserLogName(), questID, (int)tracked));

	questManager.SetQuestTracked(user, questID, tracked);
}

void Packet_QuestManager::SendPacket_Quest_InitList(User* user) {
	if (user == NULL) return;
	auto& connection = user->GetConnection();
	if (connection == NULL) return;

	map<unsigned int, UserQuestProgress> userQuests;
	questManager.GetUserAllQuestProgress(user, userQuests);

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Quest });
	if (packet == NULL) return;

	packet->WriteUInt8(1); // Subtype 1: InitQuestList

	// Collect quests to send (Daily 0, Weekly 3, Honor 2)
	vector<const QuestConfig*> questsToSend;
	for (const auto& [qid, qcfg] : questManager.GetAllQuests()) {
		if (qcfg.type == 0 || qcfg.type == 3 || qcfg.type == 2) {
			questsToSend.push_back(&qcfg);
		}
	}

	// Include user's daily Special Quest (Type 1)
	unsigned int specialQID = questManager.GetUserDailySpecialQuestID(user);
	const QuestConfig* specialQuest = questManager.GetQuest(specialQID);
	if (specialQuest) {
		questsToSend.push_back(specialQuest);
	}

	unsigned char questCount = (unsigned char)std::min<size_t>(questsToSend.size(), 255);
	packet->WriteUInt8(questCount);

	for (size_t i = 0; i < questCount; ++i) {
		const auto* q = questsToSend[i];
		packet->WriteUInt32_LE(q->id);

		// questFlag: 1 (valid) | 2 (questType) | 0x20 (state) | 0x40 (isTracked) = 0x0063
		packet->WriteUInt16_LE(0x0063);
		packet->WriteUInt8(q->type);

		// Default state: 0 (InProgress)
		unsigned char state = 0;
		unsigned char isTracked = (q->id <= 3) ? 1 : 0; // Default track first 3 daily quests
		unsigned short counts[6] = { 0 };

		auto it = userQuests.find(q->id);
		if (it != userQuests.end()) {
			state = it->second.state;
			isTracked = it->second.isTracked;
			for (int c = 0; c < 6; ++c) {
				counts[c] = it->second.counts[c];
			}
		} else if (q->type == 2) {
			// Check if already unlocked title
			unsigned short titleID = (unsigned short)(q->id - 2000);
			if (databaseManager.HasUserTitle(user->GetUserID(), titleID)) {
				state = 2; // Claimed / 已獲得
				for (size_t c = 0; c < q->conditions.size() && c < 6; ++c) {
					counts[c] = q->conditions[c].target;
				}
			}
		}

		packet->WriteUInt8(state);
		packet->WriteUInt8(isTracked);

		// Conditions
		unsigned char condCount = (unsigned char)q->conditions.size();
		packet->WriteUInt8(condCount);
		for (size_t c = 0; c < condCount; ++c) {
			const auto& cond = q->conditions[c];
			packet->WriteUInt8(cond.index);
			// condFlag: 1 (valid) | 4 (target) | 8 (current) = 0x0D
			packet->WriteUInt8(0x0D);
			packet->WriteUInt16_LE(cond.target);
			packet->WriteUInt16_LE(counts[c]);
		}

		// Rewards
		unsigned char rewardCount = (unsigned char)q->rewards.size();
		packet->WriteUInt8(rewardCount);
		for (unsigned char r = 0; r < rewardCount; ++r) {
			const auto& rw = q->rewards[r];
			packet->WriteUInt8(r);
			packet->WriteUInt8(0x03); // valid (1) | id (2)
			packet->WriteUInt32_LE(rw.image); // Must be resource_id (0~12) for client to lookup in Quest_RewardRes.csv!
		}
	}

	// Tail flag: 0x0001 (Honor points) | 0x0080 (Special quest remaining resets) = 0x0081
	unsigned int honorPoints = databaseManager.GetUserHonorPoints(user->GetUserID());
	unsigned char resetCount = questManager.GetUserSpecialQuestResetCount(user);
	packet->WriteUInt16_LE(0x0081);
	packet->WriteUInt32_LE(honorPoints);
	packet->WriteUInt8(resetCount);

	packet->Send();

	serverConsole.Print(PrefixType::Info, format("[ Packet_QuestManager ] Sent InitQuestList to User ({}) ({} quests)\n",
		user->GetUserLogName(), questCount));
}

void Packet_QuestManager::SendPacket_Quest_UpdateQuest(TCPConnection::pointer connection, unsigned int questID, unsigned char state, unsigned char isTracked, bool updateTracked) {
	if (connection == NULL) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Quest });
	if (packet == NULL) return;

	packet->WriteUInt8(11); // Subtype 11: UpdateQuest
	packet->WriteUInt32_LE(questID);

	if (updateTracked) {
		packet->WriteUInt16_LE(0x0061); // questFlag: 1 | 0x20 | 0x40
		packet->WriteUInt8(state);
		packet->WriteUInt8(isTracked);
	}
	else {
		packet->WriteUInt16_LE(0x0021); // questFlag: 1 | 0x20
		packet->WriteUInt8(state);
	}

	packet->Send();
}

void Packet_QuestManager::SendPacket_Quest_UpdateCondition(TCPConnection::pointer connection, unsigned int questID, unsigned char condIndex, unsigned short target, unsigned short current) {
	if (connection == NULL) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Quest });
	if (packet == NULL) return;

	packet->WriteUInt8(12); // Subtype 12: UpdateCondition
	packet->WriteUInt32_LE(questID);
	packet->WriteUInt8(condIndex);
	packet->WriteUInt8(0x0D); // condFlag: 1 | 4 | 8
	packet->WriteUInt16_LE(target);
	packet->WriteUInt16_LE(current);

	packet->Send();
}

void Packet_QuestManager::SendPacket_Quest_ClaimRewardReply(TCPConnection::pointer connection, unsigned int questID, unsigned char status, const std::vector<QuestRewardItem>& rewards) {
	if (connection == NULL) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Quest });
	if (packet == NULL) return;

	packet->WriteUInt8(41); // Subtype 41 (0x29): ClaimRewardReply
	packet->WriteUInt32_LE(questID);
	packet->WriteUInt8(status); // 1 = Success
	packet->WriteUInt8((unsigned char)rewards.size());

	for (const auto& r : rewards) {
		packet->WriteUInt8(r.type);
		switch (r.type) {
			case 0: // Item: UInt16 itemID, UInt8 count, UInt8 period
				packet->WriteUInt16_LE(r.itemID);
				packet->WriteUInt8(r.count);
				packet->WriteUInt8(r.period);
				break;
			case 1: // Points: UInt32 points
				packet->WriteUInt32_LE(r.amount);
				break;
			case 2: // EXP: UInt32 exp
				packet->WriteUInt32_LE(r.amount);
				break;
			case 3: // Title: UInt16 titleID
				packet->WriteUInt16_LE((unsigned short)(r.itemID > 0 ? r.itemID : r.amount));
				break;
			case 4: // Honor points: UInt32 honorPoints
				packet->WriteUInt32_LE(r.amount);
				break;
			default:
				break;
		}
	}

	packet->Send();
}

void Packet_QuestManager::SendPacket_Quest_UpdateStats(TCPConnection::pointer connection, unsigned int honorPoints, unsigned char resetCount) {
	if (connection == NULL) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Quest });
	if (packet == NULL) return;

	packet->WriteUInt8(14); // Subtype 14 (0x0E): UpdateStats & Trigger UI Refresh
	packet->WriteUInt16_LE(0x0081); // statsFlag: 0x0001 (Honor) | 0x0080 (ResetCount)
	packet->WriteUInt32_LE(honorPoints);
	packet->WriteUInt8(resetCount);

	packet->Send();
}
