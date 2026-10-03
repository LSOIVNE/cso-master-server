#include "databasemanager.h"

User::User(TCPConnection::pointer connection, unsigned long userID, const string& userName) : _connection(connection), _userID(userID), _userName(userName) {
	if (_connection == NULL) {
		delete this;
		return;
	}

	_userStatus = UserStatus::InLogin;
	_currentRoomID = 0;
}

void User::SetUserNetwork(PortType portType, unsigned long externalIP, unsigned short externalPort, unsigned long localIP, unsigned short localPort, unsigned long long currentTime) noexcept {
	_userNetwork.externalIP = externalIP;
	_userNetwork.localIP = localIP;

	switch (portType) {
		case PortType::Guest: {
			_userNetwork.externalGuestPort = externalPort;
			_userNetwork.localGuestPort = localPort;
			_userNetwork.lastGuestHeartbeat = currentTime;
			_userNetwork.networkSet |= 0x1;
			break;
		}
		case PortType::Host: {
			_userNetwork.externalHostPort = externalPort;
			_userNetwork.localHostPort = localPort;
			_userNetwork.lastHostHeartbeat = currentTime;
			_userNetwork.networkSet |= 0x2;
			break;
		}
	}
}

void User::SetUserNetworkHeartbeat(PortType portType, unsigned long long currentTime) noexcept {
	switch (portType) {
		case PortType::Guest: {
			_userNetwork.lastGuestHeartbeat = currentTime;
			break;
		}
		case PortType::Host: {
			_userNetwork.lastHostHeartbeat = currentTime;
			break;
		}
	}
}

char User::CreateUserCharacter(const string& nickName) const noexcept {
	return databaseManager.CreateUserCharacter(_userID, nickName);
}

UserCharacterResult User::GetUserCharacter(unsigned long flag) const noexcept {
	return databaseManager.GetUserCharacter(_userID, flag);
}

char User::CreateUserBuymenus() const noexcept {
	return databaseManager.CreateUserBuymenus(_userID);
}

char User::CreateUserBookmarks() const noexcept {
	return databaseManager.CreateUserBookmarks(_userID);
}

char User::CreateUserInventory() const noexcept {
	return databaseManager.CreateUserInventory(_userID);
}

char User::AddUserSession() const noexcept {
	return databaseManager.AddUserSession(_userID);
}

void User::RemoveUserSession() const noexcept {
	databaseManager.RemoveUserSession(_userID);
}

char User::AddUserTransfer(const string& authToken, unsigned char serverID, unsigned char channelID) const noexcept {
	return databaseManager.AddUserTransfer(_userID, authToken, serverID, channelID);
}

void User::RemoveUserTransfer() const noexcept {
	databaseManager.RemoveUserTransfer(_userID);
}

char User::IsUserCharacterExists() const noexcept {
	const UserCharacterResult& userCharacterResult = GetUserCharacter(NULL);

	return userCharacterResult.result;
}

bool User::SaveUserOption(const vector<unsigned char>& userOption) const noexcept {
	return databaseManager.SaveUserOption(_userID, userOption);
}

const vector<unsigned char> User::GetUserOption() const noexcept {
	return databaseManager.GetUserOption(_userID);
}

bool User::SaveUserBuyMenu(unsigned char categoryID, unsigned char slotID, unsigned short itemID) const noexcept {
	return databaseManager.SaveUserBuyMenu(_userID, categoryID, slotID, itemID);
}

const vector<BuyMenu> User::GetUserBuyMenus() const noexcept {
	return databaseManager.GetUserBuyMenus(_userID);
}

bool User::SaveUserItemSetSlot(unsigned char slotID, unsigned short itemID) const noexcept {
	return databaseManager.SaveUserItemSetSlot(_userID, slotID, itemID);
}

ItemSet User::GetUserItemSet() const noexcept {
	return databaseManager.GetUserItemSet(_userID);
}

bool User::SaveUserBookMark(const BookMark& userBookMark) const noexcept {
	return databaseManager.SaveUserBookMark(_userID, userBookMark);
}

const vector<BookMark> User::GetUserBookMarks() const noexcept {
	return databaseManager.GetUserBookMarks(_userID);
}

const vector<InventoryItem> User::GetUserInventory() const noexcept {
	return databaseManager.GetUserInventory(_userID);
}

static const unsigned long long LEVEL_EXP_TABLE[] = {
	605ULL, 1260ULL, 1964ULL, 2725ULL, 3543ULL, 4418ULL, 5359ULL, 6374ULL, 7462ULL, 8632ULL,
	9892ULL, 11250ULL, 12715ULL, 14286ULL, 15971ULL, 17779ULL, 19726ULL, 21821ULL, 24071ULL, 26493ULL,
	29095ULL, 31893ULL, 34904ULL, 38144ULL, 41629ULL, 45376ULL, 49401ULL, 53729ULL, 58384ULL, 63391ULL,
	68775ULL, 74568ULL, 80794ULL, 87487ULL, 94687ULL, 102427ULL, 110748ULL, 119699ULL, 129321ULL, 139663ULL,
	150782ULL, 162736ULL, 175590ULL, 189409ULL, 204267ULL, 220246ULL, 237428ULL, 254610ULL, 271792ULL, 288974ULL,
	306156ULL, 323338ULL, 340520ULL, 357702ULL, 374884ULL, 392066ULL, 409248ULL, 426430ULL, 443612ULL, 460794ULL,
	477976ULL, 495158ULL, 512340ULL, 529522ULL, 546704ULL, 563886ULL, 581068ULL, 598250ULL, 615432ULL, 632614ULL,
	649796ULL
};

unsigned char User::CalculateLevel(unsigned long long exp) {
	unsigned char level = 1;
	for (size_t i = 0; i < sizeof(LEVEL_EXP_TABLE) / sizeof(LEVEL_EXP_TABLE[0]); ++i) {
		if (exp >= LEVEL_EXP_TABLE[i]) {
			level = (unsigned char)(i + 2);
		}
		else {
			break;
		}
	}
	if (level > 72) level = 72;
	return level;
}