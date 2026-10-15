#pragma once
#include "tcp_connection.h"

#ifdef GetUserName
#undef GetUserName
#endif

constexpr auto HARDWARE_ID_SIZE = 16;

#include <vector>

constexpr auto USERCHARACTER_FLAG_UNK1 =		0x1;
constexpr auto USERCHARACTER_FLAG_NICKNAME =	0x2;
constexpr auto USERCHARACTER_FLAG_UNK4 =		0x4;
constexpr auto USERCHARACTER_FLAG_LEVEL =		0x8;
constexpr auto USERCHARACTER_FLAG_UNK10 =		0x10;
constexpr auto USERCHARACTER_FLAG_EXP =			0x20;
constexpr auto USERCHARACTER_FLAG_CASH =		0x40;
constexpr auto USERCHARACTER_FLAG_POINTS =		0x80;
constexpr auto USERCHARACTER_FLAG_BATTLESTATS = 0x100;
constexpr auto USERCHARACTER_FLAG_LOCATION =	0x200;
constexpr auto USERCHARACTER_FLAG_UNK400 =		0x400;
constexpr auto USERCHARACTER_FLAG_CASH_UINT32 =	0x400; // CSO 2013 client reads cash/gash from this flag into CGamePlayer::m_nCash (offset 312)
constexpr auto USERCHARACTER_FLAG_UNK800 =		0x800;
constexpr auto USERCHARACTER_FLAG_CLAN =		0x1000;
constexpr auto USERCHARACTER_FLAG_TITLES =		0x4000; // 0x4000: League/Rank (UInt32, 勿包含於 ALL)
constexpr auto USERCHARACTER_FLAG_CBOXCOUNT =	0x10000; // 0x10000: 密碼箱數量 (UInt16, 存入 CGamePlayer +0x188)
constexpr auto USERCHARACTER_FLAG_PREFIXID_EX =	0x20000; // 0x20000: 當前佩戴稱號前綴 ID 擴展 (UInt32 0)
constexpr auto USERCHARACTER_FLAG_HONORPOINTS = 0x40000; // 0x40000: 當前佩戴稱號 ID (UInt16 prefixID) + 累積榮譽點數 (UInt32 honorPoints)
constexpr auto USERCHARACTER_FLAG_PREFIXID =	USERCHARACTER_FLAG_HONORPOINTS; // prefixID 存於 0x40000 的第一欄位
constexpr auto USERCHARACTER_FLAG_ACHIEVEMENTS = 0x80000; // 0x80000: 稱號/成就已解鎖遮罩 (128 bytes bitmask)
constexpr auto USERCHARACTER_FLAG_CHATCOUNT =	0x200000; // 0x200000: 頻道廣播次數 (勿包含於 ALL，否則覆蓋排名字節顯示 255 名)
constexpr auto USERCHARACTER_FLAG_COSTUME =     0x400000;
constexpr auto USERCHARACTER_FLAG_ALL =			0x1FFF | USERCHARACTER_FLAG_CBOXCOUNT | USERCHARACTER_FLAG_HONORPOINTS | USERCHARACTER_FLAG_ACHIEVEMENTS;

struct UserCharacter {
	unsigned long flag = 0;
	unsigned char unk1 = 0;
	string nickName = "";
	string unk4_1 = "";
	unsigned char unk4_2 = 0;
	unsigned char unk4_3 = 0;
	unsigned char unk4_4 = 0;
	unsigned char level = 0;
	unsigned char unk10 = 0;
	unsigned long long exp = 0;
	unsigned long long cash = 0;
	unsigned long long points = 0;
	unsigned long battles = 0;
	unsigned long wins = 0;
	unsigned long frags = 0;
	unsigned long deaths = 0;
	unsigned long battleStats4 = 0; // zombieKills (Mode 8, 9, 14 - #CSO_ZombieKillCount)
	unsigned long battleStats5 = 0; // survivals (Mode 8, 9, 14 - #CSO_SurvivalCount)
	unsigned short city = 0;
	unsigned short county = 0;
	unsigned short neighborhood = 0;
	string unk200_5 = "";
	unsigned long unk400 = 0;
	unsigned char unk800 = 0;
	unsigned long clanID = 0;
	unsigned long clanMarkID = 0;
	string clanName = "";
	unsigned char clanUnk4 = 0;
	unsigned char clanUnk5 = 0;
	unsigned char clanUnk6 = 0;
	unsigned short prefixID = 0;
	unsigned short honorPoints = 0;
	unsigned long cboxCount = 0;
	unsigned char badges[5] = { 0, 0, 0, 0, 0 };
	vector<unsigned char> achievements;
	unsigned char chatRemainCount = 255;
};

struct UserCharacterResult {
	UserCharacter userCharacter;
	char result = 0;
};

enum PortType {
	Guest = 0,
	Host = 1
};

struct UserNetwork {
	unsigned long externalIP = 0;
	unsigned short externalHostPort = 0;
	unsigned short externalGuestPort = 0;
	unsigned long localIP = 0;
	unsigned short localHostPort = 0;
	unsigned short localGuestPort = 0;
	unsigned long long lastHostHeartbeat = 0;
	unsigned long long lastGuestHeartbeat = 0;
	char networkSet = 0;
};

enum UserStatus {
	InLogin = 0,
	InServerList = 1,
	InLobby = 2,
	InRoom = 3,
	InGame = 4
};

class User {
public:
	User(TCPConnection::pointer connection, unsigned long userID, const string& userName);

	const TCPConnection::pointer& GetConnection() const noexcept {
		return _connection;
	}

	const string GetUserLogName() const noexcept {
		return format("{}, {}", _userID, _userName);
	}

	const string& GetUserIPAddress() const noexcept {
		return _connection->GetIPAddress();
	}

	unsigned long GetUserID() const noexcept {
		return _userID;
	}

	const string& GetUserName() const noexcept {
		return _userName;
	}

	const UserNetwork& GetUserNetwork() const noexcept {
		return _userNetwork;
	}

	void SetUserNetwork(PortType portType, unsigned long externalIP, unsigned short externalPort, unsigned long localIP, unsigned short localPort, unsigned long long currentTime) noexcept;
	void SetUserNetworkHeartbeat(PortType portType, unsigned long long currentTime) noexcept;

	UserStatus GetUserStatus() const noexcept {
		return _userStatus;
	}

	void SetUserStatus(UserStatus userStatus) noexcept {
		_userStatus = userStatus;
	}

	unsigned short GetCurrentRoomID() const noexcept {
		return _currentRoomID;
	}

	void SetCurrentRoomID(unsigned short roomID) noexcept {
		_currentRoomID = roomID;
	}

	char CreateUserCharacter(const string& nickName) const noexcept;
	bool UpdateUserNickName(const string& nickName) const noexcept;
	bool UpdateUserCamoNickName(const string& camoNickName) const noexcept;
	UserCharacterResult GetUserCharacter(unsigned long flag) const noexcept;
	char CreateUserBuymenus() const noexcept;
	char CreateUserBookmarks() const noexcept;
	char CreateUserInventory() const noexcept;
	char CreateUserItemsets() const noexcept;
	char AddUserSession() const noexcept;
	void RemoveUserSession() const noexcept;
	char AddUserTransfer(const string& authToken, unsigned char serverID, unsigned char channelID) const noexcept;
	void RemoveUserTransfer() const noexcept;
	char IsUserCharacterExists() const noexcept;
	bool SaveUserOption(const vector<unsigned char>& userOption) const noexcept;
	const vector<unsigned char> GetUserOption() const noexcept;
	bool SaveUserBuyMenu(unsigned char categoryID, unsigned char slotID, unsigned short itemID) const noexcept;
	const vector<BuyMenu> GetUserBuyMenus() const noexcept;
	bool SaveUserItemSetSlot(unsigned char slotID, unsigned short itemID) const noexcept;
	ItemSet GetUserItemSet() const noexcept;
	bool SaveUserBookMark(const BookMark& userBookMark) const noexcept;
	const vector<BookMark> GetUserBookMarks() const noexcept;
	const vector<InventoryItem> GetUserInventory() const noexcept;
	bool HasItem(unsigned short itemID) const noexcept;
	unsigned long GetCBoxCount() const noexcept;
	bool SetCBoxCount(unsigned long count) const noexcept;
	bool AddCBoxCount(long delta, unsigned long* outNewCount = nullptr) const noexcept;
	static unsigned char CalculateLevel(unsigned long long exp);
	static unsigned long long GetExpForLevel(unsigned char level);

	void AddPendingExpireNoticeItemID(unsigned short itemID) noexcept {
		_pendingExpireNoticeItemIDs.push_back(itemID);
	}
	vector<unsigned short> GetAndClearPendingExpireNoticeItemIDs() noexcept {
		vector<unsigned short> ids = std::move(_pendingExpireNoticeItemIDs);
		_pendingExpireNoticeItemIDs.clear();
		return ids;
	}
	bool HasPendingExpireNotices() const noexcept {
		return !_pendingExpireNoticeItemIDs.empty();
	}

private:
	TCPConnection::pointer _connection;
	unsigned long _userID;
	string _userName;
	UserNetwork _userNetwork;
	UserStatus _userStatus;
	unsigned short _currentRoomID;
	vector<unsigned short> _pendingExpireNoticeItemIDs;
};

struct GameUser {
	User* user;
	UserCharacter userCharacter;
};