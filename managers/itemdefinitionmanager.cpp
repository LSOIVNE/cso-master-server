#include "itemdefinitionmanager.h"
#include "serverconfig.h"
#include "serverconsole.h"
#include <openssl/sha.h>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <algorithm>
#include <format>
#include "nlohmann/json.hpp"

ItemDefinitionManager itemDefinitionManager;

static std::string ComputeSHA256(const std::string& path) {
	std::ifstream file(path, std::ios::binary);
	if (!file.is_open()) return "";

	SHA256_CTX ctx;
	SHA256_Init(&ctx);

	char buf[8192];
	while (file.read(buf, sizeof(buf))) {
		SHA256_Update(&ctx, buf, file.gcount());
	}
	if (file.gcount() > 0) {
		SHA256_Update(&ctx, buf, file.gcount());
	}

	unsigned char hash[SHA256_DIGEST_LENGTH];
	SHA256_Final(hash, &ctx);

	std::stringstream ss;
	for (int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
		ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
	}
	return ss.str();
}

bool ItemDefinitionManager::Init() {
	_items.clear();
	_itemOrder.clear();

	const std::string loadedPath = "configs/item.csv";
	std::ifstream file(loadedPath);
	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, "[ ItemDefinitionManager ] Failed to open configs/item.csv!\n");
		return false;
	}

	serverConsole.Print(PrefixType::Info, format("[ ItemDefinitionManager ] Loading items from: {}\n", loadedPath));

	if (serverConfig.itemCommands.verifyItemCSVHash) {
		std::string hash = ComputeSHA256(loadedPath);
		std::string expected = serverConfig.itemCommands.expectedItemCSVHash;
		std::string hashLower = hash;
		std::string expLower = expected;
		std::transform(hashLower.begin(), hashLower.end(), hashLower.begin(), ::tolower);
		std::transform(expLower.begin(), expLower.end(), expLower.begin(), ::tolower);

		if (hashLower == expLower) {
			serverConsole.Print(PrefixType::Info, format("[ ItemDefinitionManager ] item.csv SHA-256 hash verified: {} (CSO TW 2013 client matched!)\n", hash));
		}
		else {
			serverConsole.Print(PrefixType::Warn, format("[ ItemDefinitionManager ] WARNING: item.csv SHA-256 mismatch!\n  Expected: {}\n  Found:    {}\n  (Please ensure you are using the authentic CSO TW 2013 item.csv!)\n", expected, hash));
		}
	}

	std::string line;
	while (std::getline(file, line)) {
		if (line.empty() || line[0] == ';' || line[0] == '\r' || line[0] == '\n') {
			continue;
		}

		std::vector<std::string> fields;
		std::string cur;
		bool inQuotes = false;
		for (size_t i = 0; i < line.size(); i++) {
			char c = line[i];
			if (c == '"') {
				inQuotes = !inQuotes;
			}
			else if (c == ',' && !inQuotes) {
				fields.push_back(cur);
				cur.clear();
			}
			else if (c != '\r' && c != '\n') {
				cur += c;
			}
		}
		fields.push_back(cur);

		if (fields.size() < 28) {
			continue;
		}

		try {
			unsigned short id = (unsigned short)std::stoul(fields[0]);
			std::string name = fields[1];
			std::string className = fields[3];
			unsigned char category = (unsigned char)std::stoul(fields[4]);
			bool isPeriod = (fields[27] == "1");

			ItemDefinition itemDef;
			itemDef.id = id;
			itemDef.name = name;
			itemDef.className = className;
			itemDef.category = category;
			itemDef.isPeriod = isPeriod;
			if (fields.size() > 5 && !fields[5].empty()) {
				try { itemDef.inGameID = (unsigned char)std::stoul(fields[5]); } catch (...) {}
			}
			if (fields.size() > 7 && !fields[7].empty()) {
				try { itemDef.team = (unsigned char)std::stoul(fields[7]); } catch (...) {}
			}
			if (fields.size() > 8 && !fields[8].empty()) {
				try { itemDef.level = (unsigned char)std::stoul(fields[8]); } catch (...) {}
			}

			if (fields.size() > 38 && !fields[38].empty()) {
				try { itemDef.disReward1 = (unsigned short)std::stoul(fields[38]); } catch (...) {}
			}
			if (fields.size() > 39 && !fields[39].empty()) {
				try { itemDef.disReward2 = (unsigned short)std::stoul(fields[39]); } catch (...) {}
			}
			if (fields.size() > 40 && !fields[40].empty()) {
				try { itemDef.disReward3 = (unsigned short)std::stoul(fields[40]); } catch (...) {}
			}

			_items[id] = itemDef;
			_itemOrder.push_back(id);
		}
		catch (...) {
			continue;
		}
	}

	file.close();
	serverConsole.Print(PrefixType::Info, format("[ ItemDefinitionManager ] Successfully loaded {} item definitions from item.csv.\n", _items.size()));

	// Load authentic Traditional Chinese item names mapping if available
	const std::string np = "configs/item_names_tw.json";
	std::ifstream nFile(np);
	if (nFile.is_open()) {
		try {
			nlohmann::json j;
			nFile >> j;
			size_t twCount = 0;
			for (auto& [key, val] : j.items()) {
				unsigned short id = (unsigned short)std::stoul(key);
				if (_items.find(id) != _items.end() && val.is_string()) {
					_items[id].name = val.get<std::string>();
					twCount++;
				}
			}
			serverConsole.Print(PrefixType::Info, format("[ ItemDefinitionManager ] Loaded {} authentic Chinese item names from {}\n", twCount, np));
		}
		catch (const std::exception& e) {
			serverConsole.Print(PrefixType::Warn, format("[ ItemDefinitionManager ] Failed to parse {}: {}\n", np, e.what()));
		}
	}

	return true;
}

CostumeSlot ItemDefinition::GetCostumeSlot() const {
	if (!IsCostume()) return CostumeSlot::None;
	if (className == "HeadCostume") return CostumeSlot::Head;
	if (className == "BackCostume") return CostumeSlot::Back;
	if (className == "PelvisCostume") return CostumeSlot::Pelvis;
	if (className == "FaceCostume") return CostumeSlot::Face;
	if (className == "ZombieSkinCostume") return CostumeSlot::ZombieSkin;
	return CostumeSlot::None;
}

const ItemDefinition* ItemDefinitionManager::GetItem(unsigned short id) const {
	auto it = _items.find(id);
	if (it != _items.end()) {
		return &(it->second);
	}
	return NULL;
}

std::vector<ItemDefinition> ItemDefinitionManager::GetItemsByCategory(unsigned char category) const {
	std::vector<ItemDefinition> result;
	if (!IsCategoryAllowed(category)) {
		return result;
	}
	for (unsigned short id : _itemOrder) {
		auto it = _items.find(id);
		if (it != _items.end() && it->second.category == category) {
			if (IsItemAllowedInCategory(category, id)) {
				result.push_back(it->second);
			}
		}
	}
	return result;
}

std::vector<ItemDefinition> ItemDefinitionManager::GetAllItems() const {
	std::vector<ItemDefinition> result;
	for (unsigned short id : _itemOrder) {
		auto it = _items.find(id);
		if (it != _items.end()) {
			if (IsCategoryAllowed(it->second.category) && IsItemAllowedInCategory(it->second.category, id)) {
				result.push_back(it->second);
			}
		}
	}
	return result;
}

std::string ItemDefinitionManager::GetCategoryName(unsigned char category) const {
	switch (category) {
		case 1: return "手槍";
		case 2: return "散彈槍";
		case 3: return "衝鋒槍";
		case 4: return "步槍";
		case 5: return "機槍";
		case 6: return "裝備";
		case 7: return "人物";
		case 8: return "道具";
		case 9: return "消耗品";
		case 11: return "近戰武器";
		case 12: return "服飾飾品";
		case 0: return "全部";
		default: return "未知分類";
	}
}

unsigned char ItemDefinitionManager::ParseCategory(const std::string& input) const {
	if (input.empty()) {
		return 255;
	}

	std::string s = input;
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return (char)std::tolower(c); });

	if (s == "1" || s == "手槍" || s == "手枪" || s == "pistol") return 1;
	if (s == "2" || s == "散彈槍" || s == "散弹枪" || s == "霰彈槍" || s == "霰弹枪" || s == "shotgun") return 2;
	if (s == "3" || s == "衝鋒槍" || s == "冲锋枪" || s == "smg") return 3;
	if (s == "4" || s == "步槍" || s == "步枪" || s == "rifle") return 4;
	if (s == "5" || s == "機槍" || s == "机枪" || s == "mg" || s == "machinegun") return 5;
	if (s == "6" || s == "裝備" || s == "装备" || s == "equipment") return 6;
	if (s == "7" || s == "人物" || s == "角色" || s == "class" || s == "character") return 7;
	if (s == "8" || s == "道具" || s == "紋身" || s == "纹身" || s == "功能" || s == "passive") return 8;
	if (s == "9" || s == "消耗品" || s == "消耗" || s == "密碼箱" || s == "密码箱" || s == "解碼器" || s == "解码器" || s == "active") return 9;
	if (s == "11" || s == "近戰" || s == "近战" || s == "近戰武器" || s == "近战武器" || s == "刀" || s == "knife" || s == "melee") return 11;
	if (s == "12" || s == "服飾" || s == "服饰" || s == "飾品" || s == "饰品" || s == "服飾飾品" || s == "服饰饰品" || s == "costume" || s == "accessory") return 12;

	return 255; // Unknown
}

bool ItemDefinitionManager::IsCategoryAllowed(unsigned char category) const {
	if (serverConfig.itemCommands.excludedCategories.count(category) > 0) {
		return false;
	}
	if (!serverConfig.itemCommands.includedCategories.empty() && serverConfig.itemCommands.includedCategories.count(category) == 0) {
		return false;
	}
	return true;
}

bool ItemDefinitionManager::IsItemDisabled(unsigned short id) const {
	return (serverConfig.itemCommands.disabledItemIDs.count(id) > 0);
}

bool ItemDefinitionManager::IsItemAllowedInCategory(unsigned char category, unsigned short id) const {
	if (IsItemDisabled(id)) {
		return false;
	}

	auto itEx = serverConfig.itemCommands.categoryExcludeItemIDs.find(category);
	if (itEx != serverConfig.itemCommands.categoryExcludeItemIDs.end()) {
		if (itEx->second.count(id) > 0) {
			return false;
		}
	}

	auto itIn = serverConfig.itemCommands.categoryIncludeItemIDs.find(category);
	if (itIn != serverConfig.itemCommands.categoryIncludeItemIDs.end() && !itIn->second.empty()) {
		if (itIn->second.count(id) == 0) {
			return false;
		}
	}

	return true;
}
