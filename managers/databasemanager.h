#pragma once
#include "mysql/mysql.h"
#include "user.h"

struct LoginResult {
	unsigned long userID = 0;
	Packet_ReplyType reply = Packet_ReplyType::LoginSuccess;
};

struct TransferLoginResult {
	unsigned long userID = 0;
	string userName = "";
	Packet_ReplyType reply = Packet_ReplyType::LoginSuccess;
};

struct BingoSlot {
	unsigned char slotIdx = 0;  // 0 ~ 24
	unsigned char number = 0;   // 0 ~ 99
	unsigned char opened = 0;   // 0 or 1
};

struct BingoPrize {
	unsigned char lineIdx = 0;  // 0 ~ 11
	unsigned short itemID = 0;
	unsigned char count = 1;
	unsigned char duration = 0; // days, 0 = permanent
	unsigned char opened = 0;   // 0 or 1
};

struct BingoBoard {
	unsigned long userID = 0;
	int status = 0;
	bool canPlay = true;
	vector<BingoSlot> slots;   // 25 items
	vector<BingoPrize> prizes; // 12 items
};

struct AttendanceRecord {
	unsigned long userID = 0;
	unsigned char consecutiveDays = 0;
	unsigned int totalCheckIns = 0;
	string lastCheckInDate = "";
	bool canCheckIn = true;
	int daysSinceLast = 999;
};

class DatabaseManager {
public:
	DatabaseManager();
	~DatabaseManager();

	bool Init(const string& server, const string& user, const string& password, const string& database);
	void Shutdown();
	const LoginResult Login(const string& userName, const string& password);
	bool IsNickNameExists(const string& nickName);
	bool UpdateUserNickName(unsigned long userID, const string& nickName);
	bool UpdateUserCamoNickName(unsigned long userID, const string& camoNickName);
	char CreateUserCharacter(unsigned long userID, const string& nickName);
	const UserCharacterResult GetUserCharacter(unsigned long userID, unsigned long flag);
	bool CreateUserBuymenus(unsigned long userID);
	bool CreateUserBookmarks(unsigned long userID);
	bool CreateUserInventory(unsigned long userID);
	char AddUserSession(unsigned long userID);
	void RemoveUserSession(unsigned long userID);
	char IsUserSessionAdded(unsigned long userID);
	void RemoveAllUserSessions();
	const TransferLoginResult TransferLogin(const string& authToken);
	char AddUserTransfer(unsigned long userID, const string& authToken, unsigned char serverID, unsigned char channelID);
	void RemoveUserTransfer(unsigned long userID);
	void RemoveOldUserTransfers();
	void RemoveAllUserTransfers();
	bool SaveUserOption(unsigned long userID, const vector<unsigned char>& userOption);
	const vector<unsigned char> GetUserOption(unsigned long userID);
	bool SaveUserBuyMenu(unsigned long userID, unsigned char categoryID, unsigned char slotID, unsigned short itemID);
	const vector<BuyMenu> GetUserBuyMenus(unsigned long userID);
	bool SaveUserItemSetSlot(unsigned long userID, unsigned char slotID, unsigned short itemID);
	ItemSet GetUserItemSet(unsigned long userID);
	bool SaveUserBookMark(unsigned long userID, const BookMark& userBookMark);
	const vector<BookMark> GetUserBookMarks(unsigned long userID);
	const vector<InventoryItem> GetUserInventory(unsigned long userID);
	bool SetUserInventoryItemInUse(unsigned long userID, unsigned short slotID, bool inUse);
	bool SetUserInventoryItemsUnused(unsigned long userID, const vector<unsigned short>& slotIDs);
	bool AddUserInventoryItems(unsigned long userID, const vector<InventoryItem>& newItems);
	bool UpdateUserInventoryItemCount(unsigned long userID, unsigned short itemID, unsigned short addedCount);
	bool ConsumeUserInventoryItem(unsigned long userID, unsigned short itemID, unsigned short count, unsigned short& outSlotID, bool& outDeleted);
	bool ConsumeUserInventoryItemBySlot(unsigned long userID, unsigned short slotID, unsigned short count, bool& outDeleted);
	bool UpdateUserInventoryExpiration(unsigned long userID, unsigned short itemID, unsigned long newExpirationTime);
	bool UpdateUserInventoryItemEnhance(unsigned long userID, unsigned short slotID, unsigned long enhanceBits);
	bool ResetUserInventory(unsigned long userID);
	bool SaveUserBattleResult(unsigned long userID, unsigned long long addedExp, unsigned long long addedPoints, unsigned short addedFrags, unsigned short addedDeaths, bool isWin, unsigned char newLevel, bool recordStats = true);
	bool DeductUserFunds(unsigned long userID, bool isPoints, unsigned long long amount);
	bool AddUserFunds(unsigned long userID, bool isPoints, unsigned long long amount);
	bool EnsureBingoTables();
	bool GetUserBingoBoard(unsigned long userID, BingoBoard& outBoard);
	bool SaveUserBingoBoard(const BingoBoard& board);
	bool UpdateUserBingoSlot(unsigned long userID, unsigned char slotIdx, unsigned char opened);
	bool UpdateUserBingoPrize(unsigned long userID, unsigned char lineIdx, unsigned char opened);
	bool ResetUserBingoBoard(unsigned long userID, const BingoBoard& newBoard);
	bool EnsureAttendanceTables();
	bool GetUserAttendance(unsigned long userID, AttendanceRecord& outRecord);
	bool RecordUserAttendance(unsigned long userID, unsigned char newConsecutiveDays);
	bool ResetUserAttendance(unsigned long userID, unsigned char targetStreak = 255);
	bool AdvanceUserAttendanceNextDay(unsigned long userID);
	bool EnsureTitleTables();
	bool GetUserTitles(unsigned long userID, vector<unsigned short>& outTitleIDs);
	bool AddUserTitle(unsigned long userID, unsigned short titleID);
	bool HasUserTitle(unsigned long userID, unsigned short titleID);
	bool SetUserPrefixID(unsigned long userID, unsigned short prefixID);
	bool SetUserHonorPoints(unsigned long userID, unsigned short honorPoints);
	bool AddUserHonorPoints(unsigned long userID, unsigned short points);
	bool SetUserBadges(unsigned long userID, const unsigned char badges[5]);
	bool SetUserBadgeSlot(unsigned long userID, unsigned char slot, unsigned char titleID);

private:
	MYSQL* _connection;
};

extern DatabaseManager databaseManager;