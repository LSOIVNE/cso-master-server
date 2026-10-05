#pragma once
#include "user.h"
#include <string>
#include <vector>

struct AttendanceRewardItem {
	unsigned int itemID = 0;
	std::string name = "";
	unsigned short count = 1;
	unsigned short period = 0; // days (0 = permanent / count item)
	unsigned int expirationTime = 0;
	unsigned char isEvent = 0;
	unsigned int points = 0;
	unsigned int weight = 10;
};

struct AttendanceDailyReward {
	unsigned char day = 1; // 1 ~ 7
	AttendanceRewardItem item;
};

struct AttendanceConfig {
	bool enabled = true;
	bool autoPopupOnLogin = true;
	std::vector<AttendanceDailyReward> dailyRewards;
	std::vector<AttendanceRewardItem> candidatePool;
};

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

private:
	bool grantRewardItem(User* user, const AttendanceRewardItem& item);
	std::vector<AttendanceRewardItem> selectCandidateItems();

	AttendanceConfig _config;
};

extern AttendanceManager attendanceManager;
