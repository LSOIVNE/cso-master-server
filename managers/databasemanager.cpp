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

    if (success) {
        EnsureBingoTables();
        EnsureAttendanceTables();
        EnsureTitleTables();
    }

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

bool DatabaseManager::IsNickNameExists(const string& nickName) {
    if (_connection == NULL) {
        return false;
    }

    char escaped[64] = {};
    mysql_real_escape_string(_connection, escaped, nickName.c_str(), (unsigned long)nickName.size());

    string query = format("SELECT userID FROM user_characters WHERE nickName = '{}' LIMIT 1;", escaped);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on IsNickNameExists: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return false;
    }

    bool exists = (mysql_num_rows(res) > 0);
    mysql_free_result(res);
    return exists;
}

bool DatabaseManager::UpdateUserNickName(unsigned long userID, const string& nickName) {
    if (_connection == NULL) {
        return false;
    }

    char escaped[64] = {};
    mysql_real_escape_string(_connection, escaped, nickName.c_str(), (unsigned long)nickName.size());

    string query = format("UPDATE user_characters SET nickName = '{}' WHERE userID = {};", escaped, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on UpdateUserNickName: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Updated nickName for userID {} to '{}'\n", userID, nickName));
    return true;
}

bool DatabaseManager::UpdateUserCamoNickName(unsigned long userID, const string& camoNickName) {
    if (_connection == NULL) {
        return false;
    }

    char escaped[64] = {};
    mysql_real_escape_string(_connection, escaped, camoNickName.c_str(), (unsigned long)camoNickName.size());

    string query = format("UPDATE user_characters SET unk4_1 = '{}' WHERE userID = {};", escaped, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on UpdateUserCamoNickName: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Updated camoNickName (unk4_1) for userID {} to '{}'\n", userID, camoNickName));
    return true;
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
        if (flag & USERCHARACTER_FLAG_TITLES) {
            info += " badge0, badge1, badge2, badge3, badge4,";
        }
        if (flag & USERCHARACTER_FLAG_PREFIXID) {
            info += " prefixID,";
        }
        if (flag & USERCHARACTER_FLAG_HONORPOINTS) {
            info += " honorPoints,";
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
            // CSO 2013 client uses flag 0x400 (32-bit int) as Cash / GASH balance in CGamePlayer (offset 312)
            if (userCharacter.unk400 == 0 && userCharacter.cash > 0) {
                userCharacter.unk400 = (unsigned long)userCharacter.cash;
            }
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
        if (flag & USERCHARACTER_FLAG_TITLES) {
            userCharacter.badges[0] = atoi(row[index++]);
            userCharacter.badges[1] = atoi(row[index++]);
            userCharacter.badges[2] = atoi(row[index++]);
            userCharacter.badges[3] = atoi(row[index++]);
            userCharacter.badges[4] = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_PREFIXID) {
            userCharacter.prefixID = atoi(row[index++]);
        }
        if (flag & USERCHARACTER_FLAG_HONORPOINTS) {
            userCharacter.honorPoints = atoi(row[index++]);
        }
    }

    mysql_free_result(res);

    if (flag & USERCHARACTER_FLAG_HONORPOINTS) {
        string cboxQuery = format("SELECT COALESCE(SUM(count), 0) FROM user_inventory WHERE userID = {} AND itemID = 166;", userID);
        if (!mysql_query(_connection, cboxQuery.c_str())) {
            MYSQL_RES* cboxRes = mysql_store_result(_connection);
            if (cboxRes) {
                MYSQL_ROW cboxRow = mysql_fetch_row(cboxRes);
                if (cboxRow && cboxRow[0]) {
                    userCharacter.cboxCount = (unsigned short)atoi(cboxRow[0]);
                }
                mysql_free_result(cboxRes);
            }
        }
        else {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserCharacter (cboxCount): {}\n", mysql_error(_connection)));
        }
    }

    if (flag & USERCHARACTER_FLAG_ACHIEVEMENTS) {
        userCharacter.achievements.assign(128, 0);
        string titleQuery = format("SELECT titleID FROM user_titles WHERE userID = {};", userID);
        if (!mysql_query(_connection, titleQuery.c_str())) {
            MYSQL_RES* tRes = mysql_store_result(_connection);
            if (tRes) {
                MYSQL_ROW tRow;
                while ((tRow = mysql_fetch_row(tRes))) {
                    if (tRow[0]) {
                        unsigned short tid = (unsigned short)atoi(tRow[0]);
                        if (tid > 0 && tid <= 1024) {
                            unsigned int byteIdx = tid / 8;
                            unsigned int bitIdx = tid % 8;
                            if (byteIdx < 128) {
                                userCharacter.achievements[byteIdx] |= (1 << bitIdx);
                            }
                        }
                    }
                }
                mysql_free_result(tRes);
            }
        }
        else {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserCharacter (achievements): {}\n", mysql_error(_connection)));
        }
    }

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

bool DatabaseManager::ConsumeUserInventoryItem(unsigned long userID, unsigned short itemID, unsigned short count, unsigned short& outSlotID, bool& outDeleted) {
    outSlotID = 0;
    outDeleted = false;
    if (_connection == NULL) {
        return false;
    }

    string query = format("SELECT slotID, count FROM user_inventory WHERE userID = {} AND itemID = {} ORDER BY count DESC LIMIT 1;", userID, itemID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ConsumeUserInventoryItem SELECT: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return false;
    }

    outSlotID = (unsigned short)atoi(row[0]);
    unsigned short currentCount = (unsigned short)atoi(row[1]);
    mysql_free_result(res);

    if (currentCount <= count) {
        // Completely consumed: delete the row
        string delQuery = format("DELETE FROM user_inventory WHERE userID = {} AND slotID = {};", userID, outSlotID);
        if (mysql_query(_connection, delQuery.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ConsumeUserInventoryItem DELETE: {}\n", mysql_error(_connection)));
            return false;
        }
        outDeleted = true;
        serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] ItemID {} (slotID {}) completely consumed and deleted for userID {}\n", itemID, outSlotID, userID));
    }
    else {
        // Partially consumed: decrement count
        string updateQuery = format("UPDATE user_inventory SET count = count - {} WHERE userID = {} AND slotID = {};", count, userID, outSlotID);
        if (mysql_query(_connection, updateQuery.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ConsumeUserInventoryItem UPDATE: {}\n", mysql_error(_connection)));
            return false;
        }
        outDeleted = false;
        serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] ItemID {} (slotID {}) count decremented by {} (remaining: {}) for userID {}\n",
            itemID, outSlotID, count, currentCount - count, userID));
    }

    return true;
}

bool DatabaseManager::ConsumeUserInventoryItemBySlot(unsigned long userID, unsigned short slotID, unsigned short count, bool& outDeleted) {
    outDeleted = false;
    if (_connection == NULL) {
        return false;
    }

    string query = format("SELECT count, itemID FROM user_inventory WHERE userID = {} AND slotID = {} LIMIT 1;", userID, slotID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ConsumeUserInventoryItemBySlot SELECT: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return false;
    }

    unsigned short currentCount = (unsigned short)atoi(row[0]);
    unsigned short itemID = (unsigned short)atoi(row[1]);
    mysql_free_result(res);

    if (currentCount <= count) {
        // Completely consumed: delete the row
        string delQuery = format("DELETE FROM user_inventory WHERE userID = {} AND slotID = {};", userID, slotID);
        if (mysql_query(_connection, delQuery.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ConsumeUserInventoryItemBySlot DELETE: {}\n", mysql_error(_connection)));
            return false;
        }
        outDeleted = true;
        serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] SlotID {} (itemID {}) completely consumed and deleted for userID {}\n", slotID, itemID, userID));
    }
    else {
        // Partially consumed: decrement count
        string updateQuery = format("UPDATE user_inventory SET count = count - {} WHERE userID = {} AND slotID = {};", count, userID, slotID);
        if (mysql_query(_connection, updateQuery.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ConsumeUserInventoryItemBySlot UPDATE: {}\n", mysql_error(_connection)));
            return false;
        }
        outDeleted = false;
        serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] SlotID {} (itemID {}) count decremented by {} (remaining: {}) for userID {}\n",
            slotID, itemID, count, currentCount - count, userID));
    }

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

bool DatabaseManager::UpdateUserInventoryItemEnhance(unsigned long userID, unsigned short slotID, unsigned long enhanceBits) {
    if (_connection == NULL) {
        return false;
    }

    string query = format("UPDATE user_inventory SET enhanceBits = {} WHERE userID = {} AND slotID = {};", enhanceBits, userID, slotID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on UpdateUserInventoryItemEnhance: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Successfully updated slotID {} enhanceBits ({:#x}) for userID {}\n", slotID, enhanceBits, userID));
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

bool DatabaseManager::DeductUserFunds(unsigned long userID, bool isPoints, unsigned long long amount) {
    if (_connection == NULL) {
        return false;
    }

    if (amount == 0) {
        return true;
    }

    string query;
    if (isPoints) {
        query = format("UPDATE user_characters SET points = points - {} WHERE userID = {} AND points >= {};",
            amount, userID, amount);
    }
    else {
        query = format("UPDATE user_characters SET cash = cash - {}, unk400 = unk400 - {} WHERE userID = {} AND cash >= {};",
            amount, amount, userID, amount);
    }

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on DeductUserFunds: {}\n", mysql_error(_connection)));
        return false;
    }

    my_ulonglong affected = mysql_affected_rows(_connection);
    if (affected == 0) {
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Deducted {} {} for userID {}\n",
        amount, isPoints ? "points" : "cash", userID));
    return true;
}

bool DatabaseManager::AddUserFunds(unsigned long userID, bool isPoints, unsigned long long amount) {
    if (_connection == NULL) {
        return false;
    }

    if (amount == 0) {
        return true;
    }

    string query;
    if (isPoints) {
        query = format("UPDATE user_characters SET points = points + {} WHERE userID = {};",
            amount, userID);
    }
    else {
        query = format("UPDATE user_characters SET cash = cash + {}, unk400 = unk400 + {} WHERE userID = {};",
            amount, amount, userID);
    }

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserFunds: {}\n", mysql_error(_connection)));
        return false;
    }

    my_ulonglong affected = mysql_affected_rows(_connection);
    if (affected == 0) {
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Added {} {} for userID {}\n",
        amount, isPoints ? "points" : "cash", userID));
    return true;
}

bool DatabaseManager::EnsureBingoTables() {
    if (_connection == NULL) {
        return false;
    }

    const char* queries[] = {
        "CREATE TABLE IF NOT EXISTS user_bingo ("
        "    userID INT UNSIGNED NOT NULL PRIMARY KEY,"
        "    status INT NOT NULL DEFAULT 0,"
        "    canPlay TINYINT(1) NOT NULL DEFAULT 1,"
        "    FOREIGN KEY (userID) REFERENCES user_characters(userID) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;",

        "CREATE TABLE IF NOT EXISTS user_bingo_slots ("
        "    userID INT UNSIGNED NOT NULL,"
        "    slotIdx TINYINT UNSIGNED NOT NULL,"
        "    number TINYINT UNSIGNED NOT NULL,"
        "    opened TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "    PRIMARY KEY (userID, slotIdx),"
        "    FOREIGN KEY (userID) REFERENCES user_characters(userID) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;",

        "CREATE TABLE IF NOT EXISTS user_bingo_prizes ("
        "    userID INT UNSIGNED NOT NULL,"
        "    lineIdx TINYINT UNSIGNED NOT NULL,"
        "    itemID INT UNSIGNED NOT NULL,"
        "    count SMALLINT UNSIGNED NOT NULL DEFAULT 1,"
        "    duration INT UNSIGNED NOT NULL DEFAULT 0,"
        "    opened TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "    PRIMARY KEY (userID, lineIdx),"
        "    FOREIGN KEY (userID) REFERENCES user_characters(userID) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;"
    };

    for (const char* q : queries) {
        if (mysql_query(_connection, q)) {
            serverConsole.Print(PrefixType::Warn, format("[ DatabaseManager ] EnsureBingoTables warning: {}\n", mysql_error(_connection)));
        }
    }

    return true;
}

bool DatabaseManager::GetUserBingoBoard(unsigned long userID, BingoBoard& outBoard) {
    if (_connection == NULL) {
        return false;
    }

    string query = format("SELECT status, canPlay FROM user_bingo WHERE userID = {};", userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserBingoBoard: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(_connection);
    if (!result) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (!row) {
        mysql_free_result(result);
        return false;
    }

    outBoard.userID = userID;
    outBoard.status = row[0] ? atoi(row[0]) : 0;
    outBoard.canPlay = row[1] ? (atoi(row[1]) != 0) : true;
    mysql_free_result(result);

    // Load 25 slots
    outBoard.slots.clear();
    string slotQuery = format("SELECT slotIdx, number, opened FROM user_bingo_slots WHERE userID = {} ORDER BY slotIdx ASC;", userID);
    if (mysql_query(_connection, slotQuery.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserBingoSlots: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* slotResult = mysql_store_result(_connection);
    if (slotResult) {
        while ((row = mysql_fetch_row(slotResult)) != NULL) {
            BingoSlot slot;
            slot.slotIdx = (unsigned char)atoi(row[0]);
            slot.number = (unsigned char)atoi(row[1]);
            slot.opened = (unsigned char)atoi(row[2]);
            outBoard.slots.push_back(slot);
        }
        mysql_free_result(slotResult);
    }

    if (outBoard.slots.size() != 25) {
        return false;
    }

    // Load 12 prizes
    outBoard.prizes.clear();
    string prizeQuery = format("SELECT lineIdx, itemID, count, duration, opened FROM user_bingo_prizes WHERE userID = {} ORDER BY lineIdx ASC;", userID);
    if (mysql_query(_connection, prizeQuery.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserBingoPrizes: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* prizeResult = mysql_store_result(_connection);
    if (prizeResult) {
        while ((row = mysql_fetch_row(prizeResult)) != NULL) {
            BingoPrize prize;
            prize.lineIdx = (unsigned char)atoi(row[0]);
            prize.itemID = (unsigned short)atoi(row[1]);
            prize.count = (unsigned char)atoi(row[2]);
            prize.duration = (unsigned char)atoi(row[3]);
            prize.opened = (unsigned char)atoi(row[4]);
            outBoard.prizes.push_back(prize);
        }
        mysql_free_result(prizeResult);
    }

    if (outBoard.prizes.size() != 12) {
        return false;
    }

    return true;
}

bool DatabaseManager::SaveUserBingoBoard(const BingoBoard& board) {
    if (_connection == NULL) {
        return false;
    }

    // 1. Insert/Update user_bingo
    string q1 = format("INSERT INTO user_bingo (userID, status, canPlay) VALUES ({}, {}, {}) "
        "ON DUPLICATE KEY UPDATE status = VALUES(status), canPlay = VALUES(canPlay);",
        board.userID, board.status, board.canPlay ? 1 : 0);
    if (mysql_query(_connection, q1.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] SaveUserBingoBoard error: {}\n", mysql_error(_connection)));
        return false;
    }

    // 2. Insert/Update slots
    for (const auto& slot : board.slots) {
        string q2 = format("INSERT INTO user_bingo_slots (userID, slotIdx, number, opened) VALUES ({}, {}, {}, {}) "
            "ON DUPLICATE KEY UPDATE number = VALUES(number), opened = VALUES(opened);",
            board.userID, (int)slot.slotIdx, (int)slot.number, (int)slot.opened);
        if (mysql_query(_connection, q2.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] SaveUserBingoSlot error: {}\n", mysql_error(_connection)));
        }
    }

    // 3. Insert/Update prizes
    for (const auto& prize : board.prizes) {
        string q3 = format("INSERT INTO user_bingo_prizes (userID, lineIdx, itemID, count, duration, opened) VALUES ({}, {}, {}, {}, {}, {}) "
            "ON DUPLICATE KEY UPDATE itemID = VALUES(itemID), count = VALUES(count), duration = VALUES(duration), opened = VALUES(opened);",
            board.userID, (int)prize.lineIdx, (int)prize.itemID, (int)prize.count, (int)prize.duration, (int)prize.opened);
        if (mysql_query(_connection, q3.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] SaveUserBingoPrize error: {}\n", mysql_error(_connection)));
        }
    }

    return true;
}

bool DatabaseManager::UpdateUserBingoSlot(unsigned long userID, unsigned char slotIdx, unsigned char opened) {
    if (_connection == NULL) {
        return false;
    }
    string q = format("UPDATE user_bingo_slots SET opened = {} WHERE userID = {} AND slotIdx = {};",
        (int)opened, userID, (int)slotIdx);
    return mysql_query(_connection, q.c_str()) == 0;
}

bool DatabaseManager::UpdateUserBingoPrize(unsigned long userID, unsigned char lineIdx, unsigned char opened) {
    if (_connection == NULL) {
        return false;
    }
    string q = format("UPDATE user_bingo_prizes SET opened = {} WHERE userID = {} AND lineIdx = {};",
        (int)opened, userID, (int)lineIdx);
    return mysql_query(_connection, q.c_str()) == 0;
}

bool DatabaseManager::ResetUserBingoBoard(unsigned long userID, const BingoBoard& newBoard) {
    if (_connection == NULL) {
        return false;
    }
    string d1 = format("DELETE FROM user_bingo_slots WHERE userID = {};", userID);
    mysql_query(_connection, d1.c_str());
    string d2 = format("DELETE FROM user_bingo_prizes WHERE userID = {};", userID);
    mysql_query(_connection, d2.c_str());
    return SaveUserBingoBoard(newBoard);
}

bool DatabaseManager::EnsureAttendanceTables() {
    if (_connection == NULL) {
        return false;
    }

    const char* query =
        "CREATE TABLE IF NOT EXISTS user_attendance ("
        "    userID INT UNSIGNED NOT NULL PRIMARY KEY,"
        "    consecutiveDays TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "    totalCheckIns INT UNSIGNED NOT NULL DEFAULT 0,"
        "    lastCheckInDate DATE DEFAULT NULL,"
        "    createdTime DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "    updatedTime DATETIME NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,"
        "    FOREIGN KEY (userID) REFERENCES user_characters(userID) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    if (mysql_query(_connection, query)) {
        serverConsole.Print(PrefixType::Warn, format("[ DatabaseManager ] EnsureAttendanceTables warning: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::GetUserAttendance(unsigned long userID, AttendanceRecord& outRecord) {
    if (_connection == NULL) {
        return false;
    }

    outRecord.userID = userID;
    outRecord.consecutiveDays = 0;
    outRecord.totalCheckIns = 0;
    outRecord.lastCheckInDate = "";
    outRecord.canCheckIn = true;
    outRecord.daysSinceLast = 999;

    string query = format(
        "SELECT consecutiveDays, totalCheckIns, "
        "       COALESCE(DATE_FORMAT(lastCheckInDate, '%Y-%m-%d'), ''), "
        "       IF(lastCheckInDate IS NULL, 1, IF(lastCheckInDate < CURDATE(), 1, 0)) AS canCheckIn, "
        "       IF(lastCheckInDate IS NULL, 999, DATEDIFF(CURDATE(), lastCheckInDate)) AS daysSinceLast "
        "FROM user_attendance WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserAttendance: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* result = mysql_store_result(_connection);
    if (!result) {
        return false;
    }

    MYSQL_ROW row = mysql_fetch_row(result);
    if (row) {
        outRecord.consecutiveDays = (unsigned char)(row[0] ? atoi(row[0]) : 0);
        outRecord.totalCheckIns = (unsigned int)(row[1] ? strtoul(row[1], NULL, 10) : 0);
        outRecord.lastCheckInDate = row[2] ? row[2] : "";
        outRecord.canCheckIn = (row[3] ? atoi(row[3]) : 1) != 0;
        outRecord.daysSinceLast = row[4] ? atoi(row[4]) : 999;
    } else {
        mysql_free_result(result);
        string insertQ = format(
            "INSERT INTO user_attendance (userID, consecutiveDays, totalCheckIns, lastCheckInDate) "
            "VALUES ({}, 0, 0, NULL) "
            "ON DUPLICATE KEY UPDATE userID=userID;", userID);
        mysql_query(_connection, insertQ.c_str());
        return true;
    }

    mysql_free_result(result);
    return true;
}

bool DatabaseManager::RecordUserAttendance(unsigned long userID, unsigned char newConsecutiveDays) {
    if (_connection == NULL) {
        return false;
    }

    string query = format(
        "INSERT INTO user_attendance (userID, consecutiveDays, totalCheckIns, lastCheckInDate) "
        "VALUES ({0}, {1}, 1, CURDATE()) "
        "ON DUPLICATE KEY UPDATE "
        "    consecutiveDays = IF(lastCheckInDate IS NULL OR lastCheckInDate < CURDATE(), {1}, consecutiveDays), "
        "    totalCheckIns = IF(lastCheckInDate IS NULL OR lastCheckInDate < CURDATE(), totalCheckIns + 1, totalCheckIns), "
        "    lastCheckInDate = IF(lastCheckInDate IS NULL OR lastCheckInDate < CURDATE(), CURDATE(), lastCheckInDate);",
        userID, (int)newConsecutiveDays);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RecordUserAttendance: {}\n", mysql_error(_connection)));
        return false;
    }

    my_ulonglong affected = mysql_affected_rows(_connection);
    if (affected == 0) {
        return false;
    }

    return true;
}

bool DatabaseManager::ResetUserAttendance(unsigned long userID, unsigned char targetStreak) {
    if (_connection == NULL) {
        return false;
    }

    string query;
    if (targetStreak == 255) {
        // Revert today's check-in so today can be claimed again
        query = format(
            "UPDATE user_attendance "
            "SET consecutiveDays = IF(lastCheckInDate = CURDATE() AND consecutiveDays > 0, consecutiveDays - 1, consecutiveDays), "
            "    totalCheckIns = IF(lastCheckInDate = CURDATE() AND totalCheckIns > 0, totalCheckIns - 1, totalCheckIns), "
            "    lastCheckInDate = DATE_SUB(CURDATE(), INTERVAL 1 DAY) "
            "WHERE userID = {};", userID);
    }
    else {
        // Set exact streak (0 = Day 1 next, 1 = Day 2 next, etc.) and unlock
        query = format(
            "UPDATE user_attendance "
            "SET consecutiveDays = {}, "
            "    lastCheckInDate = DATE_SUB(CURDATE(), INTERVAL 1 DAY) "
            "WHERE userID = {};", (int)targetStreak, userID);
    }

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetUserAttendance: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::AdvanceUserAttendanceNextDay(unsigned long userID) {
    if (_connection == NULL) {
        return false;
    }

    AttendanceRecord record;
    if (!GetUserAttendance(userID, record)) {
        return false;
    }

    unsigned char newStreak = record.consecutiveDays;
    if (record.canCheckIn) {
        newStreak = (record.consecutiveDays % 7) + 1;
    }

    string query = format(
        "UPDATE user_attendance "
        "SET consecutiveDays = {}, "
        "    lastCheckInDate = DATE_SUB(CURDATE(), INTERVAL 1 DAY) "
        "WHERE userID = {};", (int)newStreak, userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AdvanceUserAttendanceNextDay: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::EnsureTitleTables() {
    if (_connection == NULL) {
        return false;
    }

    // 1. Ensure user_characters columns (ignore error if column already exists)
    const char* alterCols[] = {
        "ALTER TABLE `user_characters` ADD COLUMN `prefixID` SMALLINT UNSIGNED NOT NULL DEFAULT 0;",
        "ALTER TABLE `user_characters` ADD COLUMN `honorPoints` SMALLINT UNSIGNED NOT NULL DEFAULT 0;",
        "ALTER TABLE `user_characters` ADD COLUMN `badge0` TINYINT UNSIGNED NOT NULL DEFAULT 0;",
        "ALTER TABLE `user_characters` ADD COLUMN `badge1` TINYINT UNSIGNED NOT NULL DEFAULT 0;",
        "ALTER TABLE `user_characters` ADD COLUMN `badge2` TINYINT UNSIGNED NOT NULL DEFAULT 0;",
        "ALTER TABLE `user_characters` ADD COLUMN `badge3` TINYINT UNSIGNED NOT NULL DEFAULT 0;",
        "ALTER TABLE `user_characters` ADD COLUMN `badge4` TINYINT UNSIGNED NOT NULL DEFAULT 0;"
    };
    for (const char* sql : alterCols) {
        mysql_query(_connection, sql);
    }

    // 2. Ensure user_titles table
    const char* createSql =
        "CREATE TABLE IF NOT EXISTS `user_titles` ("
        "  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
        "  `userID` INT UNSIGNED NOT NULL,"
        "  `titleID` SMALLINT UNSIGNED NOT NULL,"
        "  `unlockTime` TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,"
        "  PRIMARY KEY (`id`),"
        "  UNIQUE KEY `uk_user_title` (`userID`, `titleID`),"
        "  KEY `idx_userID` (`userID`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    if (mysql_query(_connection, createSql)) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on EnsureTitleTables: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::GetUserTitles(unsigned long userID, vector<unsigned short>& outTitleIDs) {
    outTitleIDs.clear();
    if (_connection == NULL) return false;

    string query = format("SELECT titleID FROM user_titles WHERE userID = {} ORDER BY titleID ASC;", userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserTitles: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (!res) return false;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        if (row[0]) {
            outTitleIDs.push_back((unsigned short)atoi(row[0]));
        }
    }
    mysql_free_result(res);
    return true;
}

bool DatabaseManager::AddUserTitle(unsigned long userID, unsigned short titleID) {
    if (_connection == NULL || titleID == 0) return false;

    string query = format("INSERT IGNORE INTO user_titles (userID, titleID) VALUES ({}, {});", userID, titleID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserTitle: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::HasUserTitle(unsigned long userID, unsigned short titleID) {
    if (_connection == NULL || titleID == 0) return false;

    string query = format("SELECT 1 FROM user_titles WHERE userID = {} AND titleID = {} LIMIT 1;", userID, titleID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on HasUserTitle: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (!res) return false;

    bool exists = (mysql_num_rows(res) > 0);
    mysql_free_result(res);
    return exists;
}

bool DatabaseManager::SetUserPrefixID(unsigned long userID, unsigned short prefixID) {
    if (_connection == NULL) return false;

    string query = format("UPDATE user_characters SET prefixID = {} WHERE userID = {};", prefixID, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserPrefixID: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::SetUserHonorPoints(unsigned long userID, unsigned short honorPoints) {
    if (_connection == NULL) return false;

    string query = format("UPDATE user_characters SET honorPoints = {} WHERE userID = {};", honorPoints, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserHonorPoints: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::AddUserHonorPoints(unsigned long userID, unsigned short points) {
    if (_connection == NULL || points == 0) return false;

    string query = format("UPDATE user_characters SET honorPoints = honorPoints + {} WHERE userID = {};", points, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserHonorPoints: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::SetUserBadges(unsigned long userID, const unsigned char badges[5]) {
    if (_connection == NULL) return false;

    string query = format(
        "UPDATE user_characters SET badge0 = {}, badge1 = {}, badge2 = {}, badge3 = {}, badge4 = {} WHERE userID = {};",
        (int)badges[0], (int)badges[1], (int)badges[2], (int)badges[3], (int)badges[4], userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserBadges: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::SetUserBadgeSlot(unsigned long userID, unsigned char slot, unsigned char titleID) {
    if (_connection == NULL || slot >= 5) return false;

    string query = format("UPDATE user_characters SET badge{} = {} WHERE userID = {};", (int)slot, (int)titleID, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserBadgeSlot: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}



