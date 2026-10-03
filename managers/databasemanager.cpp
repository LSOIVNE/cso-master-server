#include "databasemanager.h"
#include "serverconfig.h"
#include "serverconsole.h"
#include "mysql/mysqld_error.h"
#include "libbcrypt/bcrypt.h"
#include <tuple>
#include <ctime>
#include <unordered_set>
#include "udp_server.h"

DatabaseManager databaseManager;

DatabaseManager::DatabaseManager() {
    _connection = NULL;
}

DatabaseManager::~DatabaseManager() {
    Shutdown();
}

bool DatabaseManager::Init(const string& server, const string& user, const string& password, const string& database) {
    _connection = mysql_init(NULL);
    bool success = true;

    if (!mysql_real_connect(_connection, server.c_str(), user.c_str(), password.c_str(), database.c_str(), 0, NULL, 0)) {
        success = false;
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Connection error on Init: {}\n", mysql_error(_connection)));
    }

    tie(success, _connection) = make_tuple(success, _connection);

    return success;
}

void DatabaseManager::Shutdown() {
    if (_connection) {
        RemoveAllUserSessions();
        RemoveAllUserTransfers();

        mysql_close(_connection);
        _connection = NULL;
    }
}

const LoginResult DatabaseManager::Login(const string& userName, const string& password) {
    if (userName.empty()) {
        return { 0, Packet_ReplyType::InvalidName };
    }

    if (password.empty()) {
        return { 0, Packet_ReplyType::InvalidPassword };
    }

    char* userNameEscaped = new char[userName.size() * 2 + 1];
    mysql_real_escape_string(_connection, userNameEscaped, userName.c_str(), (unsigned long)userName.size());

    string query = format("SELECT userID, password FROM users WHERE userName = '{}';", userNameEscaped);
    delete[] userNameEscaped;

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on Login: {}\n", mysql_error(_connection)));
        return { 0, Packet_ReplyType::SysError };
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = mysql_fetch_row(res);

    LoginResult loginResult;

    if (row == NULL) {
        loginResult.reply = Packet_ReplyType::NotExist;
    }
    else {
        int ret = bcrypt_checkpw(password.c_str(), row[1]);
        switch (ret) {
            case -1: {
                loginResult.reply = Packet_ReplyType::SysError;
                break;
            }
            case 0: {
                loginResult.userID = atoi(row[0]);
                break;
            }
            default: {
                loginResult.reply = Packet_ReplyType::WrongPassword;
                break;
            }
        }
    }

    mysql_free_result(res);

    return loginResult;
}

char DatabaseManager::CreateUserCharacter(unsigned long userID, const string& nickName) {
    string query = format("INSERT INTO user_characters (userID, nickName) VALUES ({}, '{}');", userID, nickName);

    if (mysql_query(_connection, query.c_str())) {
        if (mysql_errno(_connection) == ER_DUP_ENTRY) {
            return 0;
        }

        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserCharacter: {}\n", mysql_error(_connection)));
        return -1;
    }

    query = format("INSERT INTO user_buymenus (userID, categoryID, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9) VALUES");
    for (auto& buyMenu : serverConfig.defaultBuyMenus) {
        query += format(" ({}, {}", userID, buyMenu.categoryID);
        for (unsigned char slotID = 0; slotID < BUYMENU_MAX_SLOT; slotID++) {
            query += format(", {}", buyMenu.items[slotID]);
        }
        query += "),";
    }
    query[query.size() - 1] = ';';

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserCharacter: {}\n", mysql_error(_connection)));
        return -1;
    }

    query = format("INSERT INTO user_bookmarks (userID, slotID, name, primaryItemID, primaryAmmo, secondaryItemID, secondaryAmmo, flashbang, hegrenade, smokegrenade, defusekit, nightvision, kevlar, unk1) VALUES");
    for (unsigned char slotID = 0; slotID < BOOKMARK_MAX_SLOT; slotID++) {
        query += format(" ({}, {}, '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),", userID, slotID);
    }
    query[query.size() - 1] = ';';

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserCharacter: {}\n", mysql_error(_connection)));
        return -1;
    }

    return 1;
}

const UserCharacterResult DatabaseManager::GetUserCharacter(unsigned long userID, unsigned long flag) {
    UserCharacter userCharacter;
    userCharacter.flag = flag;

    string info;

    if (flag == NULL) {
        info += " 1 ";
    }
    else {
        if (flag & USERCHARACTER_FLAG_UNK1) {
            info += " unk1,";
        }
        if (flag & USERCHARACTER_FLAG_NICKNAME) {
            info += " nickName,";
        }
        if (flag & USERCHARACTER_FLAG_UNK4) {
            info += " unk4_1, unk4_2, unk4_3, unk4_4,";
        }
        if (flag & USERCHARACTER_FLAG_LEVEL) {
            info += " level,";
        }
        if (flag & USERCHARACTER_FLAG_UNK10) {
            info += " unk10,";
        }
        if (flag & USERCHARACTER_FLAG_EXP) {
            info += " exp,";
        }
        if (flag & USERCHARACTER_FLAG_CASH) {
            info += " cash,";
        }
        if (flag & USERCHARACTER_FLAG_POINTS) {
            info += " points,";
        }
        if (flag & USERCHARACTER_FLAG_BATTLESTATS) {
            info += " battles, wins, frags, deaths,";
        }
        if (flag & USERCHARACTER_FLAG_LOCATION) {
            info += " city, county, neighborhood, unk200_5,";
        }
        if (flag & USERCHARACTER_FLAG_UNK400) {
            info += " unk400,";
        }
        if (flag & USERCHARACTER_FLAG_UNK800) {
            info += " unk800,";
        }
        if (flag & USERCHARACTER_FLAG_CLAN) {
            info += " clanID, clanMarkID, clanName, clanUnk4, clanUnk5, clanUnk6,";
        }
        info[info.size() - 1] = ' ';
    }

    string query = format("SELECT{}FROM user_characters WHERE userID = {};", info, userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserCharacter: {}\n", mysql_error(_connection)));
        return { userCharacter, -1 };
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = mysql_fetch_row(res);

    if (row == NULL) {
        mysql_free_result(res);
        return { userCharacter, 0 };
    }

    if (flag) {
        char index = 0;
        if (flag & USERCHARACTER_FLAG_UNK1) {
            userCharacter.unk1 = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_NICKNAME) {
            userCharacter.nickName = row[index++];
        }
        if (flag & USERCHARACTER_FLAG_UNK4) {
            userCharacter.unk4_1 = row[index++];
            userCharacter.unk4_2 = atoi(row[index++]);
            userCharacter.unk4_3 = atoi(row[index++]);
            userCharacter.unk4_4 = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_LEVEL) {
            userCharacter.level = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_UNK10) {
            userCharacter.unk10 = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_EXP) {
            userCharacter.exp = atoll(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_CASH) {
            userCharacter.cash = atoll(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_POINTS) {
            userCharacter.points = atoll(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_BATTLESTATS) {
            userCharacter.battles = atoi(row[index++]);
            userCharacter.wins = atoi(row[index++]);
            userCharacter.frags = atoi(row[index++]);
            userCharacter.deaths = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_LOCATION) {
            userCharacter.city = atoi(row[index++]);
            userCharacter.county = atoi(row[index++]);
            userCharacter.neighborhood = atoi(row[index++]);
            userCharacter.unk200_5 = row[index++];
        }
        if (flag & USERCHARACTER_FLAG_UNK400) {
            userCharacter.unk400 = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_UNK800) {
            userCharacter.unk800 = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_CLAN) {
            userCharacter.clanID = atoi(row[index++]);
            userCharacter.clanMarkID = atoi(row[index++]);
            userCharacter.clanName = row[index++];
            userCharacter.clanUnk4 = atoi(row[index++]);
            userCharacter.clanUnk5 = atoi(row[index++]);
            userCharacter.clanUnk6 = atoi(row[index++]);
        }
    }

    mysql_free_result(res);

    return { userCharacter, 1 };
}

bool DatabaseManager::CreateUserBuymenus(unsigned long userID) {
    string query = format("SELECT 1 FROM user_buymenus WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserBuymenus: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_use_result(_connection);

    if (mysql_fetch_row(res)) {
        mysql_free_result(res);
    }
    else {
        mysql_free_result(res);

        query = format("INSERT INTO user_buymenus (userID, categoryID, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9) VALUES");
        bool hasCat14 = false, hasCat15 = false;
        for (auto& buyMenu : serverConfig.defaultBuyMenus) {
            if (buyMenu.categoryID == 14) hasCat14 = true;
            if (buyMenu.categoryID == 15) hasCat15 = true;
            query += format(" ({}, {}", userID, buyMenu.categoryID);
            for (unsigned char slotID = 0; slotID < BUYMENU_MAX_SLOT; slotID++) {
                query += format(", {}", buyMenu.items[slotID]);
            }
            query += "),";
        }
        if (!hasCat14) {
            query += format(" ({}, 14, 161, 0, 0, 0, 0, 0, 0, 0, 0),", userID);
        }
        if (!hasCat15) {
            query += format(" ({}, 15, 161, 0, 0, 0, 0, 0, 0, 0, 0),", userID);
        }
        query[query.size() - 1] = ';';

        if (mysql_query(_connection, query.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserBuymenus: {}\n", mysql_error(_connection)));
            return false;
        }
    }

    return true;
}

bool DatabaseManager::CreateUserBookmarks(unsigned long userID) {
    string query = format("SELECT 1 FROM user_bookmarks WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserBookmarks: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_use_result(_connection);

    if (mysql_fetch_row(res)) {
        mysql_free_result(res);
    }
    else {
        mysql_free_result(res);

        string query = format("INSERT INTO user_bookmarks (userID, slotID, name, primaryItemID, primaryAmmo, secondaryItemID, secondaryAmmo, flashbang, hegrenade, smokegrenade, defusekit, nightvision, kevlar, unk1) VALUES");
        for (unsigned char slotID = 0; slotID < BOOKMARK_MAX_SLOT; slotID++) {
            query += format(" ({}, {}, '', 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0),", userID, slotID);
        }
        query[query.size() - 1] = ';';

        if (mysql_query(_connection, query.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserBookmarks: {}\n", mysql_error(_connection)));
            return false;
        }
    }

    return true;
}

bool DatabaseManager::CreateUserInventory(unsigned long userID) {
    string query = format("SELECT 1 FROM user_inventory WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserInventory: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_use_result(_connection);

    if (mysql_fetch_row(res)) {
        mysql_free_result(res);
    }
    else {
        mysql_free_result(res);

        time_t now;
        time(&now);
        static const std::unordered_set<unsigned short> kTerroristCharacters = {
            40, 41, 42, 43, 44, 45, 46, 47, 48,
            153, 154, 155, 254, 317, 319, 329, 360, 377, 393, 426, 432, 433, 510
        };

        static const std::unordered_set<unsigned short> kCTCharacters = {
            49, 50, 51, 52, 53, 54, 55, 56, 57,
            150, 151, 152, 253, 316, 318, 328, 361, 362, 376, 392, 418, 419, 425, 511
        };

        static const std::unordered_set<unsigned short> kTattooItems = {
            373, 374, 375, 386, 387, 9997, 9998, 9999
        };

        bool equippedT = false;
        bool equippedCT = false;
        bool equippedTattoo = false;
        unsigned short slotID = 1;

        string query = format("INSERT INTO user_inventory (userID, slotID, itemID, count, activeFlag, inUse, obtainTime, expirationTime, enhanceBits) VALUES");
        for (auto& itemID : serverConfig.defaultInventory) {
            int inUse = 1;
            if (kTerroristCharacters.count(itemID)) {
                if (!equippedT) {
                    inUse = 1;
                    equippedT = true;
                } else {
                    inUse = 0;
                }
            } else if (kCTCharacters.count(itemID)) {
                if (!equippedCT) {
                    inUse = 1;
                    equippedCT = true;
                } else {
                    inUse = 0;
                }
            } else if (kTattooItems.count(itemID)) {
                if (!equippedTattoo) {
                    inUse = 1;
                    equippedTattoo = true;
                } else {
                    inUse = 0;
                }
            }
            query += format(" ({}, {}, {}, 1, 1, {}, {}, 0, 0),", userID, slotID++, itemID, inUse, now);
        }
        query[query.size() - 1] = ';';

        if (mysql_query(_connection, query.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserInventory: {}\n", mysql_error(_connection)));
            return false;
        }
    }

    return true;
}

char DatabaseManager::AddUserSession(unsigned long userID) {
    string query = format("SELECT serverID, channelID FROM user_sessions WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserSession: {}\n", mysql_error(_connection)));
        return -1;
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = mysql_fetch_row(res);

    if (row == NULL) {
        mysql_free_result(res);

        query = format("INSERT INTO user_sessions (userID, serverID, channelID) VALUES ({}, {}, {});", userID, serverConfig.serverID, serverConfig.channelID);

        if (mysql_query(_connection, query.c_str())) {
            if (mysql_errno(_connection) == ER_DUP_ENTRY) {
                return 0;
            }

            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserSession: {}\n", mysql_error(_connection)));
            return -1;
        }
    }
    else {
        unsigned char serverID = atoi(row[0]);
        unsigned char channelID = atoi(row[1]);

        mysql_free_result(res);

        if (!serverID || serverID > serverConfig.serverList.size()) {
            return 0;
        }

        if (!channelID || channelID > serverConfig.serverList[(unsigned char)(serverID - 1)].channels.size()) {
            return 0;
        }

        if (serverConfig.serverList[(unsigned char)(serverID - 1)].channels[(unsigned char)(channelID - 1)].isOnline) {
            return 0;
        }

        query = format("DELETE FROM user_sessions WHERE userID = {};", userID);

        if (mysql_query(_connection, query.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserSession: {}\n", mysql_error(_connection)));
            return -1;
        }

        query = format("INSERT INTO user_sessions (userID, serverID, channelID) VALUES ({}, {}, {});", userID, serverConfig.serverID, serverConfig.channelID);

        if (mysql_query(_connection, query.c_str())) {
            if (mysql_errno(_connection) == ER_DUP_ENTRY) {
                return 0;
            }

            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserSession: {}\n", mysql_error(_connection)));
            return -1;
        }
    }

    return 1;
}

void DatabaseManager::RemoveUserSession(unsigned long userID) {
    string query = format("DELETE FROM user_sessions WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RemoveUserSession: {}\n", mysql_error(_connection)));
        return;
    }
}

char DatabaseManager::IsUserSessionAdded(unsigned long userID) {
    string query = format("SELECT serverID, channelID FROM user_sessions WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on IsUserSessionAdded: {}\n", mysql_error(_connection)));
        return -1;
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = mysql_fetch_row(res);

    if (row == NULL) {
        mysql_free_result(res);

        return 0;
    }
    else {
        unsigned char serverID = atoi(row[0]);
        unsigned char channelID = atoi(row[1]);

        mysql_free_result(res);

        if (!serverID || serverID > serverConfig.serverList.size()) {
            return 2;
        }

        if (!channelID || channelID > serverConfig.serverList[(unsigned char)(serverID - 1)].channels.size()) {
            return 2;
        }

        if (serverID != serverConfig.serverID || channelID != serverConfig.channelID) {
            return 2;
        }
    }

    return 1;
}

void DatabaseManager::RemoveAllUserSessions() {
    string query = format("DELETE FROM user_sessions WHERE serverID = {} AND channelID = {};", serverConfig.serverID, serverConfig.channelID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RemoveAllUserSessions: {}\n", mysql_error(_connection)));
        return;
    }
}

const TransferLoginResult DatabaseManager::TransferLogin(const string& authToken) {
    if (authToken.empty()) {
        return { 0, "", Packet_ReplyType::INVALID_USERINFO };
    }

    string query = format("SELECT userID FROM user_transfers WHERE authToken = '{}' AND serverID = {} AND channelID = {};", authToken, serverConfig.serverID, serverConfig.channelID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on TransferLogin: {}\n", mysql_error(_connection)));
        return { 0, "", Packet_ReplyType::SysError };
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = mysql_fetch_row(res);

    TransferLoginResult transferLoginResult;

    if (row == NULL) {
        transferLoginResult.reply = Packet_ReplyType::TRANSFER_ERR;
    }
    else {
        transferLoginResult.userID = atoi(row[0]);

        mysql_free_result(res);

        query = format("SELECT userName FROM users WHERE userID = {};", transferLoginResult.userID);

        if (mysql_query(_connection, query.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on TransferLogin: {}\n", mysql_error(_connection)));
            return { 0, "", Packet_ReplyType::SysError };
        }

        res = mysql_use_result(_connection);
        row = mysql_fetch_row(res);

        if (row == NULL) {
            transferLoginResult.reply = Packet_ReplyType::NotExist;
        }
        else {
            transferLoginResult.userName = row[0];
        }
    }

    mysql_free_result(res);

    return transferLoginResult;
}

char DatabaseManager::AddUserTransfer(unsigned long userID, const string& authToken, unsigned char serverID, unsigned char channelID) {
    string query = format("INSERT INTO user_transfers (userID, authToken, serverID, channelID, transferTime) VALUES ({}, '{}', {}, {}, {});", userID, authToken, serverID, channelID, serverConsole.GetCurrentTime());

    if (mysql_query(_connection, query.c_str())) {
        if (mysql_errno(_connection) == ER_DUP_ENTRY) {
            return 0;
        }

        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserTransfer: {}\n", mysql_error(_connection)));
        return -1;
    }

    return 1;
}

void DatabaseManager::RemoveUserTransfer(unsigned long userID) {
    string query = format("DELETE FROM user_transfers WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RemoveUserTransfer: {}\n", mysql_error(_connection)));
        return;
    }
}

void DatabaseManager::RemoveOldUserTransfers() {
    string query = format("DELETE FROM user_transfers WHERE serverID = {} AND channelID = {} AND transferTime < {};", serverConfig.serverID, serverConfig.channelID, serverConsole.GetCurrentTime() - 60);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RemoveOldUserTransfers: {}\n", mysql_error(_connection)));
        return;
    }
}

void DatabaseManager::RemoveAllUserTransfers() {
    string query = format("DELETE FROM user_transfers WHERE serverID = {} AND channelID = {};", serverConfig.serverID, serverConfig.channelID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RemoveAllUserTransfers: {}\n", mysql_error(_connection)));
        return;
    }
}

bool DatabaseManager::SaveUserOption(unsigned long userID, const vector<unsigned char>& userOption) {
    string userOptionHex;
    for (auto& c : userOption) {
        userOptionHex += format("{:02X}", c);
    }

    string query = format("INSERT INTO user_options (userID, userOption) VALUES ({}, 0x{}) ON DUPLICATE KEY UPDATE userOption = 0x{};", userID, userOptionHex, userOptionHex);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserOption: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

const vector<unsigned char> DatabaseManager::GetUserOption(unsigned long userID) {
    string query = format("SELECT userOption FROM user_options WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserOption: {}\n", mysql_error(_connection)));
        return {};
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = mysql_fetch_row(res);

    vector<unsigned char> userOption;

    if (row) {
        unsigned long* lengths = mysql_fetch_lengths(res);

        if (lengths == NULL) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Fetch lengths error on GetUserOption: {}\n", mysql_error(_connection)));
        }
        else {
            userOption.insert(userOption.begin(), row[0], row[0] + lengths[0]);
        }
    }

    mysql_free_result(res);

    return userOption;
}

bool DatabaseManager::SaveUserBuyMenu(unsigned long userID, unsigned char categoryID, unsigned char slotID, unsigned short itemID) {
    if (categoryID >= BUYMENU_TOTAL_CATEGORIES || slotID >= BUYMENU_MAX_SLOT) {
        return false;
    }

    string query = format("INSERT INTO user_buymenus (userID, categoryID, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9) "
                          "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}) "
                          "ON DUPLICATE KEY UPDATE slot{} = {};",
                          userID, categoryID,
                          (slotID == 0 ? itemID : 0),
                          (slotID == 1 ? itemID : 0),
                          (slotID == 2 ? itemID : 0),
                          (slotID == 3 ? itemID : 0),
                          (slotID == 4 ? itemID : 0),
                          (slotID == 5 ? itemID : 0),
                          (slotID == 6 ? itemID : 0),
                          (slotID == 7 ? itemID : 0),
                          (slotID == 8 ? itemID : 0),
                          slotID + 1, itemID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserBuyMenu: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

const vector<BuyMenu> DatabaseManager::GetUserBuyMenus(unsigned long userID) {
    string query = format("SELECT categoryID, slot1, slot2, slot3, slot4, slot5, slot6, slot7, slot8, slot9 FROM user_buymenus WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserBuyMenus: {}\n", mysql_error(_connection)));
        return {};
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = NULL;
    vector<BuyMenu> userBuyMenus;

    while ((row = mysql_fetch_row(res)))
    {
        BuyMenu buyMenu;
        buyMenu.categoryID = atoi(row[0]);

        for (unsigned char slotID = 0; slotID < BUYMENU_MAX_SLOT; slotID++) {
            buyMenu.items.push_back((unsigned short)atoi(row[slotID + 1]));
        }

        userBuyMenus.push_back(buyMenu);
    }

    mysql_free_result(res);

	return userBuyMenus;
}

bool DatabaseManager::SaveUserItemSetSlot(unsigned long userID, unsigned char slotID, unsigned short itemID) {
    string query = format("INSERT INTO user_itemsets (userID, slotID, itemID) VALUES ({}, {}, {}) ON DUPLICATE KEY UPDATE itemID = {};", userID, slotID, itemID, itemID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserItemSetSlot: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

ItemSet DatabaseManager::GetUserItemSet(unsigned long userID) {
    string query = format("SELECT slotID, itemID FROM user_itemsets WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserItemSet: {}\n", mysql_error(_connection)));
        return {};
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = NULL;
    ItemSet itemSet;

    while ((row = mysql_fetch_row(res)))
    {
        unsigned char slotID = (unsigned char)atoi(row[0]);
        unsigned short itemID = (unsigned short)atoi(row[1]);

        if (slotID == 0) {
            itemSet.characterItemID = itemID;
        }
        else if (slotID == 1) {
            itemSet.activeTab = (unsigned char)itemID;
        }
        else if (slotID >= 10 && slotID < 40) {
            int loadout = (slotID / 10) - 1;
            int weapon = slotID % 10;
            if (loadout >= 0 && loadout < 3 && weapon < 4) {
                itemSet.weapons[loadout][weapon] = itemID;
            }
        }
    }

    mysql_free_result(res);

    return itemSet;
}

bool DatabaseManager::SaveUserBookMark(unsigned long userID, const BookMark& userBookMark) {
    char* bookMarkName = new char[userBookMark.name.size() * 2 + 1];
    mysql_real_escape_string(_connection, bookMarkName, userBookMark.name.c_str(), (unsigned long)userBookMark.name.size());

    string query = format("UPDATE user_bookmarks SET name = '{}', primaryItemID = {}, primaryAmmo = {}, secondaryItemID = {}, secondaryAmmo = {}, flashbang = {}, hegrenade = {}, smokegrenade = {}, defusekit = {}, nightvision = {}, kevlar = {}, unk1 = {} WHERE userID = {} AND slotID = {};",
        bookMarkName, userBookMark.primaryItemID, userBookMark.primaryAmmo, userBookMark.secondaryItemID, userBookMark.secondaryAmmo, userBookMark.flashbang,
        userBookMark.hegrenade, userBookMark.smokegrenade, userBookMark.defusekit, userBookMark.nightvision, userBookMark.kevlar, userBookMark.unk1, userID, userBookMark.slotID);
    delete[] bookMarkName;

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserBookMark: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

const vector<BookMark> DatabaseManager::GetUserBookMarks(unsigned long userID) {
    string query = format("SELECT slotID, name, primaryItemID, primaryAmmo, secondaryItemID, secondaryAmmo, flashbang, hegrenade, smokegrenade, defusekit, nightvision, kevlar, unk1 FROM user_bookmarks WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserBookMarks: {}\n", mysql_error(_connection)));
        return {};
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = NULL;
    vector<BookMark> userBookMarks;

    while ((row = mysql_fetch_row(res)))
    {
        BookMark bookMark { (unsigned char)atoi(row[0]), row[1], (unsigned char)atoi(row[2]), (bool)atoi(row[3]), (unsigned char)atoi(row[4]), (bool)atoi(row[5]), (unsigned char)atoi(row[6]), 
                            (bool)atoi(row[7]), (bool)atoi(row[8]), (bool)atoi(row[9]), (bool)atoi(row[10]), (unsigned char)atoi(row[11]), (unsigned char)atoi(row[12]) };

        userBookMarks.push_back(bookMark);
    }

    mysql_free_result(res);

    return userBookMarks;
}

const vector<InventoryItem> DatabaseManager::GetUserInventory(unsigned long userID) {
    string query = format("SELECT slotID, itemID, count, activeFlag, inUse, obtainTime, expirationTime, enhanceBits FROM user_inventory WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserInventory: {}\n", mysql_error(_connection)));
        return {};
    }

    MYSQL_RES* res = mysql_use_result(_connection);
    MYSQL_ROW row = NULL;
    vector<InventoryItem> userInventory;

    while ((row = mysql_fetch_row(res)))
    {
        InventoryItem item{
            (unsigned short)atoi(row[0]),
            (unsigned short)atoi(row[1]),
            (unsigned short)atoi(row[2]),
            (unsigned char)atoi(row[3]),
            (bool)atoi(row[4]),
            (unsigned long)strtoul(row[5], NULL, 10),
            (unsigned long)strtoul(row[6], NULL, 10),
            (unsigned long)strtoul(row[7], NULL, 10)
        };

        userInventory.push_back(item);
    }

    mysql_free_result(res);

    return userInventory;
}

bool DatabaseManager::SetUserInventoryItemInUse(unsigned long userID, unsigned short slotID, bool inUse) {
    string query = format("UPDATE user_inventory SET inUse = {} WHERE userID = {} AND slotID = {};", inUse ? 1 : 0, userID, slotID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserInventoryItemInUse: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::SetUserInventoryItemsUnused(unsigned long userID, const vector<unsigned short>& slotIDs) {
    if (slotIDs.empty()) {
        return true;
    }

    string idList = "";
    for (size_t i = 0; i < slotIDs.size(); i++) {
        idList += to_string(slotIDs[i]);
        if (i + 1 < slotIDs.size()) {
            idList += ",";
        }
    }

    string query = format("UPDATE user_inventory SET inUse = 0 WHERE userID = {} AND slotID IN ({});", userID, idList);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserInventoryItemsUnused: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::AddUserInventoryItems(unsigned long userID, const vector<InventoryItem>& newItems) {
    if (newItems.empty() || _connection == NULL) {
        return true;
    }

    string query = "INSERT INTO user_inventory (userID, slotID, itemID, count, activeFlag, inUse, obtainTime, expirationTime, enhanceBits) VALUES ";
    for (size_t i = 0; i < newItems.size(); i++) {
        const auto& item = newItems[i];
        query += format("({}, {}, {}, {}, {}, {}, {}, {}, {})",
            userID, item.slotID, item.itemID, item.count,
            (unsigned int)(item.activeFlag == 0 ? 1 : item.activeFlag),
            item.inUse ? 1 : 0,
            item.obtainTime, item.expirationTime, item.enhanceBits);
        if (i + 1 < newItems.size()) {
            query += ", ";
        }
        else {
            query += ";";
        }
    }

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserInventoryItems: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Successfully added {} items to inventory for userID {}\n", newItems.size(), userID));
    return true;
}

bool DatabaseManager::UpdateUserInventoryItemCount(unsigned long userID, unsigned short itemID, unsigned short addedCount) {
    if (_connection == NULL) {
        return false;
    }

    string query = format("UPDATE user_inventory SET count = LEAST(count + {}, 999) WHERE userID = {} AND itemID = {};", addedCount, userID, itemID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on UpdateUserInventoryItemCount: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Successfully updated itemID {} count (+{}) for userID {}\n", itemID, addedCount, userID));
    return true;
}

bool DatabaseManager::UpdateUserInventoryExpiration(unsigned long userID, unsigned short itemID, unsigned long newExpirationTime) {
    if (_connection == NULL) {
        return false;
    }

    string query = format("UPDATE user_inventory SET expirationTime = {} WHERE userID = {} AND itemID = {};", newExpirationTime, userID, itemID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on UpdateUserInventoryExpiration: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Successfully updated itemID {} expirationTime ({}) for userID {}\n", itemID, newExpirationTime, userID));
    return true;
}

bool DatabaseManager::ResetUserInventory(unsigned long userID) {
    if (_connection == NULL) {
        return false;
    }

    string delQuery = format("DELETE FROM user_inventory WHERE userID = {};", userID);
    if (mysql_query(_connection, delQuery.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetUserInventory delete: {}\n", mysql_error(_connection)));
        return false;
    }

    bool res = CreateUserInventory(userID);
    if (res) {
        serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Successfully reset user inventory to default for userID {}\n", userID));
    }
    return res;
}

bool DatabaseManager::SaveUserBattleResult(unsigned long userID, unsigned long long addedExp, unsigned long long addedPoints, unsigned short addedFrags, unsigned short addedDeaths, bool isWin, unsigned char newLevel, bool recordStats) {
    if (_connection == NULL) {
        return false;
    }

    string query = format("UPDATE user_characters SET exp = exp + {}, points = points + {}, battles = battles + {}, wins = wins + {}, frags = frags + {}, deaths = deaths + {}, level = {} WHERE userID = {};",
        addedExp, addedPoints, recordStats ? 1 : 0, (recordStats && isWin) ? 1 : 0, recordStats ? addedFrags : 0, recordStats ? addedDeaths : 0, newLevel, userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserBattleResult: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Updated battle result for userID {}: +{} exp, +{} points, recordStats: {}, +{} frags, +{} deaths, win: {}, level: {}\n",
        userID, addedExp, addedPoints, recordStats ? 1 : 0, recordStats ? addedFrags : 0, recordStats ? addedDeaths : 0, (recordStats && isWin) ? 1 : 0, newLevel));

    return true;
}

