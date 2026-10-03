#include "serverconfig.h"
#include "serverconsole.h"
#include "nlohmann/json.hpp"
#include <fstream>

using json = nlohmann::json;

ServerConfig serverConfig;

ServerConfig::ServerConfig() {
	ip = 0;
	port = 0;
	ssl = false;
	decryptCipherMethod = CipherMethod::CleanUp;
	encryptCipherMethod = CipherMethod::CleanUp;
	udpAuthKey = "";
	maxPlayers = 0;
	serverID = 0;
	channelID = 0;
	launcherVersion = 67;
	clientVersion = 11;
	clientBuildTimestamp = "11.06.08";
	clientNARChecksum = 1860010506;
	sqlServer = "";
	sqlUser = "";
	sqlPassword = "";
	sqlDatabase = "";
}

string defaultServerConfig = R"({
	"IP": "127.0.0.1",
	"Port": 8001,
	"SSL": true,
	"DecryptCipherMethod": 1,
	"EncryptCipherMethod": 1,
	"UDPAuthKey": "",
	"MaxPlayers": 600,
	"ServerID": 1,
	"ChannelID": 1,
	"LauncherVersion": 67,
	"ClientVersion": 11,
	"ClientBuildTimestamp": "11.06.08",
	"ClientNARChecksum": 1860010506,
	"ServerList": [
		{
			"Type": 0,
			"Name": "Server 1",
			"Channels": [
				{
					"Name": "Channel 1-1"
				}
			]
		}
	],
	"SQL": {
		"Server": "localhost",
		"User": "root",
		"Password": "",
		"Database": "csodatabase"
	},
	"ProhibitedNames": [],
	"DefaultBuyMenus": {
		"Terrorist": {
			"Pistol": [ 3, 6, 2, 4, 1 ],
			"ShotGun": [ 7, 8, 38 ],
			"SubMachineGun": [ 10, 12, 13, 11, 37 ],
			"Rifle": [ 23, 21, 17, 19, 14, 22, 34, 39 ],
			"MachineGun": [ 24, 32 ],
			"Equipment": [ 27, 28, 30, 31, 26, 25 ],
			"Class": [ 41, 42, 43, 40, 44, 45, 46, 47, 48 ],
			"Knife": [ 161 ]
		},
		"Counter-Terrorist": {
			"Pistol": [ 3, 6, 2, 4, 5 ],
			"ShotGun": [ 7, 8 ],
			"SubMachineGun": [ 9, 12, 13, 11, 36, 37 ],
			"Rifle": [ 20, 17, 15, 16, 18, 14, 33, 35 ],
			"MachineGun": [ 24, 32 ],
			"Equipment": [ 27, 28, 30, 31, 26, 25, 29 ],
			"Class": [ 49, 50, 52, 51, 53, 54, 55, 56, 57 ],
			"Knife": [ 161 ]
		}
	},
	"DefaultInventory": [ 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40, 41, 42, 43, 44, 45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57 ]
})";

const char* buyMenuCategories[] = {
	"Pistol",
	"ShotGun",
	"SubMachineGun",
	"Rifle",
	"MachineGun",
	"Equipment",
	"Class"
};

bool ServerConfig::Load() {
	try {
		ifstream f("configs\\serverconfig.json");
		json config;

		if (f.fail()) {
			config = json::parse(defaultServerConfig);
			serverConsole.Print(PrefixType::Warn, format("[ ServerConfig ] Couldn't open serverconfig.json, using default values!\n"));
		}
		else {
			json defaultConfig = json::parse(defaultServerConfig);
			config = json::parse(f);

			defaultConfig.update(config, true);
			config = defaultConfig;
		}

		f.close();

		if (config.contains("IP")) {
			string ipStr = config.value("IP", "127.0.0.1");

			struct sockaddr_in addr {};
			inet_pton(AF_INET, ipStr.c_str(), &(addr.sin_addr));

			ip = addr.sin_addr.S_un.S_addr;
		}
		if (config.contains("Port")) {
			port = config.value("Port", 8001);
		}
		if (config.contains("SSL")) {
			ssl = config.value("SSL", true);
		}
		if (config.contains("DecryptCipherMethod")) {
			unsigned char decryptCipherMethod = config.value("DecryptCipherMethod", 1);

			if (decryptCipherMethod == 1 || decryptCipherMethod == 2) {
				this->decryptCipherMethod = (CipherMethod)(decryptCipherMethod + 1);
			}
			else {
				this->decryptCipherMethod = CipherMethod::Null;
			}
		}
		if (config.contains("EncryptCipherMethod")) {
			unsigned char encryptCipherMethod = config.value("EncryptCipherMethod", 1);

			if (encryptCipherMethod == 1 || encryptCipherMethod == 2) {
				this->encryptCipherMethod = (CipherMethod)(encryptCipherMethod + 1);
			}
			else {
				this->encryptCipherMethod = CipherMethod::Null;
			}
		}
		if (config.contains("UDPAuthKey")) {
			udpAuthKey = config.value("UDPAuthKey", "");
		}
		if (config.contains("MaxPlayers")) {
			maxPlayers = config.value("MaxPlayers", 600);
		}
		if (config.contains("ServerID")) {
			serverID = config.value("ServerID", 1);
		}
		if (config.contains("ChannelID")) {
			channelID = config.value("ChannelID", 1);
		}
		if (config.contains("LauncherVersion")) {
			launcherVersion = config.value("LauncherVersion", 67);
		}
		if (config.contains("ClientVersion")) {
			clientVersion = config.value("ClientVersion", 11);
		}
		if (config.contains("ClientBuildTimestamp")) {
			clientBuildTimestamp = config.value("ClientBuildTimestamp", "11.06.08");
		}
		if (config.contains("ClientNARChecksum")) {
			clientNARChecksum = config.value("ClientNARChecksum", 1860010506);
		}
		if (config.contains("ServerList")) {
			json serverList = config["ServerList"];

			unsigned char serverID = 1;

			for (auto& server : serverList) {
				Server s;
				s.id = serverID++;
				s.status = (ServerStatus)(s.id == this->serverID);
				s.type = (ServerType)server.value("Type", 0);
				s.name = server.value("Name", format("Server {}", s.id));

				if (server.contains("Channels")) {
					json channels = server["Channels"];

					unsigned char channelID = 1;

					for (auto& channel : channels) {
						Channel ch;
						ch.id = channelID++;
						ch.name = channel.value("Name", format("Channel {}-{}", s.id, ch.id));

						if (s.id != this->serverID || s.id == this->serverID && ch.id != this->channelID) {
							ch.maxPlayers = channel.value("MaxPlayers", 600);
							ch.ip = channel.value("IP", "127.0.0.1");
							ch.port = channel.value("Port", 8001);
							ch.isOnline = false;
						}

						s.channels.push_back(ch);
					}
				}

				this->serverList.push_back(s);
			}
		}
		if (config.contains("SQL")) {
			json sql = config["SQL"];

			sqlServer = sql.value("Server", "localhost");
			sqlUser = sql.value("User", "root");
			sqlPassword = sql.value("Password", "");
			sqlDatabase = sql.value("Database", "csodatabase");
		}
		if (config.contains("ProhibitedNames")) {
			prohibitedNames = config["ProhibitedNames"].get<vector<string>>();
		}
		if (config.contains("DefaultBuyMenus")) {
			json defaultBuyMenus = config["DefaultBuyMenus"];
			
			if (defaultBuyMenus.contains("Terrorist")) {
				json buyMenuT = defaultBuyMenus["Terrorist"];
				
				for (unsigned char categoryID = 0; categoryID < BUYMENU_MAX_CATEGORY; categoryID++) {
					string categoryName = buyMenuCategories[categoryID];

					if (buyMenuT.contains(categoryName)) {
						BuyMenu bm;
						bm.categoryID = categoryID;
						bm.items = buyMenuT[categoryName].get<vector<unsigned short>>();
						bm.items.resize(BUYMENU_MAX_SLOT);

						this->defaultBuyMenus.push_back(bm);
					}
				}
				if (buyMenuT.contains("Knife")) {
					BuyMenu bm;
					bm.categoryID = 14;
					bm.items = buyMenuT["Knife"].get<vector<unsigned short>>();
					bm.items.resize(BUYMENU_MAX_SLOT);
					this->defaultBuyMenus.push_back(bm);
				}
				else {
					BuyMenu bm;
					bm.categoryID = 14;
					bm.items.resize(BUYMENU_MAX_SLOT);
					bm.items[0] = 161;
					this->defaultBuyMenus.push_back(bm);
				}
			}
			if (defaultBuyMenus.contains("Counter-Terrorist")) {
				json buyMenuCT = defaultBuyMenus["Counter-Terrorist"];

				for (unsigned char categoryID = 0; categoryID < BUYMENU_MAX_CATEGORY; categoryID++) {
					string categoryName = buyMenuCategories[categoryID];

					if (buyMenuCT.contains(categoryName)) {
						BuyMenu bm;
						bm.categoryID = categoryID + 7;
						bm.items = buyMenuCT[categoryName].get<vector<unsigned short>>();
						bm.items.resize(BUYMENU_MAX_SLOT);

						this->defaultBuyMenus.push_back(bm);
					}
				}

				if (buyMenuCT.contains("Knife")) {
					BuyMenu bm;
					bm.categoryID = 15;
					bm.items = buyMenuCT["Knife"].get<vector<unsigned short>>();
					bm.items.resize(BUYMENU_MAX_SLOT);
					this->defaultBuyMenus.push_back(bm);
				}
				else {
					BuyMenu bm;
					bm.categoryID = 15;
					bm.items.resize(BUYMENU_MAX_SLOT);
					bm.items[0] = 161;
					this->defaultBuyMenus.push_back(bm);
				}
			}
		}
		if (config.contains("DefaultInventory")) {
			defaultInventory = config["DefaultInventory"].get<vector<unsigned short>>();
		}
		if (config.contains("ItemCommands")) {
			const auto& ic = config["ItemCommands"];
			if (ic.contains("VerifyItemCSVHash")) {
				itemCommands.verifyItemCSVHash = ic.value("VerifyItemCSVHash", true);
			}
			if (ic.contains("ExpectedItemCSVHash")) {
				itemCommands.expectedItemCSVHash = ic.value("ExpectedItemCSVHash", "b21036850d713889e6bb39ac4b9580f5ac291a53e6b965700bdf47cb8ecfc711");
			}
			if (ic.contains("DisabledItemIDs") && ic["DisabledItemIDs"].is_array()) {
				for (auto& idVal : ic["DisabledItemIDs"]) {
					itemCommands.disabledItemIDs.insert((unsigned short)idVal.get<unsigned long>());
				}
			}
			if (ic.contains("CategoryExcludeItemIDs") && ic["CategoryExcludeItemIDs"].is_object()) {
				for (auto& [catKey, val] : ic["CategoryExcludeItemIDs"].items()) {
					try {
						unsigned char catID = (unsigned char)stoul(catKey);
						if (val.is_array()) {
							for (auto& idVal : val) {
								itemCommands.categoryExcludeItemIDs[catID].insert((unsigned short)idVal.get<unsigned long>());
							}
						}
					}
					catch (...) {}
				}
			}
			if (ic.contains("CategoryIncludeItemIDs") && ic["CategoryIncludeItemIDs"].is_object()) {
				for (auto& [catKey, val] : ic["CategoryIncludeItemIDs"].items()) {
					try {
						unsigned char catID = (unsigned char)stoul(catKey);
						if (val.is_array()) {
							for (auto& idVal : val) {
								itemCommands.categoryIncludeItemIDs[catID].insert((unsigned short)idVal.get<unsigned long>());
							}
						}
					}
					catch (...) {}
				}
			}
			auto parseConfigCat = [](const json& v) -> unsigned char {
				if (v.is_number()) return (unsigned char)v.get<unsigned int>();
				if (v.is_string()) {
					string s = v.get<string>();
					transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)tolower(c); });
					if (s == "1" || s == "手槍" || s == "手枪" || s == "pistol") return 1;
					if (s == "2" || s == "散彈槍" || s == "散弹枪" || s == "霰彈槍" || s == "霰弹枪" || s == "shotgun") return 2;
					if (s == "3" || s == "衝鋒槍" || s == "冲锋枪" || s == "smg") return 3;
					if (s == "4" || s == "步槍" || s == "步枪" || s == "rifle") return 4;
					if (s == "5" || s == "機槍" || s == "机枪" || s == "mg" || s == "machinegun") return 5;
					if (s == "6" || s == "裝備" || s == "装备" || s == "equipment") return 6;
					if (s == "7" || s == "人物" || s == "角色" || s == "class" || s == "character") return 7;
					if (s == "8" || s == "道具" || s == "紋身" || s == "纹身" || s == "passive") return 8;
					if (s == "9" || s == "消耗品" || s == "消耗" || s == "active") return 9;
					if (s == "11" || s == "近戰" || s == "近战" || s == "近戰武器" || s == "近战武器" || s == "刀" || s == "knife" || s == "melee") return 11;
					if (s == "12" || s == "服飾" || s == "服饰" || s == "飾品" || s == "饰品" || s == "服飾飾品" || s == "costume" || s == "accessory") return 12;
					try {
						return (unsigned char)stoul(s);
					} catch (...) {}
				}
				return 255;
			};
			if (ic.contains("ExcludedCategories") && ic["ExcludedCategories"].is_array()) {
				for (auto& catVal : ic["ExcludedCategories"]) {
					unsigned char c = parseConfigCat(catVal);
					if (c != 255) {
						itemCommands.excludedCategories.insert(c);
					}
				}
			}
			if (ic.contains("IncludedCategories") && ic["IncludedCategories"].is_array()) {
				for (auto& catVal : ic["IncludedCategories"]) {
					unsigned char c = parseConfigCat(catVal);
					if (c != 255) {
						itemCommands.includedCategories.insert(c);
					}
				}
			}
		}

		serverConsole.Print(PrefixType::Info, "[ ServerConfig ] Loaded server configs!\n");
	}
	catch (json::parse_error& e) {
		serverConsole.Print(PrefixType::Error, format("[ ServerConfig ] Error in parsing serverconfig.json: {}\n", e.what()));
		return false;
	}

	return true;
}
