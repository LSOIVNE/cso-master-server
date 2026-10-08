#pragma once

#include "user.h"
#include <vector>
#include <string>
#include <unordered_map>

// 6 維屬性編碼結構 (對齊 GameUI.dll sub_10059FB0 與 sub_10055AF0)
struct WeaponEnhanceStats {
	uint8_t accuracy = 0;   // Index 0: bits 0~3 (命中率)
	uint8_t damage = 0;     // Index 1: bits 4~7 (傷害)
	uint8_t kickback = 0;   // Index 2: bits 8~11 (後座力 / Rebound)
	uint8_t weight = 0;     // Index 3: bits 12~15 (重量)
	uint8_t fireRate = 0;   // Index 4: bits 16~19 (連射速度 / Repeatedly)
	uint8_t ammo = 0;       // Index 5: bits 20~23 (載彈量 / Load)

	uint32_t ToBits() const {
		return (accuracy & 0xF) |
			((damage & 0xF) << 4) |
			((kickback & 0xF) << 8) |
			((weight & 0xF) << 12) |
			((fireRate & 0xF) << 16) |
			((ammo & 0xF) << 20);
	}

	static WeaponEnhanceStats FromBits(uint32_t bits) {
		WeaponEnhanceStats s;
		s.accuracy = bits & 0xF;
		s.damage = (bits >> 4) & 0xF;
		s.kickback = (bits >> 8) & 0xF;
		s.weight = (bits >> 12) & 0xF;
		s.fireRate = (bits >> 16) & 0xF;
		s.ammo = (bits >> 20) & 0xF;
		return s;
	}

	uint8_t GetTotalLevel() const {
		return accuracy + damage + kickback + weight + fireRate + ammo;
	}

	uint8_t GetStatByIndex(int index) const {
		switch (index) {
			case 0: return accuracy;
			case 1: return damage;
			case 2: return kickback;
			case 3: return weight;
			case 4: return fireRate;
			case 5: return ammo;
			default: return 0;
		}
	}

	void SetStatByIndex(int index, uint8_t val) {
		switch (index) {
			case 0: accuracy = val; break;
			case 1: damage = val; break;
			case 2: kickback = val; break;
			case 3: weight = val; break;
			case 4: fireRate = val; break;
			case 5: ammo = val; break;
		}
	}
};

struct StatWeights {
	int accuracy = 100;
	int damage = 100;
	int kickback = 100;
	int weight = 100;
	int fireRate = 100;
	int ammo = 100;

	int GetWeightByIndex(int index) const {
		switch (index) {
			case 0: return accuracy;
			case 1: return damage;
			case 2: return kickback;
			case 3: return weight;
			case 4: return fireRate;
			case 5: return ammo;
			default: return 100;
		}
	}
};

struct WeaponEnhanceLimit {
	std::string name;
	uint8_t accuracy = 0;
	uint8_t damage = 0;
	uint8_t kickback = 0;
	uint8_t weight = 0;
	uint8_t fireRate = 0;
	uint8_t ammo = 0;
	bool hasCustomStatWeights = false;
	StatWeights statWeights;

	uint8_t GetLimitByIndex(int index) const {
		switch (index) {
			case 0: return accuracy;
			case 1: return damage;
			case 2: return kickback;
			case 3: return weight;
			case 4: return fireRate;
			case 5: return ammo;
			default: return 0;
		}
	}
};

struct RefundMaterialWeights {
	int damage = 100;    // 5212 完美傷害強化材料
	int weight = 100;    // 5213 完美重量強化材料
	int accuracy = 100;  // 5214 完美命中強化材料
	int fireRate = 100;  // 5215 完美連發強化材料
	int kickback = 100;  // 5216 完美後座力強化材料
	int ammo = 100;      // 5217 完美彈匣強化材料

	int GetWeightByItemID(unsigned short itemID) const {
		switch (itemID) {
			case 5212: return damage;
			case 5213: return weight;
			case 5214: return accuracy;
			case 5215: return fireRate;
			case 5216: return kickback;
			case 5217: return ammo;
			default: return 0;
		}
	}
};

struct EnhanceSettings {
	uint8_t normalPartMaxLevel = 5;
	uint8_t maxTotalLevel = 8;
	std::vector<int> normalPartSuccessRates = { 90, 80, 70, 50, 30 };
	std::vector<int> advancedPartSuccessRates = { 95, 85, 75, 60, 45, 30, 20, 10 };
	int normalPartLevelDownChance = 0;
	int advancedPartLevelDownChance = 0;

	// 失敗返還完美屬性強化材料配置
	bool failureRefundEnabled = true;
	int failureRefundCount = 1;
	RefundMaterialWeights failureRefundWeights;

	// +8 最高強化成功全服廣播開關
	bool broadcastMaxEnhance = true;
};

class EnhanceManager {
public:
	bool Init();
	bool Reload();

	bool HandleEnhance(User* user, unsigned short weaponSlotID, unsigned short materialSlotID);

	const WeaponEnhanceLimit* GetWeaponLimit(unsigned short itemID) const;
	bool IsEnhanceableWeapon(unsigned short itemID) const;

	void SendEnhanceResult(TCPConnection::pointer connection, unsigned char status, unsigned short weaponSlotID, unsigned long enhanceBits, unsigned char statIndex, const std::string& message = "");

private:
	void BroadcastMaxEnhance(User* user, unsigned short itemID, uint32_t enhanceBits);

	EnhanceSettings _settings;
	std::unordered_map<unsigned short, WeaponEnhanceLimit> _weapons;
};

extern EnhanceManager enhanceManager;
