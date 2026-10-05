#pragma once
#include "user.h"
#include <string>
#include <vector>

struct AttendanceRewardItem {
	unsigned int itemID = 0;
	unsigned short duration = 0; // 天數 (0 = 數量道具或永久)
	unsigned short count = 0;    // 數量 (0 = 純時效武器)
	unsigned int enhance = 0;    // 強化屬性 bits (0 = 無強化)
	unsigned char isEvent = 0;   // 1 = 活動標記
	unsigned int weight = 10;    // 候選權重
	std::string name = "";       // 備註名稱 (可選)
};

struct AttendanceDailyReward {
	unsigned char day = 1; // 1 ~ 7
	AttendanceRewardItem item;
};

struct AttendanceConfig {
	bool enabled = true;
	bool autoPopupOnLogin = true;
	bool serverMessageEnabled = true;
	std::vector<AttendanceDailyReward> dailyRewards;
	std::vector<AttendanceRewardItem> candidatePool;
};

#include <unordered_map>

class AttendanceManager {
public:
	bool Init(const std::string& configPath = "configs/attendance.json");
	bool LoadConfig(const std::string& configPath);

	// Called when a user enters the lobby / channel
	void OnUserEnterLobby(User* user);

	// Sends Packet 78 Subtype 8 Action 0 (attendance status + candidates + ladder)
	void SendAttendanceInfo(User* user, bool forceOpen = false);

	// Handles Packet 78 Subtype 8 Action 1 (claim reward / spin)
	void ClaimAttendanceReward(User* user);

	// Clears cached candidate items for a user (e.g. after claim or test reset)
	void ClearUserCandidates(unsigned long userID);

private:
	bool grantRewardItem(User* user, const AttendanceRewardItem& item);
	std::vector<AttendanceRewardItem> selectCandidateItems(User* user = nullptr);

	AttendanceConfig _config;
	std::unordered_map<unsigned long, std::vector<AttendanceRewardItem>> _userCandidates;
};

extern AttendanceManager attendanceManager;
