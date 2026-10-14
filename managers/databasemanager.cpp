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
        mysql_query(_connection, "ALTER TABLE user_characters MODIFY COLUMN unk4_1 VARBINARY(16) DEFAULT ('');");
        mysql_query(_connection, "ALTER TABLE user_characters MODIFY COLUMN clanName VARBINARY(32) DEFAULT ('');");
        mysql_query(_connection, "ALTER TABLE user_characters MODIFY COLUMN unk200_5 VARBINARY(64) DEFAULT ('');");
        mysql_query(_connection, "ALTER TABLE user_characters ADD UNIQUE KEY uk_nickName (nickName);");

        EnsureBingoTables();
        EnsureAttendanceTables();
        EnsureTitleTables();
        EnsureQuestTables();
        EnsureGiftTables();
        EnsureBookmarkTables();
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
        loginResult.reply = Packet_ReplyType::InvalidName;
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

int DatabaseManager::RegisterUser(const string& userName, const string& password, string& outErrorMsg) {
    if (_connection == NULL) {
        outErrorMsg = "伺服器資料庫未連線！";
        return 3;
    }

    if (userName.length() < 3 || userName.length() > 16) {
        outErrorMsg = "帳號長度需介於 3 到 16 個字元！";
        return 2;
    }

    if (password.length() < 4 || password.length() > 20) {
        outErrorMsg = "密碼長度需介於 4 到 20 個字元！";
        return 2;
    }

    for (char c : userName) {
        if (!isalnum((unsigned char)c) && c != '_' && c != '-') {
            outErrorMsg = "帳號僅能使用英文字母、數字與底線！";
            return 2;
        }
    }

    char escapedUser[64] = {};
    mysql_real_escape_string(_connection, escapedUser, userName.c_str(), (unsigned long)userName.size());

    string checkQuery = format("SELECT userID FROM users WHERE userName = '{}' LIMIT 1;", escapedUser);
    if (mysql_query(_connection, checkQuery.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RegisterUser check: {}\n", mysql_error(_connection)));
        outErrorMsg = "伺服器資料庫查詢失敗！";
        return 3;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res) {
        if (mysql_num_rows(res) > 0) {
            mysql_free_result(res);
            outErrorMsg = "此帳號已被註冊，請更換其他帳號！";
            return 1;
        }
        mysql_free_result(res);
    }

    char salt[BCRYPT_HASHSIZE] = {};
    char hash[BCRYPT_HASHSIZE] = {};
    if (bcrypt_gensalt(10, salt) != 0 || bcrypt_hashpw(password.c_str(), salt, hash) != 0) {
        serverConsole.Print(PrefixType::Error, "[ DatabaseManager ] Failed to hash password with bcrypt!\n");
        outErrorMsg = "密碼加密失敗！";
        return 3;
    }

    char escapedHash[128] = {};
    mysql_real_escape_string(_connection, escapedHash, hash, (unsigned long)strlen(hash));

    string insertQuery = format("INSERT INTO users (userName, password, registerTime) VALUES ('{}', '{}', NOW());", escapedUser, escapedHash);
    if (mysql_query(_connection, insertQuery.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RegisterUser insert: {}\n", mysql_error(_connection)));
        outErrorMsg = "建立帳號資料失敗！";
        return 3;
    }

    unsigned long newUserID = (unsigned long)mysql_insert_id(_connection);
    CreateUserItemsets(newUserID);
    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Successfully registered user '{}' (userID: {})\n", userName, newUserID));
    return 0;
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

bool DatabaseManager::GetUserByNickName(const string& nickName, unsigned long& outUserID, unsigned char& outLevel) {
    if (_connection == NULL) {
        return false;
    }

    char escaped[64] = {};
    mysql_real_escape_string(_connection, escaped, nickName.c_str(), (unsigned long)nickName.size());

    string query = format("SELECT userID, level FROM user_characters WHERE nickName = '{}' LIMIT 1;", escaped);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserByNickName: {}\n", mysql_error(_connection)));
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

    outUserID = strtoul(row[0], nullptr, 10);
    outLevel = (unsigned char)atoi(row[1]);
    mysql_free_result(res);
    return true;
}

bool DatabaseManager::UpdateUserNickName(unsigned long userID, const string& nickName) {
    if (_connection == NULL) {
        return false;
    }

    char escaped[64] = {};
    mysql_real_escape_string(_connection, escaped, nickName.c_str(), (unsigned long)nickName.size());

    string query = format("UPDATE user_characters SET unk4_1 = IF(unk4_1 = nickName OR unk4_1 = '' OR unk4_1 IS NULL, '{}', unk4_1), nickName = '{}' WHERE userID = {};", escaped, escaped, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on UpdateUserNickName: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Updated nickName for userID {} to '{}'\n", userID, Cp950ToUtf8(nickName)));
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

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Updated camoNickName (unk4_1) for userID {} to '{}'\n", userID, Cp950ToUtf8(camoNickName)));
    return true;
}

bool DatabaseManager::UpdateUserLocation(unsigned long userID, unsigned short city, unsigned short county, unsigned short neighborhood) {
    if (_connection == NULL) {
        return false;
    }

    string query = format("UPDATE user_characters SET city = {}, county = {}, neighborhood = {} WHERE userID = {};",
        city, county, neighborhood, userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on UpdateUserLocation: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Updated location for userID {} to city={}, county={}, neighborhood={}\n",
        userID, city, county, neighborhood));
    return true;
}

bool DatabaseManager::ResetUserWinLose(unsigned long userID) {
    if (_connection == NULL) {
        return false;
    }
    string query = format("UPDATE user_characters SET battles = 0, wins = 0 WHERE userID = {};", userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetUserWinLose: {}\n", mysql_error(_connection)));
        return false;
    }
    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Reset win/lose record for userID {}\n", userID));
    return true;
}

bool DatabaseManager::ResetUserKillDeath(unsigned long userID) {
    if (_connection == NULL) {
        return false;
    }
    string query = format("UPDATE user_characters SET frags = 0, deaths = 0 WHERE userID = {};", userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetUserKillDeath: {}\n", mysql_error(_connection)));
        return false;
    }
    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Reset kill/death record for userID {}\n", userID));
    return true;
}

char DatabaseManager::CreateUserCharacter(unsigned long userID, const string& nickName) {
    char escaped[64] = {};
    mysql_real_escape_string(_connection, escaped, nickName.c_str(), (unsigned long)nickName.size());

    string query = format("INSERT INTO user_characters (userID, nickName, unk4_1, points, cash, unk400) VALUES ({}, '{}', '{}', 100000, 100000, 100000);", userID, escaped, escaped);

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

    CreateUserItemsets(userID);

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
            info += " IF(unk4_1 = '' OR unk4_1 IS NULL OR unk4_1 = nickName OR NOT EXISTS("
                    "SELECT 1 FROM user_inventory WHERE user_inventory.userID = user_characters.userID "
                    "AND user_inventory.itemID = 68 "
                    "AND (user_inventory.expirationTime = 0 OR user_inventory.expirationTime > UNIX_TIMESTAMP())"
                    "), nickName, unk4_1), unk4_2, unk4_3, unk4_4,";
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
        if ((flag & USERCHARACTER_FLAG_HONORPOINTS) || (flag & USERCHARACTER_FLAG_PREFIXID) || (flag & USERCHARACTER_FLAG_PREFIXID_EX)) {
            info += " prefixID, honorPoints,";
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
        if ((flag & USERCHARACTER_FLAG_HONORPOINTS) || (flag & USERCHARACTER_FLAG_PREFIXID) || (flag & USERCHARACTER_FLAG_PREFIXID_EX)) {
            userCharacter.prefixID = atoi(row[index++]);
            userCharacter.honorPoints = atoi(row[index++]);
        }
    }

    mysql_free_result(res);

    if (flag & USERCHARACTER_FLAG_UNK4) {
        // If the camouflage nickname (item 68) has expired or is not owned, clean up stale unk4_1 in user_characters table
        string camoCleanQuery = format(
            "UPDATE user_characters SET unk4_1 = '' "
            "WHERE userID = {} AND unk4_1 != '' AND NOT EXISTS ("
            "SELECT 1 FROM user_inventory WHERE user_inventory.userID = user_characters.userID "
            "AND user_inventory.itemID = 68 "
            "AND (user_inventory.expirationTime = 0 OR user_inventory.expirationTime > UNIX_TIMESTAMP())"
            ");", userID);
        mysql_query(_connection, camoCleanQuery.c_str());
    }

    if ((flag & USERCHARACTER_FLAG_LEVEL) && userCharacter.level == 0) {
        if (!(flag & USERCHARACTER_FLAG_EXP)) {
            string expQuery = format("SELECT exp FROM user_characters WHERE userID = {};", userID);
            if (!mysql_query(_connection, expQuery.c_str())) {
                MYSQL_RES* expRes = mysql_store_result(_connection);
                if (expRes) {
                    MYSQL_ROW expRow = mysql_fetch_row(expRes);
                    if (expRow && expRow[0]) {
                        userCharacter.exp = atoll(expRow[0]);
                    }
                    mysql_free_result(expRes);
                }
            }
        }
        userCharacter.level = max((unsigned char)1, User::CalculateLevel(userCharacter.exp));
        string healQuery = format("UPDATE user_characters SET level = {} WHERE userID = {};", (int)userCharacter.level, userID);
        mysql_query(_connection, healQuery.c_str());
        serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Auto-healed level 0 to {} for userID {}\n", (int)userCharacter.level, userID));
    }

    if ((flag & USERCHARACTER_FLAG_HONORPOINTS) || (flag & USERCHARACTER_FLAG_CBOXCOUNT)) {
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
            int count = 1;
            if (itemID == 166 || itemID == 173) {
                count = 10;
                inUse = 0;
            } else if (kTerroristCharacters.count(itemID)) {
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
            query += format(" ({}, {}, {}, {}, 1, {}, {}, 0, 0),", userID, slotID++, itemID, count, inUse, now);
        }
        query[query.size() - 1] = ';';

        if (mysql_query(_connection, query.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserInventory: {}\n", mysql_error(_connection)));
            return false;
        }
    }

    return true;
}

bool DatabaseManager::CreateUserItemsets(unsigned long userID) {
    if (_connection == NULL) {
        return false;
    }

    string query = format("SELECT 1 FROM user_itemsets WHERE userID = {} LIMIT 1;", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserItemsets check: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_use_result(_connection);

    if (mysql_fetch_row(res)) {
        mysql_free_result(res);
        return true;
    }
    mysql_free_result(res);

    query = format("INSERT INTO user_itemsets (userID, slotID, itemID) VALUES ({}, 0, 0), ({}, 1, 0)", userID, userID);

    for (int i = 0; i < 3; ++i) {
        int baseSlot = (i + 1) * 10;
        const auto& set = (i < (int)serverConfig.defaultItemSets.size()) ? serverConfig.defaultItemSets[i] : DefaultLoadout{};
        query += format(", ({}, {}, {}), ({}, {}, {}), ({}, {}, {}), ({}, {}, {})",
            userID, baseSlot + 0, set.primary,
            userID, baseSlot + 1, set.secondary,
            userID, baseSlot + 2, set.melee,
            userID, baseSlot + 3, set.hegrenade);
    }
    query += ";";

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on CreateUserItemsets: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Successfully initialized default itemsets for userID: {}\n", userID));
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
    bool hasRows = false;

    while ((row = mysql_fetch_row(res)))
    {
        hasRows = true;
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

    if (!hasRows) {
        CreateUserItemsets(userID);
        for (int i = 0; i < 3; ++i) {
            const auto& set = (i < (int)serverConfig.defaultItemSets.size()) ? serverConfig.defaultItemSets[i] : DefaultLoadout{};
            itemSet.weapons[i][0] = set.primary;
            itemSet.weapons[i][1] = set.secondary;
            itemSet.weapons[i][2] = set.melee;
            itemSet.weapons[i][3] = set.hegrenade;
        }
    }

    return itemSet;
}

bool DatabaseManager::SaveUserBookMark(unsigned long userID, const BookMark& userBookMark) {
    if (_connection == NULL) {
        return false;
    }

    char* bookMarkName = new char[userBookMark.name.size() * 2 + 1];
    mysql_real_escape_string(_connection, bookMarkName, userBookMark.name.c_str(), (unsigned long)userBookMark.name.size());

    string query = format(
        "INSERT INTO user_bookmarks (userID, slotID, name, primaryItemID, primaryAmmo, secondaryItemID, secondaryAmmo, flashbang, hegrenade, smokegrenade, defusekit, nightvision, kevlar, unk1) "
        "VALUES ({}, {}, '{}', {}, {}, {}, {}, {}, {}, {}, {}, {}, {}, {}) "
        "ON DUPLICATE KEY UPDATE name = VALUES(name), primaryItemID = VALUES(primaryItemID), primaryAmmo = VALUES(primaryAmmo), "
        "secondaryItemID = VALUES(secondaryItemID), secondaryAmmo = VALUES(secondaryAmmo), flashbang = VALUES(flashbang), "
        "hegrenade = VALUES(hegrenade), smokegrenade = VALUES(smokegrenade), defusekit = VALUES(defusekit), "
        "nightvision = VALUES(nightvision), kevlar = VALUES(kevlar), unk1 = VALUES(unk1);",
        userID, userBookMark.slotID, bookMarkName,
        userBookMark.primaryItemID, userBookMark.primaryAmmo,
        userBookMark.secondaryItemID, userBookMark.secondaryAmmo,
        userBookMark.flashbang, userBookMark.hegrenade,
        userBookMark.smokegrenade, userBookMark.defusekit,
        userBookMark.nightvision, userBookMark.kevlar, userBookMark.unk1);
    delete[] bookMarkName;

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserBookMark: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

const vector<BookMark> DatabaseManager::GetUserBookMarks(unsigned long userID) {
    if (_connection == NULL) {
        return {};
    }

    string query = format("SELECT slotID, name, primaryItemID, primaryAmmo, secondaryItemID, secondaryAmmo, flashbang, hegrenade, smokegrenade, defusekit, nightvision, kevlar, unk1 FROM user_bookmarks WHERE userID = {} ORDER BY slotID ASC;", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserBookMarks: {}\n", mysql_error(_connection)));
        return {};
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return {};
    }

    MYSQL_ROW row = NULL;
    vector<BookMark> userBookMarks;

    while ((row = mysql_fetch_row(res)))
    {
        BookMark bookMark;
        bookMark.slotID = (unsigned char)atoi(row[0]);
        bookMark.name = row[1] ? row[1] : "";
        bookMark.primaryItemID = (unsigned short)atoi(row[2]);
        bookMark.primaryAmmo = (unsigned short)atoi(row[3]);
        bookMark.secondaryItemID = (unsigned short)atoi(row[4]);
        bookMark.secondaryAmmo = (unsigned short)atoi(row[5]);
        bookMark.flashbang = (unsigned short)atoi(row[6]);
        bookMark.hegrenade = (unsigned short)atoi(row[7]);
        bookMark.smokegrenade = (unsigned short)atoi(row[8]);
        bookMark.defusekit = (unsigned short)atoi(row[9]);
        bookMark.nightvision = (unsigned short)atoi(row[10]);
        bookMark.kevlar = (unsigned short)atoi(row[11]);
        bookMark.unk1 = (unsigned short)atoi(row[12]);

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

bool DatabaseManager::DeleteUserInventoryItem(unsigned long userID, unsigned short itemID, unsigned short count, vector<unsigned short>& outDeletedSlots, unsigned short& outActualDeletedCount) {
    outDeletedSlots.clear();
    outActualDeletedCount = 0;
    if (_connection == NULL) {
        return false;
    }

    string query = format("SELECT slotID, count FROM user_inventory WHERE userID = {} AND itemID = {} ORDER BY slotID ASC;", userID, itemID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on DeleteUserInventoryItem SELECT: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return false;
    }

    struct SlotInfo {
        unsigned short slotID;
        unsigned short count;
    };
    vector<SlotInfo> slots;
    MYSQL_ROW row = NULL;
    while ((row = mysql_fetch_row(res))) {
        slots.push_back({ (unsigned short)atoi(row[0]), (unsigned short)atoi(row[1]) });
    }
    mysql_free_result(res);

    if (slots.empty()) {
        return false;
    }

    // If count == 0, delete ALL instances of this item
    if (count == 0) {
        string delQuery = format("DELETE FROM user_inventory WHERE userID = {} AND itemID = {};", userID, itemID);
        if (mysql_query(_connection, delQuery.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on DeleteUserInventoryItem DELETE ALL: {}\n", mysql_error(_connection)));
            return false;
        }

        for (const auto& s : slots) {
            outDeletedSlots.push_back(s.slotID);
            outActualDeletedCount += s.count;
        }

        serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Deleted all items ID {} (total {} across {} slots) for userID {}\n",
            itemID, outActualDeletedCount, outDeletedSlots.size(), userID));
        return true;
    }

    // Delete specific count
    unsigned short remainingToDelete = count;
    vector<unsigned short> slotsToDelete;

    for (const auto& s : slots) {
        if (remainingToDelete == 0) {
            break;
        }

        if (remainingToDelete >= s.count) {
            slotsToDelete.push_back(s.slotID);
            outDeletedSlots.push_back(s.slotID);
            outActualDeletedCount += s.count;
            remainingToDelete -= s.count;
        }
        else {
            string updQuery = format("UPDATE user_inventory SET count = count - {} WHERE userID = {} AND slotID = {};", remainingToDelete, userID, s.slotID);
            if (mysql_query(_connection, updQuery.c_str())) {
                serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on DeleteUserInventoryItem UPDATE: {}\n", mysql_error(_connection)));
                return false;
            }
            outActualDeletedCount += remainingToDelete;
            remainingToDelete = 0;
            break;
        }
    }

    if (!slotsToDelete.empty()) {
        string idList;
        for (size_t i = 0; i < slotsToDelete.size(); i++) {
            idList += to_string(slotsToDelete[i]);
            if (i + 1 < slotsToDelete.size()) {
                idList += ",";
            }
        }
        string delQuery = format("DELETE FROM user_inventory WHERE userID = {} AND slotID IN ({});", userID, idList);
        if (mysql_query(_connection, delQuery.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on DeleteUserInventoryItem DELETE: {}\n", mysql_error(_connection)));
            return false;
        }
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Deleted {} of itemID {} (deleted slots: {}) for userID {}\n",
        outActualDeletedCount, itemID, outDeletedSlots.size(), userID));
    return true;
}

bool DatabaseManager::RemoveExpiredInventoryItems(unsigned long userID, vector<ExpiredItemRecord>& outExpiredItems) {
    outExpiredItems.clear();
    if (_connection == NULL) {
        return false;
    }

    unsigned long now = (unsigned long)time(nullptr);
    string query = format("SELECT slotID, itemID FROM user_inventory WHERE userID = {} AND expirationTime > 0 AND expirationTime <= {};", userID, now);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RemoveExpiredInventoryItems SELECT: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return false;
    }

    MYSQL_ROW row;
    bool hadCamo = false;
    vector<unsigned short> slotsToDelete;
    vector<unsigned short> itemIDsToDelete;
    while ((row = mysql_fetch_row(res)) != NULL) {
        ExpiredItemRecord rec;
        rec.slotID = (unsigned short)atoi(row[0]);
        rec.itemID = (unsigned short)atoi(row[1]);
        outExpiredItems.push_back(rec);
        slotsToDelete.push_back(rec.slotID);
        itemIDsToDelete.push_back(rec.itemID);
        if (rec.itemID == 68) {
            hadCamo = true;
        }
    }
    mysql_free_result(res);

    if (outExpiredItems.empty()) {
        return false;
    }

    string slotListStr = "";
    for (size_t i = 0; i < slotsToDelete.size(); ++i) {
        slotListStr += to_string(slotsToDelete[i]);
        if (i + 1 < slotsToDelete.size()) {
            slotListStr += ",";
        }
    }

    string delQuery = format("DELETE FROM user_inventory WHERE userID = {} AND slotID IN ({});", userID, slotListStr);
    if (mysql_query(_connection, delQuery.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RemoveExpiredInventoryItems DELETE: {}\n", mysql_error(_connection)));
        return false;
    }

    // Clean up camouflage nickname in user_characters if Item 68 expired
    if (hadCamo) {
        string updateQuery = format("UPDATE user_characters SET unk4_1 = nickName WHERE userID = {};", userID);
        mysql_query(_connection, updateQuery.c_str());
    }

    // Clean up user_itemsets if any equipped weapon expired
    string itemListStr = "";
    for (size_t i = 0; i < itemIDsToDelete.size(); ++i) {
        itemListStr += to_string(itemIDsToDelete[i]);
        if (i + 1 < itemIDsToDelete.size()) {
            itemListStr += ",";
        }
    }
    string resetItemsetsQuery = format("UPDATE user_itemsets SET itemID = 0 WHERE userID = {} AND itemID IN ({});", userID, itemListStr);
    mysql_query(_connection, resetItemsetsQuery.c_str());

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Removed {} expired inventory item(s) for userID {}\n", outExpiredItems.size(), userID));
    return true;
}

bool DatabaseManager::SetUserInventoryItemExpirationSeconds(unsigned long userID, unsigned short itemID, unsigned int seconds) {
    if (_connection == NULL) {
        return false;
    }

    unsigned long newExp = (unsigned long)time(nullptr) + seconds;
    string query = format("UPDATE user_inventory SET expirationTime = {} WHERE userID = {} AND itemID = {} LIMIT 1;", newExp, userID, itemID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserInventoryItemExpirationSeconds: {}\n", mysql_error(_connection)));
        return false;
    }

    if (mysql_affected_rows(_connection) == 0) {
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Set itemID {} expiration to {}s from now (timestamp: {}) for userID {}\n", itemID, seconds, newExp, userID));
    return true;
}

bool DatabaseManager::SaveUserBattleResult(unsigned long userID, unsigned long long addedExp, unsigned long long addedPoints, unsigned short addedFrags, unsigned short addedDeaths, bool isWin, unsigned char newLevel, bool recordStats) {
    if (_connection == NULL) {
        return false;
    }

    string query;
    if (newLevel > 0) {
        query = format("UPDATE user_characters SET exp = exp + {}, points = points + {}, battles = battles + {}, wins = wins + {}, frags = frags + {}, deaths = deaths + {}, level = {} WHERE userID = {};",
            addedExp, addedPoints, recordStats ? 1 : 0, (recordStats && isWin) ? 1 : 0, recordStats ? addedFrags : 0, recordStats ? addedDeaths : 0, (int)newLevel, userID);
    } else {
        query = format("UPDATE user_characters SET exp = exp + {}, points = points + {}, battles = battles + {}, wins = wins + {}, frags = frags + {}, deaths = deaths + {} WHERE userID = {};",
            addedExp, addedPoints, recordStats ? 1 : 0, (recordStats && isWin) ? 1 : 0, recordStats ? addedFrags : 0, recordStats ? addedDeaths : 0, userID);
    }

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserBattleResult: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Updated battle result for userID {}: +{} exp, +{} points, recordStats: {}, +{} frags, +{} deaths, win: {}, level: {}\n",
        userID, addedExp, addedPoints, recordStats ? 1 : 0, recordStats ? addedFrags : 0, recordStats ? addedDeaths : 0, (recordStats && isWin) ? 1 : 0, (int)newLevel));

    return true;
}

bool DatabaseManager::AddUserExp(unsigned long userID, unsigned long long addedExp, unsigned char* outNewLevel) {
    if (_connection == NULL) {
        return false;
    }

    UserCharacterResult ucr = GetUserCharacter(userID, USERCHARACTER_FLAG_LEVEL | USERCHARACTER_FLAG_EXP);
    if (!ucr.result) {
        return false;
    }

    unsigned char oldLevel = ucr.userCharacter.level ? ucr.userCharacter.level : 1;
    unsigned long long newExp = ucr.userCharacter.exp + addedExp;
    unsigned char calculatedLevel = max(oldLevel, User::CalculateLevel(newExp));
    if (outNewLevel) {
        *outNewLevel = calculatedLevel;
    }

    string query = format("UPDATE user_characters SET exp = {}, level = {} WHERE userID = {};",
        newExp, (int)calculatedLevel, userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserExp: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Added {} exp for userID {}: newExp = {}, level = {}\n",
        addedExp, userID, newExp, (int)calculatedLevel));

    return true;
}

bool DatabaseManager::SetUserLevel(unsigned long userID, unsigned char newLevel) {
    if (_connection == NULL) {
        return false;
    }

    if (newLevel < 1) newLevel = 1;
    if (newLevel > 72) newLevel = 72;

    unsigned long long baseExp = User::GetExpForLevel(newLevel);

    string query = format("UPDATE user_characters SET level = {}, exp = {} WHERE userID = {};", (int)newLevel, baseExp, userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserLevel: {}\n", mysql_error(_connection)));
        return false;
    }

    serverConsole.Print(PrefixType::Info, format("[ DatabaseManager ] Set level for userID {} to {} (exp: {})\n", userID, (int)newLevel, baseExp));
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
        query = format("UPDATE user_characters SET cash = cash - {}, unk400 = cash - {} WHERE userID = {} AND cash >= {};",
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
        query = format("UPDATE user_characters SET cash = cash + {}, unk400 = cash + {} WHERE userID = {};",
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

unsigned short DatabaseManager::GetUserHonorPoints(unsigned long userID) {
    if (_connection == NULL) return 0;
    string query = format("SELECT honorPoints FROM user_characters WHERE userID = {};", userID);
    if (!mysql_query(_connection, query.c_str())) {
        MYSQL_RES* res = mysql_store_result(_connection);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            unsigned short pts = (row && row[0]) ? (unsigned short)atoi(row[0]) : 0;
            mysql_free_result(res);
            return pts;
        }
    }
    return 0;
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

bool DatabaseManager::EnsureQuestTables() {
    if (_connection == NULL) return false;

    const char* createSql =
        "CREATE TABLE IF NOT EXISTS `user_quests` ("
        "  `id` INT UNSIGNED NOT NULL AUTO_INCREMENT,"
        "  `userID` INT UNSIGNED NOT NULL,"
        "  `questID` INT UNSIGNED NOT NULL,"
        "  `state` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `count0` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `count1` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `count2` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `count3` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `count4` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `count5` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `lastUpdatedDate` DATE NOT NULL DEFAULT (CURRENT_DATE),"
        "  `isTracked` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "  PRIMARY KEY (`id`),"
        "  UNIQUE KEY `uk_user_quest` (`userID`, `questID`),"
        "  KEY `idx_userID` (`userID`)"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";

    if (mysql_query(_connection, createSql)) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on EnsureQuestTables: {}\n", mysql_error(_connection)));
        return false;
    }

    // Ensure isTracked column exists
    mysql_query(_connection, "ALTER TABLE `user_quests` ADD COLUMN `isTracked` TINYINT UNSIGNED NOT NULL DEFAULT 0;");

    const char* createStatsSql =
        "CREATE TABLE IF NOT EXISTS `user_quest_stats` ("
        "  `userID` INT UNSIGNED NOT NULL PRIMARY KEY,"
        "  `specialStreak` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `specialMaxStreak` SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `maxDailyCompleted` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `allClearCount` TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "  `lastSpecialDate` DATE NULL,"
        "  FOREIGN KEY (`userID`) REFERENCES `users` (`userID`) ON DELETE CASCADE"
        ") ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;";
    mysql_query(_connection, createStatsSql);

    return true;
}

bool DatabaseManager::GetUserQuests(unsigned long userID, map<unsigned int, UserQuestRecord>& outQuests) {
    outQuests.clear();
    if (_connection == NULL) return false;

    string query = format(
        "SELECT questID, state, count0, count1, count2, count3, count4, count5, CAST(lastUpdatedDate AS CHAR), isTracked "
        "FROM user_quests WHERE userID = {};", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserQuests: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (!res) return false;

    MYSQL_ROW row;
    while ((row = mysql_fetch_row(res))) {
        UserQuestRecord rec;
        rec.questID = (unsigned int)atoi(row[0]);
        rec.state = (unsigned char)atoi(row[1]);
        rec.count0 = (unsigned short)atoi(row[2]);
        rec.count1 = (unsigned short)atoi(row[3]);
        rec.count2 = (unsigned short)atoi(row[4]);
        rec.count3 = (unsigned short)atoi(row[5]);
        rec.count4 = (unsigned short)atoi(row[6]);
        rec.count5 = (unsigned short)atoi(row[7]);
        rec.lastUpdatedDate = row[8] ? row[8] : "";
        rec.isTracked = row[9] ? (unsigned char)atoi(row[9]) : 0;

        outQuests[rec.questID] = rec;
    }
    mysql_free_result(res);
    return true;
}

bool DatabaseManager::SaveUserQuest(unsigned long userID, const UserQuestRecord& record) {
    if (_connection == NULL || record.questID == 0) return false;

    string query = format(
        "INSERT INTO user_quests (userID, questID, state, count0, count1, count2, count3, count4, count5, lastUpdatedDate, isTracked) "
        "VALUES ({}, {}, {}, {}, {}, {}, {}, {}, {}, CURRENT_DATE, {}) "
        "ON DUPLICATE KEY UPDATE "
        "  state = VALUES(state), "
        "  count0 = VALUES(count0), "
        "  count1 = VALUES(count1), "
        "  count2 = VALUES(count2), "
        "  count3 = VALUES(count3), "
        "  count4 = VALUES(count4), "
        "  count5 = VALUES(count5), "
        "  lastUpdatedDate = VALUES(lastUpdatedDate), "
        "  isTracked = VALUES(isTracked);",
        userID, record.questID, (int)record.state,
        record.count0, record.count1, record.count2, record.count3, record.count4, record.count5, (int)record.isTracked);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SaveUserQuest: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::ResetDailyQuests(unsigned long userID) {
    if (_connection == NULL) return false;

    // Reset daily quests (ID <= 100) where lastUpdatedDate is before today
    string query = format(
        "UPDATE user_quests SET state = 0, count0 = 0, count1 = 0, count2 = 0, count3 = 0, count4 = 0, count5 = 0, lastUpdatedDate = CURRENT_DATE "
        "WHERE userID = {} AND questID <= 100 AND lastUpdatedDate < CURRENT_DATE;", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetDailyQuests (update): {}\n", mysql_error(_connection)));
        return false;
    }

    // Delete expired special quests (1001 <= ID <= 1019) so a new rotation takes place today
    string deleteSpecialQuery = format(
        "DELETE FROM user_quests WHERE userID = {} AND questID >= 1001 AND questID <= 1019 AND lastUpdatedDate < CURRENT_DATE;", userID);

    if (mysql_query(_connection, deleteSpecialQuery.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetDailyQuests (delete special): {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::ResetUserQuestRecord(unsigned long userID, unsigned int questID) {
    if (_connection == NULL || questID == 0) return false;

    string query = format(
        "UPDATE user_quests SET state = 0, count0 = 0, count1 = 0, count2 = 0, count3 = 0, count4 = 0, count5 = 0, lastUpdatedDate = CURRENT_DATE "
        "WHERE userID = {} AND questID = {};", userID, questID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetUserQuestRecord: {}\n", mysql_error(_connection)));
        return false;
    }
    return true;
}

bool DatabaseManager::ResetUserDailyQuests(unsigned long userID) {
    if (_connection == NULL) return false;

    string query = format(
        "UPDATE user_quests SET state = 0, count0 = 0, count1 = 0, count2 = 0, count3 = 0, count4 = 0, count5 = 0, lastUpdatedDate = CURRENT_DATE "
        "WHERE userID = {} AND questID <= 100;", userID);
    mysql_query(_connection, query.c_str());

    // 重設今日全清標記
    string qStats = format("UPDATE user_quest_stats SET allClearCount = 0 WHERE userID = {};", userID);
    mysql_query(_connection, qStats.c_str());

    return true;
}

bool DatabaseManager::ResetUserWeeklyQuests(unsigned long userID) {
    if (_connection == NULL) return false;

    string query = format(
        "UPDATE user_quests SET state = 0, count0 = 0, count1 = 0, count2 = 0, count3 = 0, count4 = 0, count5 = 0, lastUpdatedDate = CURRENT_DATE "
        "WHERE userID = {} AND questID >= 501 AND questID <= 600;", userID);
    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ResetUserWeeklyQuests: {}\n", mysql_error(_connection)));
        return false;
    }
    return true;
}

bool DatabaseManager::DeleteUserSpecialQuest(unsigned long userID) {
    if (_connection == NULL) return false;

    string query = format(
        "DELETE FROM user_quests WHERE userID = {} AND questID >= 1001 AND questID <= 1019;", userID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on DeleteUserSpecialQuest: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::SetUserQuestTracked(unsigned long userID, unsigned int questID, unsigned char isTracked) {
    if (_connection == NULL || questID == 0) return false;

    string query = format(
        "INSERT INTO user_quests (userID, questID, state, isTracked, lastUpdatedDate) "
        "VALUES ({}, {}, 0, {}, CURRENT_DATE) "
        "ON DUPLICATE KEY UPDATE isTracked = VALUES(isTracked);",
        userID, questID, (int)isTracked);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on SetUserQuestTracked: {}\n", mysql_error(_connection)));
        return false;
    }

    return true;
}

bool DatabaseManager::GetUserQuestStats(unsigned long userID, UserQuestStats& outStats) {
    if (_connection == NULL) return false;

    outStats.honorPoints = GetUserHonorPoints(userID);
    outStats.specialQuestCount = 0;
    outStats.specialQuestStreakRecord = 0;
    outStats.todayRegularQuest = 0;
    outStats.allClearCount = 0;
    outStats.resetSpecialQuestCount = 5;
    outStats.todayPCQuest = 0; // 網咖任務暫未實現，固定為 0

    // 1. 查詢今日完成的每日任務個數 (questID <= 100 且 state >= 1 且 lastUpdatedDate = CURRENT_DATE)
    string qDaily = format(
        "SELECT COUNT(*) FROM user_quests WHERE userID = {} AND questID <= 100 AND state >= 1 AND lastUpdatedDate = CURRENT_DATE;",
        userID);
    if (mysql_query(_connection, qDaily.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(_connection);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) {
                outStats.todayRegularQuest = (unsigned char)atoi(row[0]);
            }
            mysql_free_result(res);
        }
    }

    // 2. 查詢持久化統計數據
    unsigned char recordRegularQuest = 0;
    string qStats = format(
        "SELECT specialStreak, specialMaxStreak, maxDailyCompleted, allClearCount, CAST(lastSpecialDate AS CHAR), "
        "       IF(lastSpecialDate IS NULL, 999, DATEDIFF(CURRENT_DATE, lastSpecialDate)) "
        "FROM user_quest_stats WHERE userID = {};", userID);
    if (mysql_query(_connection, qStats.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(_connection);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                unsigned short currentStreak = (unsigned short)atoi(row[0]);
                unsigned short maxStreak = (unsigned short)atoi(row[1]);
                int daysDiff = (row[5] != NULL) ? atoi(row[5]) : 999;

                // 如果距離上次完成特殊任務超過 1 天 (中斷)，連續天數歸 0，但最高紀錄保留
                if (daysDiff > 1) {
                    currentStreak = 0;
                }

                outStats.specialQuestCount = currentStreak;
                outStats.specialQuestStreakRecord = maxStreak;
                recordRegularQuest = (unsigned char)atoi(row[2]);
                outStats.allClearCount = (unsigned short)atoi(row[3]);
            }
            mysql_free_result(res);
        }
    }

    // 3. 今日最高紀錄動態覆寫
    if (outStats.todayRegularQuest > recordRegularQuest) {
        recordRegularQuest = outStats.todayRegularQuest;
        string qUp = format(
            "INSERT INTO user_quest_stats (userID, maxDailyCompleted) VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE maxDailyCompleted = VALUES(maxDailyCompleted);",
            userID, (int)recordRegularQuest);
        mysql_query(_connection, qUp.c_str());
    }

    return true;
}

bool DatabaseManager::RecordSpecialQuestCompleted(unsigned long userID) {
    if (_connection == NULL) return false;

    // 檢查 lastSpecialDate
    string qCheck = format(
        "SELECT specialStreak, specialMaxStreak, CAST(lastSpecialDate AS CHAR), DATEDIFF(CURRENT_DATE, lastSpecialDate) "
        "FROM user_quest_stats WHERE userID = {};", userID);

    unsigned short streak = 1;
    unsigned short maxStreak = 1;

    if (mysql_query(_connection, qCheck.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(_connection);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                unsigned short oldStreak = (unsigned short)atoi(row[0]);
                unsigned short oldMax = (unsigned short)atoi(row[1]);
                if (row[2] != NULL && row[3] != NULL) {
                    int diff = atoi(row[3]);
                    if (diff == 0) {
                        // 今日已經完成過，不重複累加天數
                        mysql_free_result(res);
                        return true;
                    }
                    else if (diff == 1) {
                        // 昨天完成過，連續 +1
                        streak = oldStreak + 1;
                    }
                    else {
                        // 中斷，重新計為 1
                        streak = 1;
                    }
                }
                else {
                    streak = 1;
                }
                maxStreak = std::max(oldMax, streak);
            }
            mysql_free_result(res);
        }
    }

    string qSave = format(
        "INSERT INTO user_quest_stats (userID, specialStreak, specialMaxStreak, lastSpecialDate) "
        "VALUES ({}, {}, {}, CURRENT_DATE) "
        "ON DUPLICATE KEY UPDATE specialStreak = {}, specialMaxStreak = {}, lastSpecialDate = CURRENT_DATE;",
        userID, streak, maxStreak, streak, maxStreak);

    if (mysql_query(_connection, qSave.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on RecordSpecialQuestCompleted: {}\n", mysql_error(_connection)));
        return false;
    }
    return true;
}

bool DatabaseManager::RecordDailyQuestCompleted(unsigned long userID) {
    if (_connection == NULL) return false;

    // 計算今日已完成的每日任務個數 (state >= 1)
    string qDaily = format(
        "SELECT COUNT(*) FROM user_quests WHERE userID = {} AND questID <= 100 AND state >= 1 AND lastUpdatedDate = CURRENT_DATE;",
        userID);
    unsigned short todayCount = 0;
    if (mysql_query(_connection, qDaily.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(_connection);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row && row[0]) {
                todayCount = (unsigned short)atoi(row[0]);
            }
            mysql_free_result(res);
        }
    }

    // 查詢當前統計數據 (maxDailyCompleted, allClearCount, lastAllClearDate)
    string qCheck = format(
        "SELECT maxDailyCompleted, allClearCount, CAST(lastAllClearDate AS CHAR), "
        "       IF(lastAllClearDate IS NULL, 999, DATEDIFF(CURRENT_DATE, lastAllClearDate)) "
        "FROM user_quest_stats WHERE userID = {};", userID);

    unsigned char maxCompleted = (unsigned char)todayCount;
    unsigned short allClearCount = 0;
    int allClearDiff = 999;

    if (mysql_query(_connection, qCheck.c_str()) == 0) {
        MYSQL_RES* res = mysql_store_result(_connection);
        if (res) {
            MYSQL_ROW row = mysql_fetch_row(res);
            if (row) {
                if (row[0]) maxCompleted = (unsigned char)std::max<int>(atoi(row[0]), todayCount);
                if (row[1]) allClearCount = (unsigned short)atoi(row[1]);
                if (row[3]) allClearDiff = atoi(row[3]);
            }
            mysql_free_result(res);
        }
    }

    // 當今日完成的每日任務達到 20 個（全部完成）且今天尚未計入過 All Clear 時，累計天數/次數 +1
    if (todayCount >= 20 && allClearDiff != 0) {
        allClearCount++;
        string qSave = format(
            "INSERT INTO user_quest_stats (userID, maxDailyCompleted, allClearCount, lastAllClearDate) "
            "VALUES ({}, {}, {}, CURRENT_DATE) "
            "ON DUPLICATE KEY UPDATE "
            "  maxDailyCompleted = GREATEST(maxDailyCompleted, {}), "
            "  allClearCount = {}, "
            "  lastAllClearDate = CURRENT_DATE;",
            userID, maxCompleted, allClearCount, todayCount, allClearCount);
        mysql_query(_connection, qSave.c_str());
    }
    else {
        string qSave = format(
            "INSERT INTO user_quest_stats (userID, maxDailyCompleted) "
            "VALUES ({}, {}) "
            "ON DUPLICATE KEY UPDATE "
            "  maxDailyCompleted = GREATEST(maxDailyCompleted, {});",
            userID, maxCompleted, todayCount);
        mysql_query(_connection, qSave.c_str());
    }

    return true;
}

bool DatabaseManager::EnsureGiftTables() {
    if (_connection == NULL) {
        return false;
    }

    const char* query =
        "CREATE TABLE IF NOT EXISTS user_gifts ("
        "    giftID INT UNSIGNED NOT NULL AUTO_INCREMENT PRIMARY KEY,"
        "    receiverUserID INT UNSIGNED NOT NULL,"
        "    senderUserID INT UNSIGNED NOT NULL,"
        "    senderNickName VARBINARY(64) NOT NULL,"
        "    productID INT UNSIGNED NOT NULL,"
        "    itemID INT UNSIGNED NOT NULL DEFAULT 0,"
        "    duration SMALLINT UNSIGNED NOT NULL DEFAULT 0,"
        "    isCount TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "    count SMALLINT UNSIGNED NOT NULL DEFAULT 1,"
        "    message VARBINARY(255) NOT NULL DEFAULT '',"
        "    createTime INT UNSIGNED NOT NULL DEFAULT 0,"
        "    claimed TINYINT UNSIGNED NOT NULL DEFAULT 0,"
        "    INDEX idx_receiver (receiverUserID, claimed)"
        ") ENGINE=InnoDB;";

    if (mysql_query(_connection, query)) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on EnsureGiftTables: {}\n", mysql_error(_connection)));
        return false;
    }

    // Migrate existing table columns to VARBINARY to support Big5 characters seamlessly
    mysql_query(_connection, "ALTER TABLE user_gifts MODIFY COLUMN senderNickName VARBINARY(64) NOT NULL;");
    mysql_query(_connection, "ALTER TABLE user_gifts MODIFY COLUMN message VARBINARY(255) NOT NULL DEFAULT '';");

    return true;
}

bool DatabaseManager::EnsureBookmarkTables() {
    if (_connection == NULL) {
        return false;
    }

    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN name VARBINARY(32) NOT NULL DEFAULT '';");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN primaryItemID SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN primaryAmmo SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN secondaryItemID SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN secondaryAmmo SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN flashbang SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN hegrenade SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN smokegrenade SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN defusekit SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN nightvision SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN kevlar SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks MODIFY COLUMN unk1 SMALLINT UNSIGNED NOT NULL DEFAULT 0;");
    mysql_query(_connection, "ALTER TABLE user_bookmarks ADD UNIQUE KEY uk_user_slot (userID, slotID);");

    return true;
}

bool DatabaseManager::AddUserGift(const UserGift& gift, unsigned int& outGiftID) {
    if (_connection == NULL) {
        return false;
    }

    char escapedSender[128] = {};
    mysql_real_escape_string(_connection, escapedSender, gift.senderNickName.c_str(), (unsigned long)gift.senderNickName.size());

    char escapedMsg[512] = {};
    mysql_real_escape_string(_connection, escapedMsg, gift.message.c_str(), (unsigned long)gift.message.size());

    string query = format(
        "INSERT INTO user_gifts (receiverUserID, senderUserID, senderNickName, productID, itemID, duration, isCount, count, message, createTime, claimed) "
        "VALUES ({}, {}, '{}', {}, {}, {}, {}, {}, '{}', {}, 0);",
        gift.receiverUserID, gift.senderUserID, escapedSender,
        gift.productID, gift.itemID, gift.duration, gift.isCount, gift.count,
        escapedMsg, gift.createTime);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on AddUserGift: {}\n", mysql_error(_connection)));
        return false;
    }

    outGiftID = (unsigned int)mysql_insert_id(_connection);
    return true;
}

const std::vector<UserGift> DatabaseManager::GetUserGifts(unsigned long receiverUserID) {
    if (_connection == NULL) {
        return {};
    }

    string query = format(
        "SELECT giftID, receiverUserID, senderUserID, senderNickName, productID, itemID, duration, isCount, count, message, createTime, claimed "
        "FROM user_gifts WHERE receiverUserID = {} AND claimed = 0 ORDER BY giftID DESC;",
        receiverUserID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserGifts: {}\n", mysql_error(_connection)));
        return {};
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return {};
    }

    std::vector<UserGift> gifts;
    MYSQL_ROW row = NULL;
    while ((row = mysql_fetch_row(res))) {
        UserGift g;
        g.giftID = (unsigned int)strtoul(row[0], nullptr, 10);
        g.receiverUserID = strtoul(row[1], nullptr, 10);
        g.senderUserID = strtoul(row[2], nullptr, 10);
        g.senderNickName = row[3] ? row[3] : "";
        g.productID = (unsigned int)strtoul(row[4], nullptr, 10);
        g.itemID = (unsigned short)atoi(row[5]);
        g.duration = (unsigned short)atoi(row[6]);
        g.isCount = (unsigned char)atoi(row[7]);
        g.count = (unsigned short)atoi(row[8]);
        g.message = row[9] ? row[9] : "";
        g.createTime = (unsigned int)strtoul(row[10], nullptr, 10);
        g.claimed = (unsigned char)atoi(row[11]);
        gifts.push_back(g);
    }
    mysql_free_result(res);
    return gifts;
}

bool DatabaseManager::GetUserGiftByID(unsigned int giftID, unsigned long receiverUserID, UserGift& outGift) {
    if (_connection == NULL) {
        return false;
    }

    string query = format(
        "SELECT giftID, receiverUserID, senderUserID, senderNickName, productID, itemID, duration, isCount, count, message, createTime, claimed "
        "FROM user_gifts WHERE giftID = {} AND receiverUserID = {} LIMIT 1;",
        giftID, receiverUserID);

    if (mysql_query(_connection, query.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on GetUserGiftByID: {}\n", mysql_error(_connection)));
        return false;
    }

    MYSQL_RES* res = mysql_store_result(_connection);
    if (res == NULL) {
        return false;
    }

    MYSQL_ROW row = NULL;
    row = mysql_fetch_row(res);
    if (row == NULL) {
        mysql_free_result(res);
        return false;
    }

    outGift.giftID = (unsigned int)strtoul(row[0], nullptr, 10);
    outGift.receiverUserID = strtoul(row[1], nullptr, 10);
    outGift.senderUserID = strtoul(row[2], nullptr, 10);
    outGift.senderNickName = row[3] ? row[3] : "";
    outGift.productID = (unsigned int)strtoul(row[4], nullptr, 10);
    outGift.itemID = (unsigned short)atoi(row[5]);
    outGift.duration = (unsigned short)atoi(row[6]);
    outGift.isCount = (unsigned char)atoi(row[7]);
    outGift.count = (unsigned short)atoi(row[8]);
    outGift.message = row[9] ? row[9] : "";
    outGift.createTime = (unsigned int)strtoul(row[10], nullptr, 10);
    outGift.claimed = (unsigned char)atoi(row[11]);
    mysql_free_result(res);
    return true;
}

bool DatabaseManager::ClaimUserGift(unsigned int giftID, unsigned long receiverUserID, unsigned short claimCount, unsigned short& outRemainingCount) {
    if (_connection == NULL) {
        return false;
    }

    string q = format("SELECT count FROM user_gifts WHERE giftID = {} AND receiverUserID = {} AND claimed = 0 LIMIT 1;", giftID, receiverUserID);
    if (mysql_query(_connection, q.c_str())) {
        serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ClaimUserGift check: {}\n", mysql_error(_connection)));
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
    mysql_free_result(res);

    if (claimCount >= currentCount || currentCount == 0) {
        outRemainingCount = 0;
        string uq = format("UPDATE user_gifts SET count = 0, claimed = 1 WHERE giftID = {} AND receiverUserID = {};", giftID, receiverUserID);
        if (mysql_query(_connection, uq.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ClaimUserGift update: {}\n", mysql_error(_connection)));
            return false;
        }
    }
    else {
        outRemainingCount = currentCount - claimCount;
        string uq = format("UPDATE user_gifts SET count = {} WHERE giftID = {} AND receiverUserID = {};", outRemainingCount, giftID, receiverUserID);
        if (mysql_query(_connection, uq.c_str())) {
            serverConsole.Print(PrefixType::Error, format("[ DatabaseManager ] Query error on ClaimUserGift update: {}\n", mysql_error(_connection)));
            return false;
        }
    }

    return true;
}





