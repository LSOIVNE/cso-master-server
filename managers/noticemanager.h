#pragma once
#include "user.h"
#include <string>
#include <vector>

using namespace std;

struct ServerBulletinConfig {
	bool enabled = true;
	unsigned short noticeID = 1;
	unsigned char category = 2; // 0: Etc, 1: Hot, 2: Notice, 3: Event
	string title = "CSO 2013 伺服器公告";
	string content = "歡迎來到 CSO 2013 懷舊伺服器！祝您遊戲愉快！\n\n本伺服器為 2013 懷舊測試服，如有任何問題請聯繫管理員。\n輸入 /help 可查看聊天指令列表；輸入 /resetinv 可重置官方預設裝備。";
	bool sendOnLogin = true;
};

struct WelcomeNoticeConfig {
	bool enabled = true;
	string type = "ServerMessage"; // "ServerMessage", "PublicNotice", "RollingBanner", "ServerYell", "Chat", "None"
	string message = "歡迎來到 CSO 2013 懷舊測試服，祝您遊戲愉快！";
};

struct PeriodicAnnouncementConfig {
	bool enabled = true;
	unsigned int intervalSeconds = 300;
	string type = "Chat"; // "Chat", "RollingBanner", "ServerYell", "ServerMessage"
	vector<string> messages = {
		"本伺服器為 2013 懷舊測試服，祝您遊戲愉快！",
		"輸入 /help 可查看聊天指令列表；輸入 /resetinv 可重置官方預設裝備。",
		"歡迎使用 /additem <ID> 測試各種經典武器與裝備！"
	};
};

struct NoticeConfig {
	ServerBulletinConfig serverBulletin;
	WelcomeNoticeConfig welcomeNotice;
	PeriodicAnnouncementConfig periodicAnnouncements;
};

class NoticeManager {
public:
	NoticeManager();

	bool Init();
	bool LoadConfig();
	void Reload();

	void OnSecondTick();
	void SendServerBulletin(User* user);
	void BroadcastServerBulletin();
	void SendWelcomeNotice(User* user);

	const NoticeConfig& GetConfig() const noexcept { return _config; }
	NoticeConfig& GetConfig() noexcept { return _config; }

private:
	void saveDefaultConfig(const string& path);

	NoticeConfig _config;
	unsigned int _secondCounter;
	size_t _currentMessageIndex;
};

extern NoticeManager noticeManager;
