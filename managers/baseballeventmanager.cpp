#include "baseballeventmanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "serverconsole.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_umsgmanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;

BaseballEventManager baseballEventManager;

static std::string GetBaseballResetPeriodString(const std::string& testResetTime) {
	time_t now = time(nullptr);
	if (testResetTime == "now" || testResetTime == "always") {
		return format("now_{}", now);
	}

	int resetHour = 0;
	int resetMinute = 0;
	if (!testResetTime.empty()) {
		int h = 0, m = 0;
		if (sscanf_s(testResetTime.c_str(), "%d:%d", &h, &m) == 2) {
			resetHour = std::clamp(h, 0, 23);
			resetMinute = std::clamp(m, 0, 59);
		}
	}

	time_t adjustedTime = now - (resetHour * 3600 + resetMinute * 60);
	tm localTm{};
	localtime_s(&localTm, &adjustedTime);
	char buf[32];
	strftime(buf, sizeof(buf), "%Y-%m-%d", &localTm);

	if (!testResetTime.empty()) {
		return format("{}@{}:{:02d}", buf, resetHour, resetMinute);
	}
	return std::string(buf);
}

BaseballEventManager::BaseballEventManager() {
}

BaseballEventManager::~BaseballEventManager() {
	FlushAllDirty();
}

bool BaseballEventManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ BaseballEventManager ] Initializing Baseball Event system...\n");
	return LoadConfig(configPath);
}

bool BaseballEventManager::LoadConfig(const std::string& configPath) {
	std::string resolvedPath = configPath.empty() ? "configs/baseballevent.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Error, format("[ BaseballEventManager ] Config file not found: {}\n", resolvedPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ BaseballEventManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root;
		file >> root;

		if (root.contains("Settings")) {
			const auto& s = root["Settings"];
			_settings.active = s.value("Active", true);
			_settings.playtimeSecondsPerBall = s.value("PlaytimeSecondsPerBall", 1200);
			_settings.maxBalls = (unsigned char)s.value("MaxBalls", 99);
			_settings.minPlayers = s.value("MinPlayers", 4);
			_settings.testResetTime = s.value("TestResetTime", "");
			_settings.awardTitle = s.value("AwardTitle", "棒球一個入手!");
			_settings.awardContent = s.value("AwardContent", "感謝您參與CSO棒球經典賽活動,恭喜您獲得以下獎勵,祝您游戲愉快!");

			if (s.contains("HitWeights")) {
				const auto& hw = s["HitWeights"];
				_settings.hitWeights.out = hw.value("Out", 38);
				_settings.hitWeights.single = hw.value("Single", 32);
				_settings.hitWeights.double_hit = hw.value("Double", 18);
				_settings.hitWeights.triple = hw.value("Triple", 8);
				_settings.hitWeights.homeRun = hw.value("HomeRun", 4);
			}
		}

		_dailyRewards.clear();
		if (root.contains("DailyScoreRewards") && root["DailyScoreRewards"].is_array()) {
			for (const auto& it : root["DailyScoreRewards"]) {
				BaseballScoreRewardTier tier;
				tier.minScore = it.value("MinScore", 0);
				tier.maxScore = it.value("MaxScore", 0);
				tier.itemID = (unsigned short)it.value("ItemID", 0);
				tier.duration = it.value("Duration", 0);
				tier.name = it.value("Name", "");

				if (it.contains("CountByScore")) {
					tier.countByScore = it.value("CountByScore", false);
				}
				if (it.contains("CountPerScore")) {
					tier.countPerScore = (unsigned short)it.value("CountPerScore", 1);
					tier.countByScore = true;
				}
				if (it.contains("ScoreMultiplier")) {
					tier.countPerScore = (unsigned short)it.value("ScoreMultiplier", 1);
					tier.countByScore = true;
				}

				if (it.contains("Count")) {
					if (it["Count"].is_string()) {
						std::string cStr = it["Count"].get<std::string>();
						if (cStr == "score" || cStr == "Score") {
							tier.countByScore = true;
							tier.countPerScore = 1;
							tier.count = 1;
						}
					}
					else if (it["Count"].is_number()) {
						tier.count = (unsigned short)it.value("Count", 1);
					}
				}
				else {
					tier.count = 1;
				}

				_dailyRewards.push_back(tier);
			}
		}

		serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] Loaded config with {} reward tiers (minPlayers: {}, testReset: '{}').\n",
			_dailyRewards.size(), _settings.minPlayers, _settings.testResetTime));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ BaseballEventManager ] JSON parse error: {}\n", e.what()));
		return false;
	}
}

bool BaseballEventManager::GetOrInitUserData(User* user, UserBaseballData& outData) {
	if (!user) return false;
	unsigned long userID = user->GetUserID();

	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _userCache.find(userID);
		if (it != _userCache.end()) {
			outData = it->second;
			return true;
		}
	}

	bool res = databaseManager.GetUserBaseballData(userID, outData);
	if (res) {
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[userID] = outData;
	}
	return res;
}

bool BaseballEventManager::CheckDailyReset(User* user, UserBaseballData& data) {
	if (!user) return false;

	std::string curPeriod = GetBaseballResetPeriodString(_settings.testResetTime);
	if (data.lastResetDate == curPeriod) {
		return false;
	}

	// Date/period changed: issue yesterday's reward if todayScore > 0 and previous date existed
	if (!data.lastResetDate.empty() && data.todayScore > 0) {
		for (const auto& tier : _dailyRewards) {
			if (data.todayScore >= tier.minScore && data.todayScore <= tier.maxScore) {
				unsigned short finalCount = tier.count;
				if (tier.countByScore) {
					unsigned int mult = tier.countPerScore > 0 ? tier.countPerScore : (tier.count > 0 ? tier.count : 1);
					unsigned int calc = (unsigned int)data.todayScore * mult;
					if (calc == 0) calc = 1;
					if (calc > 65535) calc = 65535;
					finalCount = (unsigned short)calc;
				}

				std::string rewardDisplayName = tier.name;
				if (tier.countByScore) {
					size_t pos = rewardDisplayName.find("%d");
					if (pos != std::string::npos) {
						rewardDisplayName.replace(pos, 2, std::to_string(finalCount));
					}
					else if (rewardDisplayName.find("個") == std::string::npos && rewardDisplayName.find(")") == std::string::npos) {
						rewardDisplayName += " (" + std::to_string(finalCount) + "個)";
					}
				}

				InventoryItem item{};
				GrantPrize(user, tier.itemID, finalCount, tier.duration, &item);
				auto conn = user->GetConnection();
				if (item.slotID != 0 && conn) {
					packetManager.SendPacket_InventoryDelta(conn, { item }, {});
				}

				if (conn) {
					// 1. 發放獎勵使用 AwardItem award 彈窗 (ItemAwardNoticeRaw 支援自訂標題與內容)
					AwardItem award;
					award.itemID = tier.itemID;
					award.count = finalCount;
					award.duration = (unsigned short)tier.duration;
					award.flags = 0;

					packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
						conn,
						{ award },
						_settings.awardTitle,
						_settings.awardContent,
						{},
						Packet_UMsgType::ItemAwardNoticeRaw
					);

					// 2. 定向發送黃色字體伺服器訊息
					std::string msg = format("CSO棒球經典賽：昨日累積分數為 {} 分，已獲得結算獎勵：{}！", (int)data.todayScore, rewardDisplayName);
					packet_UMsgManager.SendPacket_UMsg_ServerMessageText(conn, msg);
				}

				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) granted settlement reward (score {} -> itemID {} [x{}])\n",
					user->GetUserLogName(), (int)data.todayScore, tier.itemID, finalCount));
				break;
			}
		}
	}

	data.todayScore = 0;
	data.outCount = 0;
	data.baseStatus = 0;
	data.playtimeSeconds = 0;
	data.lastResetDate = curPeriod;

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[data.userID] = data;
		_dirtyUsers[data.userID] = false;
	}
	databaseManager.SaveUserBaseballData(data);
	return true;
}

bool BaseballEventManager::ForceDailyReset(User* user) {
	if (!user) return false;

	UserBaseballData data;
	GetOrInitUserData(user, data);

	if (data.todayScore == 0) {
		data.outCount = 0;
		data.baseStatus = 0;
		data.playtimeSeconds = 0;
		data.lastResetDate = format("force_{}", time(nullptr));
		databaseManager.SaveUserBaseballData(data);
		if (user->GetConnection()) SendPacket_Status(user->GetConnection(), data);
		return false;
	}

	bool granted = false;
	for (const auto& tier : _dailyRewards) {
		if (data.todayScore >= tier.minScore && data.todayScore <= tier.maxScore) {
			unsigned short finalCount = tier.count;
			if (tier.countByScore) {
				unsigned int mult = tier.countPerScore > 0 ? tier.countPerScore : (tier.count > 0 ? tier.count : 1);
				unsigned int calc = (unsigned int)data.todayScore * mult;
				if (calc == 0) calc = 1;
				if (calc > 65535) calc = 65535;
				finalCount = (unsigned short)calc;
			}

			std::string rewardDisplayName = tier.name;
			if (tier.countByScore) {
				size_t pos = rewardDisplayName.find("%d");
				if (pos != std::string::npos) {
					rewardDisplayName.replace(pos, 2, std::to_string(finalCount));
				}
				else if (rewardDisplayName.find("個") == std::string::npos && rewardDisplayName.find(")") == std::string::npos) {
					rewardDisplayName += " (" + std::to_string(finalCount) + "個)";
				}
			}

			InventoryItem item{};
			GrantPrize(user, tier.itemID, finalCount, tier.duration, &item);
			auto conn = user->GetConnection();
			if (item.slotID != 0 && conn) {
				packetManager.SendPacket_InventoryDelta(conn, { item }, {});
			}

			if (conn) {
				AwardItem award;
				award.itemID = tier.itemID;
				award.count = finalCount;
				award.duration = (unsigned short)tier.duration;
				award.flags = 0;

				packet_UMsgManager.SendPacket_UMsg_ItemAwardNotice(
					conn,
					{ award },
					_settings.awardTitle,
					_settings.awardContent,
					{},
					Packet_UMsgType::ItemAwardNoticeRaw
				);

				std::string msg = format("CSO棒球經典賽：今日手動結算分數為 {} 分，已獲得結算獎勵：{}！", (int)data.todayScore, rewardDisplayName);
				packet_UMsgManager.SendPacket_UMsg_ServerMessageText(conn, msg);
			}

			granted = true;
			break;
		}
	}

	data.todayScore = 0;
	data.outCount = 0;
	data.baseStatus = 0;
	data.playtimeSeconds = 0;
	data.lastResetDate = format("force_{}", time(nullptr));

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[data.userID] = data;
		_dirtyUsers[data.userID] = false;
	}
	databaseManager.SaveUserBaseballData(data);

	if (user->GetConnection()) {
		SendPacket_Status(user->GetConnection(), data);
	}
	return granted;
}

void BaseballEventManager::ParsePacket(User* user, TCPConnection::Packet::pointer packet) {
	if (!user || !packet) return;

	auto connection = user->GetConnection();
	if (!connection) return;

	unsigned char subAction = packet->ReadUInt8();
	serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) sent Baseball action {}\n", user->GetUserLogName(), (int)subAction));

	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);

	switch (subAction) {
		case 40: // Client opens dialog and requests status
		case 42: {
			SendPacket_Status(connection, data);
			break;
		}
		case 41: { // Client clicks Swing (Batting)
			if (data.outCount >= 3) {
				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) tried to swing while 3 OUT!\n", user->GetUserLogName()));
				SendPacket_Status(connection, data);
				return;
			}

			if (data.ballCount == 0) {
				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) tried to swing with 0 balls!\n", user->GetUserLogName()));
				SendPacket_Status(connection, data);
				return;
			}

			// Deduct 1 ball
			data.ballCount--;

			// Roll hit result
			int wOut = _settings.hitWeights.out;
			int w1 = _settings.hitWeights.single;
			int w2 = _settings.hitWeights.double_hit;
			int w3 = _settings.hitWeights.triple;
			int wHR = _settings.hitWeights.homeRun;
			int totalWeight = wOut + w1 + w2 + w3 + wHR;
			if (totalWeight <= 0) totalWeight = 100;

			int roll = rand() % totalWeight;
			unsigned char hitResult = 0;
			if (roll < wOut) {
				hitResult = 0; // Out
			}
			else if (roll < wOut + w1) {
				hitResult = 1; // 1-Base Hit
			}
			else if (roll < wOut + w1 + w2) {
				hitResult = 2; // 2-Base Hit
			}
			else if (roll < wOut + w1 + w2 + w3) {
				hitResult = 3; // 3-Base Hit
			}
			else {
				hitResult = 4; // Home Run
			}

			// Runner positions: b1 = 1st, b2 = 2nd, b3 = 3rd
			bool b1 = (data.baseStatus % 10) != 0;
			bool b2 = ((data.baseStatus % 100) / 10) != 0;
			bool b3 = (data.baseStatus / 100) != 0;
			int runsScored = 0;

			if (hitResult == 0) { // Out
				data.outCount++;
				if (data.outCount >= 3) {
					data.outCount = 3;
					data.baseStatus = 0; // Clear bases on 3 OUT
					data.ballCount = 0;  // 清空當天剩餘的棒球
					data.playtimeSeconds = 0; // 當天無法再累積遊戲時間獲得棒球
				}
			}
			else if (hitResult == 1) { // 1-Base Hit
				if (b3) runsScored++;
				bool new_b3 = b2;
				bool new_b2 = b1;
				bool new_b1 = true;
				data.baseStatus = (new_b3 ? 100 : 0) + (new_b2 ? 10 : 0) + (new_b1 ? 1 : 0);
			}
			else if (hitResult == 2) { // 2-Base Hit
				if (b3) runsScored++;
				if (b2) runsScored++;
				bool new_b3 = b1;
				bool new_b2 = true;
				bool new_b1 = false;
				data.baseStatus = (new_b3 ? 100 : 0) + (new_b2 ? 10 : 0) + (new_b1 ? 1 : 0);
			}
			else if (hitResult == 3) { // 3-Base Hit
				if (b3) runsScored++;
				if (b2) runsScored++;
				if (b1) runsScored++;
				bool new_b3 = true;
				bool new_b2 = false;
				bool new_b1 = false;
				data.baseStatus = (new_b3 ? 100 : 0) + (new_b2 ? 10 : 0) + (new_b1 ? 1 : 0);
			}
			else if (hitResult == 4) { // Home Run
				runsScored = (b3 ? 1 : 0) + (b2 ? 1 : 0) + (b1 ? 1 : 0) + 1;
				data.baseStatus = 0;
			}

			data.todayScore = (unsigned char)std::min(255, (int)data.todayScore + runsScored);
			data.totalScore = (unsigned short)std::min(65535, (int)data.totalScore + runsScored);

			{
				std::lock_guard<std::mutex> lock(_mutex);
				_userCache[data.userID] = data;
				_dirtyUsers[data.userID] = false;
			}
			databaseManager.SaveUserBaseballData(data);

			serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) swing result: hitResult={}, runs={}, todayScore={}, outCount={}, baseStatus={}\n",
				user->GetUserLogName(), (int)hitResult, runsScored, (int)data.todayScore, (int)data.outCount, (int)data.baseStatus));

			SendPacket_BattingResult(connection, data, hitResult);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ BaseballEventManager ] User ({}) sent unknown action {}\n", user->GetUserLogName(), (int)subAction));
			break;
		}
	}
}

void BaseballEventManager::OnSecondTick() {
	if (!_settings.active) return;

	static unsigned int secondCounter = 0;
	secondCounter++;

	// Periodic flush of dirty cached users every 60 seconds
	if (secondCounter % 60 == 0) {
		FlushAllDirty();
	}

	// Periodic reset check for online users every 5 seconds
	if (secondCounter % 5 == 0) {
		for (User* u : userManager.GetUsers()) {
			if (u && u->GetConnection() && userManager.IsUserLoggedIn(u)) {
				// Avoid interrupting active gameplay with modal dialogs
				if (u->GetUserStatus() != UserStatus::InGame) {
					UserBaseballData data;
					GetOrInitUserData(u, data);
					CheckDailyReset(u, data);
				}
			}
		}
	}

	// In-game playtime accumulation and periodic reminder check
	for (Room* room : roomManager.GetRooms()) {
		if (!room) continue;
		if (room->GetRoomSettings().roomState != RoomState::Playing && room->GetGameMatch() == nullptr) {
			continue;
		}

		std::vector<User*> inGameUsers;
		for (User* u : room->GetRoomUsers()) {
			if (u && u->GetConnection() && u->GetUserStatus() == UserStatus::InGame) {
				inGameUsers.push_back(u);
			}
		}

		if (inGameUsers.size() < _settings.minPlayers) {
			continue;
		}

		for (User* u : inGameUsers) {
			ProcessInGameTick(u, 1);
		}
	}
}

void BaseballEventManager::ProcessInGameTick(User* user, unsigned int deltaSeconds) {
	if (!user || !_settings.active) return;
	unsigned long userID = user->GetUserID();

	UserBaseballData data;
	GetOrInitUserData(user, data);

	if (data.ballCount >= _settings.maxBalls || data.outCount >= 3) {
		return;
	}

	data.playtimeSeconds += deltaSeconds;
	unsigned char earnedBalls = 0;

	while (data.playtimeSeconds >= _settings.playtimeSecondsPerBall) {
		data.playtimeSeconds -= _settings.playtimeSecondsPerBall;
		if (data.ballCount < _settings.maxBalls) {
			data.ballCount++;
			earnedBalls++;
		}
		if (data.ballCount >= _settings.maxBalls) {
			break;
		}
	}

	bool shouldSaveImmediate = false;

	if (earnedBalls > 0) {
		shouldSaveImmediate = true;
		auto conn = user->GetConnection();
		if (conn) {
			SendPacket_Status(conn, data);
			std::string msg = format("CSO棒球經典賽：遊戲時間累積滿 20 分鐘，獲得 {} 顆棒球！目前持有棒球：{} 顆",
				(int)earnedBalls, (int)data.ballCount);
			packet_UMsgManager.SendPacket_UMsg_ServerMessageText(conn, msg);
		}
	}

	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[userID] = data;
		_dirtyUsers[userID] = true;
	}

	if (shouldSaveImmediate) {
		databaseManager.SaveUserBaseballData(data);
		std::lock_guard<std::mutex> lock(_mutex);
		_dirtyUsers[userID] = false;
	}
}

void BaseballEventManager::OnMatchEnd(User* user, unsigned int /*durationSeconds*/) {
	if (!user || !_settings.active) return;
	FlushUserData(user->GetUserID());

	auto conn = user->GetConnection();
	if (conn) {
		UserBaseballData data;
		GetOrInitUserData(user, data);
		CheckDailyReset(user, data);
		SendPacket_Status(conn, data);
	}
}

void BaseballEventManager::OnUserLogout(User* user) {
	if (!user) return;
	unsigned long userID = user->GetUserID();
	FlushUserData(userID);
	std::lock_guard<std::mutex> lock(_mutex);
	_userCache.erase(userID);
	_dirtyUsers.erase(userID);
}

void BaseballEventManager::OnUserLogin(User* user) {
	if (!user) return;
	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);
}

void BaseballEventManager::OnUserEnterLobby(User* user) {
	if (!user) return;
	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);
}

void BaseballEventManager::FlushUserData(unsigned long userID) {
	UserBaseballData data;
	bool dirty = false;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		auto it = _userCache.find(userID);
		if (it != _userCache.end()) {
			data = it->second;
			auto dirtyIt = _dirtyUsers.find(userID);
			if (dirtyIt != _dirtyUsers.end() && dirtyIt->second) {
				dirty = true;
				dirtyIt->second = false;
			}
		}
	}
	if (dirty) {
		databaseManager.SaveUserBaseballData(data);
	}
}

void BaseballEventManager::FlushAllDirty() {
	std::vector<UserBaseballData> toSave;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		for (auto& pair : _dirtyUsers) {
			if (pair.second) {
				auto it = _userCache.find(pair.first);
				if (it != _userCache.end()) {
					toSave.push_back(it->second);
				}
				pair.second = false;
			}
		}
	}
	for (const auto& data : toSave) {
		databaseManager.SaveUserBaseballData(data);
	}
}

void BaseballEventManager::SendPacket_Status(TCPConnection::pointer connection, const UserBaseballData& data) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(4);  // gameType: Baseball Event
	packet->WriteUInt8(40); // subAction: Status
	packet->WriteUInt8(data.todayScore);
	packet->WriteUInt8((unsigned char)(data.totalScore > 255 ? 255 : data.totalScore));
	packet->WriteUInt8(data.ballCount);
	packet->WriteUInt8(data.baseStatus);
	packet->WriteUInt8(data.outCount);

	packet->Send();
}

void BaseballEventManager::SendPacket_BattingResult(TCPConnection::pointer connection, const UserBaseballData& data, unsigned char hitResult) {
	if (!connection) return;

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Minigame });
	if (!packet) return;

	packet->WriteUInt8(4);  // gameType: Baseball Event
	packet->WriteUInt8(41); // subAction: Batting Result
	packet->WriteUInt8(data.todayScore);
	packet->WriteUInt8((unsigned char)(data.totalScore > 255 ? 255 : data.totalScore));
	packet->WriteUInt8(data.ballCount);
	packet->WriteUInt8(data.baseStatus);
	packet->WriteUInt8(data.outCount);
	packet->WriteUInt8(hitResult);

	packet->Send();
}

bool BaseballEventManager::AddUserBalls(User* user, int deltaBalls) {
	if (!user) return false;

	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);

	int newCount = (int)data.ballCount + deltaBalls;
	if (newCount < 0) newCount = 0;
	if (newCount > (int)_settings.maxBalls) newCount = (int)_settings.maxBalls;

	data.ballCount = (unsigned char)newCount;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[data.userID] = data;
		_dirtyUsers[data.userID] = false;
	}
	databaseManager.SaveUserBaseballData(data);

	if (user->GetConnection()) {
		SendPacket_Status(user->GetConnection(), data);
	}
	return true;
}

bool BaseballEventManager::ResetUserToday(User* user) {
	if (!user) return false;

	UserBaseballData data;
	GetOrInitUserData(user, data);

	data.todayScore = 0;
	data.outCount = 0;
	data.baseStatus = 0;
	data.playtimeSeconds = 0;
	data.lastResetDate = GetBaseballResetPeriodString(_settings.testResetTime);
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[data.userID] = data;
		_dirtyUsers[data.userID] = false;
	}
	databaseManager.SaveUserBaseballData(data);

	if (user->GetConnection()) {
		SendPacket_Status(user->GetConnection(), data);
	}
	return true;
}

bool BaseballEventManager::SetUserScore(User* user, unsigned char todayScore, unsigned short totalScore) {
	if (!user) return false;

	UserBaseballData data;
	GetOrInitUserData(user, data);
	CheckDailyReset(user, data);

	data.todayScore = todayScore;
	data.totalScore = totalScore;
	{
		std::lock_guard<std::mutex> lock(_mutex);
		_userCache[data.userID] = data;
		_dirtyUsers[data.userID] = false;
	}
	databaseManager.SaveUserBaseballData(data);

	if (user->GetConnection()) {
		SendPacket_Status(user->GetConnection(), data);
	}
	return true;
}

void BaseballEventManager::GrantPrize(User* user, unsigned short itemID, unsigned short count, unsigned int durationDays, InventoryItem* outItem) {
	if (!user) return;
	unsigned long userID = user->GetUserID();

	// Special handling for C-Box (itemID 166): Stored in user_characters.cboxCount, never in inventory
	if (itemID == 166) {
		unsigned short addCount = count > 0 ? count : 1;
		unsigned long newTotal = 0;
		user->AddCBoxCount(addCount, &newTotal);
		auto conn = user->GetConnection();
		if (conn) {
			packet_UpdateInfoManager.SendPacket_UserUpdateInfo_CBoxCount(conn, userID, (unsigned short)newTotal);
			auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
		}
		if (outItem != nullptr) {
			*outItem = InventoryItem{};
		}
		return;
	}

	auto inv = user->GetUserInventory();

	unsigned short maxSlotID = 0;
	bool alreadyOwned = false;
	InventoryItem ownedItem{};
	for (const auto& it : inv) {
		if (it.slotID > maxSlotID) {
			maxSlotID = it.slotID;
		}
		if (it.itemID == itemID) {
			alreadyOwned = true;
			ownedItem = it;
		}
	}

	const auto* itemDef = itemDefinitionManager.GetItem(itemID);
	bool isPeriodItem = (durationDays != 0) || (itemDef != nullptr && itemDef->isPeriod);
	time_t now = time(nullptr);

	if (!isPeriodItem && durationDays == 0) {
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(userID, itemID, count);
			if (outItem != nullptr) {
				ownedItem.count += count;
				*outItem = ownedItem;
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
			if (outItem != nullptr) {
				*outItem = newItem;
			}
		}
	}
	else {
		bool isPermItem = (durationDays == 0);
		if (alreadyOwned) {
			if (ownedItem.expirationTime == 0) {
				serverConsole.Print(PrefixType::Info, format("[ BaseballEventManager ] User ({}) already owns permanent itemID {}, skipping duplicate.\n",
					user->GetUserLogName(), itemID));
			}
			else if (isPermItem) {
				ownedItem.expirationTime = 0;
				databaseManager.UpdateUserInventoryExpiration(userID, itemID, 0);
				if (outItem != nullptr) {
					*outItem = ownedItem;
				}
			}
			else {
				unsigned long addSeconds = (unsigned long)durationDays * 86400;
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				ownedItem.expirationTime = baseTime + addSeconds;
				databaseManager.UpdateUserInventoryExpiration(userID, itemID, ownedItem.expirationTime);
				if (outItem != nullptr) {
					*outItem = ownedItem;
				}
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count > 0 ? count : 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = isPermItem ? 0 : (unsigned long)(now + (unsigned long)durationDays * 86400);
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
			if (outItem != nullptr) {
				*outItem = newItem;
			}
		}
	}
}
