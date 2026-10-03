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

class DatabaseManager {
public:
	DatabaseManager();
	~DatabaseManager();

	bool Init(const string& server, const string& user, const string& password, const string& database);
	void Shutdown();
	const LoginResult Login(const string& userName, const string& password);
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
	bool UpdateUserInventoryExpiration(unsigned long userID, unsigned short itemID, unsigned long newExpirationTime);
	bool ResetUserInventory(unsigned long userID);
	bool SaveUserBattleResult(unsigned long userID, unsigned long long addedExp, unsigned long long addedPoints, unsigned short addedFrags, unsigned short addedDeaths, bool isWin, unsigned char newLevel, bool recordStats = true);

private:
	MYSQL* _connection;
};

extern DatabaseManager databaseManager;