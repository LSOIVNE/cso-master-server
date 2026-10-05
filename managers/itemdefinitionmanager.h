#pragma once

#include <string>
#include <vector>
#include <unordered_map>

enum class CostumeSlot : int {
	None = 0,
	Head = 1,
	Back = 2,
	Pelvis = 3,
	Face = 4,
	ZombieSkin = 5
};

struct ItemDefinition {
	unsigned short id = 0;
	std::string name;
	std::string className;
	unsigned char category = 0;
	bool isPeriod = false; // true: duration-based (Period=1), false: count-based (Period=0)

	bool IsCostume() const {
		return category == 12;
	}

	bool IsEquipment() const {
		if (className == "NicknameChange" || className == "NicknameCamouflage") {
			return false;
		}
		if (isPeriod) {
			return true;
		}
		if (category >= 1 && category <= 7) {
			return true;
		}
		if (category == 11 || category == 12) {
			return true;
		}
		if (className == "Equipment" || className == "Class" || className == "Tattoo" || IsCostume()) {
			return true;
		}
		return false;
	}

	CostumeSlot GetCostumeSlot() const;
};

class ItemDefinitionManager {
public:
	bool Init();
	const ItemDefinition* GetItem(unsigned short id) const;
	std::vector<ItemDefinition> GetItemsByCategory(unsigned char category) const;
	std::vector<ItemDefinition> GetAllItems() const;
	std::string GetCategoryName(unsigned char category) const;
	unsigned char ParseCategory(const std::string& input) const;
	bool IsItemDisabled(unsigned short id) const;
	bool IsCategoryAllowed(unsigned char category) const;
	bool IsItemAllowedInCategory(unsigned char category, unsigned short id) const;

private:
	std::unordered_map<unsigned short, ItemDefinition> _items;
	std::vector<unsigned short> _itemOrder;
};

extern ItemDefinitionManager itemDefinitionManager;
