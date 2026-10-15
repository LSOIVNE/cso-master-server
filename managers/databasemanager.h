#pragma once
#include "mysql/mysql.h"
#include "user.h"
#include <map>
#include <string>
#include <vector>

struct UserQuestRecord {
	unsigned int questID = 0;
	unsigned char state = 0; // 0=InProgress, 1=Complete, 2=Claimed
	unsigned char isTracked = 0;
	unsigned short count0 = 0;
	unsigned short count1 = 0;
	unsigned short count2 = 0;
	unsigned short count3 = 0;
	unsigned short count4 = 0;
	unsigned short count5 = 0;
	string lastUpdatedDate = "";
};

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

struct UserGift {
	unsigned int giftID = 0;
	unsigned long receiverUserID = 0;
	unsigned long senderUserID = 0;
	std::string senderNickName = "";
	unsigned int productID = 0;
	unsigned short itemID = 0;
	unsigned short duration = 0;
	unsigned char isCount = 0;
	unsigned short count = 1;
	std::string message = "";
	unsigned int createTime = 0;
	unsigned char claimed = 0;
};

class DatabaseManager {
public:
	DatabaseManager();
	~DatabaseManager();

	bool Init(const string& server, const string& user, const string& password, const string& database);
	void Shutdown();
	const LoginResult Login(const string& userName, const string& password);
	int RegisterUser(const string& userName, const string& password, string& outErrorMsg);
	bool IsNickNameExists(const string& nickName);
	bool GetUserByNickName(const string& nickName, unsigned long& outUserID, unsigned char& outLevel);
	bool UpdateUserNickName(unsigned long userID, const string& nickName);
	bool UpdateUserCamoNickName(unsigned long userID, const string& camoNickName);
	bool UpdateUserLocation(unsigned long userID, unsigned short city, unsigned short county, unsigned short neighborhood);
	bool ResetUserWinLose(unsigned long userID);
	bool ResetUserKillDeath(unsigned long userID);
	char CreateUserCharacter(unsigned long userID, const string& nickName);
	const UserCharacterResult GetUserCharacter(unsigned long userID, unsigned long flag);
	bool CreateUserBuymenus(unsigned long userID);
	bool CreateUserBookmarks(unsigned long userID);
	bool CreateUserInventory(unsigned long userID);
	bool CreateUserItemsets(unsigned long userID);
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
	bool DeleteUserInventoryItem(unsigned long userID, unsigned short itemID, unsigned short count, vector<unsigned short>& outDeletedSlots, unsigned short& outActualDeletedCount);
	unsigned long GetUserCBoxCount(unsigned long userID);
	bool SetUserCBoxCount(unsigned long userID, unsigned long count);
	bool AddUserCBoxCount(unsigned long userID, long delta, unsigned long* outNewCount = nullptr);
	struct ExpiredItemRecord {
		unsigned short slotID = 0;
		unsigned short itemID = 0;
	};
	bool RemoveExpiredInventoryItems(unsigned long userID, vector<ExpiredItemRecord>& outExpiredItems);
	bool SetUserInventoryItemExpirationSeconds(unsigned long userID, unsigned short itemID, unsigned int seconds);
	bool SaveUserBattleResult(unsigned long userID, unsigned long long addedExp, unsigned long long addedPoints, unsigned short addedFrags, unsigned short addedDeaths, bool isWin, unsigned char newLevel, bool recordStats = true, unsigned int addedZombieKills = 0, unsigned int addedSurvivals = 0);
	bool RecordUserLeavePenalty(unsigned long userID, bool deductPoints, unsigned long pointAmount, bool recordDefeat);
	bool SaveUserLeaveExpAndPoints(unsigned long userID, unsigned long long addedExp, unsigned long long addedPoints, bool recordDefeat);
	bool AddUserExp(unsigned long userID, unsigned long long addedExp, unsigned char* outNewLevel = nullptr);
	bool SetUserLevel(unsigned long userID, unsigned char newLevel);
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
	unsigned short GetUserHonorPoints(unsigned long userID);
	bool SetUserHonorPoints(unsigned long userID, unsigned short honorPoints);
	bool AddUserHonorPoints(unsigned long userID, unsigned short points);
	bool SetUserBadges(unsigned long userID, const unsigned char badges[5]);
	bool SetUserBadgeSlot(unsigned long userID, unsigned char slot, unsigned char titleID);

	// Quests
	bool EnsureQuestTables();
	bool GetUserQuests(unsigned long userID, map<unsigned int, UserQuestRecord>& outQuests);
	bool SaveUserQuest(unsigned long userID, const UserQuestRecord& record);
	bool ResetDailyQuests(unsigned long userID);
	bool ResetUserQuestRecord(unsigned long userID, unsigned int questID);
	bool ResetUserDailyQuests(unsigned long userID);
	bool ResetUserWeeklyQuests(unsigned long userID);
	bool DeleteUserSpecialQuest(unsigned long userID);
	bool SetUserQuestTracked(unsigned long userID, unsigned int questID, unsigned char isTracked);
	bool GetUserQuestStats(unsigned long userID, struct UserQuestStats& outStats);
	bool RecordSpecialQuestCompleted(unsigned long userID);
	bool RecordDailyQuestCompleted(unsigned long userID);

	// Gifts
	bool EnsureGiftTables();
	bool EnsureBookmarkTables();
	bool AddUserGift(const UserGift& gift, unsigned int& outGiftID);
	const std::vector<UserGift> GetUserGifts(unsigned long receiverUserID);
	bool GetUserGiftByID(unsigned int giftID, unsigned long receiverUserID, UserGift& outGift);
	bool ClaimUserGift(unsigned int giftID, unsigned long receiverUserID, unsigned short claimCount, unsigned short& outRemainingCount);

	// Shelter Survival (Mode 26)
	bool EnsureShelterTables();
	bool GetUserShelterStats(unsigned long userID, unsigned int& outTotalDays, unsigned int& outAccumRescuee);
	bool UpdateUserShelterStats(unsigned long userID, unsigned int matchDays, unsigned int matchRescuee,
		unsigned int& outTotalDays, unsigned int& outAccumRescuee);
	bool SetUserZombieStats(unsigned long userID, unsigned int zombieKills, unsigned int survivals);

private:
	MYSQL* _connection;
};

struct UserQuestStats {
	unsigned int honorPoints = 0;
	unsigned short specialQuestCount = 0;        // 特殊任務連續完成天數 (Bit 1, UInt16)
	unsigned short specialQuestStreakRecord = 0;  // 特殊任務連續完成最高紀錄 (Bit 2, UInt16)
	unsigned char todayRegularQuest = 0;         // 今天已完成的每日任務個數 (Bit 3, UInt8)
	unsigned short allClearCount = 0;            // 今天每日任務已全部完成累積天數/次數 (Bit 5, UInt16)
	unsigned char resetSpecialQuestCount = 5;    // 特殊任務剩餘重置次數 (Bit 7, UInt8)
	unsigned char todayPCQuest = 0;              // 今天達成的網咖任務個數 (Bit 9, UInt8, 暫未實現固定為0)
};

extern DatabaseManager databaseManager;