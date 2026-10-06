#pragma once
#include "tcp_connection.h"
#include <vector>
#include <string>

using namespace std;

enum ServerType {
	Normal = 0,
	Newbie = 1
};

enum ServerStatus {
	NotReady = 0,
	Ready = 1
};

struct Channel {
	unsigned char id = 0;
	string name = "";
	unsigned short numPlayers = 0;
	unsigned short maxPlayers = 0;
	string ip = "";
	unsigned short port = 0;
	bool isOnline = true;
	unsigned long long lastHeartbeat = 0;
	string loginMsg = "";
};

struct Server {
	unsigned char id = 0;
	ServerStatus status = ServerStatus::NotReady;
	ServerType type = ServerType::Normal;
	string name = "";
	vector<Channel> channels;
};

#include <unordered_set>
#include <unordered_map>

struct ItemCommandConfig {
	bool verifyItemCSVHash = true;
	string expectedItemCSVHash = "b21036850d713889e6bb39ac4b9580f5ac291a53e6b965700bdf47cb8ecfc711";
	unordered_set<unsigned short> disabledItemIDs;
	unordered_map<unsigned char, unordered_set<unsigned short>> categoryExcludeItemIDs;
	unordered_map<unsigned char, unordered_set<unsigned short>> categoryIncludeItemIDs;
	unordered_set<unsigned char> excludedCategories;
	unordered_set<unsigned char> includedCategories;
};

struct PermissionConfig {
	vector<string> gmList = { "test", "admin" };
	bool allowPlayerItemCommands = true;
	bool allowPlayerDailyCommands = true;
	bool allowPlayerNotice = false;
	bool allowPlayerReloadShop = false;
	bool allowPlayerReloadEnhance = false;
};

struct ShopConfig {
	bool broadcastOnReload = false;
};

class ServerConfig {
public:
	ServerConfig();

	bool Load();

	unsigned long ip;
	unsigned short port;
	bool ssl;
	CipherMethod decryptCipherMethod;
	CipherMethod encryptCipherMethod;
	string udpAuthKey;
	unsigned short maxPlayers;
	unsigned char serverID;
	unsigned char channelID;
	unsigned char launcherVersion;
	unsigned short clientVersion;
	string clientBuildTimestamp;
	unsigned long clientNARChecksum;
	vector<Server> serverList;
	string sqlServer;
	string sqlUser;
	string sqlPassword;
	string sqlDatabase;
	vector<string> prohibitedNames;
	vector<BuyMenu> defaultBuyMenus;
	vector<unsigned short> defaultInventory;
	ItemCommandConfig itemCommands;
	PermissionConfig permissions;
	ShopConfig shop;
};

extern ServerConfig serverConfig;
