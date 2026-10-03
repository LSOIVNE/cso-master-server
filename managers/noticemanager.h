#pragma once
#include "user.h"
#include <string>
#include <vector>

using namespace std;

struct NoticeConfig {
	bool welcomeEnabled = true;
	string welcomeType = "PublicNotice"; // "PublicNotice", "ServerMessage", "ServerYell", "RollingBanner"
	string welcomeMessage = "歡迎來到 CSO 2013 懷舊伺服器！祝您遊戲愉快！";

	bool periodicEnabled = true;
	unsigned int periodicIntervalSeconds = 300;
	string periodicType = "PublicNotice"; // "PublicNotice", "RollingBanner", "ServerYell", "ServerMessage"
	vector<string> periodicMessages = {
		"本伺服器為 2013 懷舊測試服，祝您遊戲愉快！",
		"輸入 /help 可查看聊天指令列表；輸入 /resetinv 可重置官方預設裝備。",
		"歡迎使用 /additem <ID> 測試各種經典武器與裝備！"
	};
};

class NoticeManager {
public:
	NoticeManager();

	bool Init();
	bool LoadConfig();
	void Reload();

	void OnSecondTick();
	void SendWelcomeNotice(User* user);

	const NoticeConfig& GetConfig() const noexcept { return _config; }

private:
	void saveDefaultConfig(const string& path);

	NoticeConfig _config;
	unsigned int _secondCounter;
	size_t _currentMessageIndex;
};

extern NoticeManager noticeManager;
