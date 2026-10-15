#include "battlebonusmanager.h"
#include "serverconsole.h"
#include "nlohmann/json.hpp"
#include "itemdefinitionmanager.h"
#include "user/user.h"
#include "room/gamematchuser.h"

#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;
using namespace std;

BattleBonusManager battleBonusManager;

BattleBonusManager::BattleBonusManager() = default;

bool BattleBonusManager::Init() {
	serverConsole.Print(PrefixType::Info, "[ BattleBonusManager ] Initializing BattleBonus system...\n");
	return LoadConfig("configs/battle_bonus.json");
}

bool BattleBonusManager::LoadConfig(const std::string& configPath) {
	std::string resolvedPath = configPath.empty() ? "configs/battle_bonus.json" : configPath;
	if (!std::filesystem::exists(resolvedPath)) {
		serverConsole.Print(PrefixType::Warn, format("[ BattleBonusManager ] Config file not found: {}\n", resolvedPath));
		return false;
	}

	try {
		std::ifstream file(resolvedPath);
		if (!file.is_open()) {
			serverConsole.Print(PrefixType::Error, format("[ BattleBonusManager ] Could not open file: {}\n", resolvedPath));
			return false;
		}

		json root = json::parse(file);

		_passItems.clear();
		_classes.clear();
		_decorations.clear();
		_pcBangBonus = { 0, 0 };
		_baseEventBonus = { 0, 0 };
		_scheduledEvents.clear();
		_stackGeneralAndZombie = false;

		// 1. Pass items
		if (root.contains("pass_items") && root["pass_items"].is_object()) {
			const auto& pObj = root["pass_items"];
			_stackGeneralAndZombie = pObj.value("stack_general_and_zombie", false);

			auto loadPassArray = [this](const json& arr, const std::string& defaultGroup) {
				if (!arr.is_array()) return;
				for (const auto& itemVal : arr) {
					PassItemBonus p;
					p.itemID = itemVal.value("itemID", (unsigned short)0);
					if (p.itemID == 0) continue;
					p.name = itemVal.value("name", "");
					p.pointPercent = itemVal.value("pointPercent", (unsigned short)0);
					p.expPercent = itemVal.value("expPercent", (unsigned short)0);
					p.exclusiveGroup = itemVal.value("exclusiveGroup", defaultGroup);
					if (itemVal.contains("validModes") && itemVal["validModes"].is_array()) {
						for (const auto& m : itemVal["validModes"]) {
							p.validModes.push_back((unsigned char)m.get<int>());
						}
					}
					_passItems[p.itemID] = p;
				}
			};

			if (pObj.contains("general")) {
				loadPassArray(pObj["general"], "general_pass");
			}
			if (pObj.contains("zombie")) {
				loadPassArray(pObj["zombie"], "zombie_pass");
			}
		}

		// 2. Classes
		if (root.contains("classes") && root["classes"].is_array()) {
			for (const auto& itemVal : root["classes"]) {
				ClassBonus c;
				c.itemID = itemVal.value("itemID", (unsigned short)0);
				if (c.itemID == 0) continue;
				c.name = itemVal.value("name", "");
				c.pointPercent = itemVal.value("pointPercent", (unsigned short)0);
				c.expPercent = itemVal.value("expPercent", (unsigned short)0);
				_classes[c.itemID] = c;
			}
		}

		// 3. Decorations (Tattoos, Costumes)
		if (root.contains("decorations") && root["decorations"].is_array()) {
			for (const auto& itemVal : root["decorations"]) {
				DecorationBonus d;
				d.itemID = itemVal.value("itemID", (unsigned short)0);
				if (d.itemID == 0) continue;
				d.name = itemVal.value("name", "");
				d.pointPercent = itemVal.value("pointPercent", (unsigned short)0);
				d.expPercent = itemVal.value("expPercent", (unsigned short)0);
				_decorations[d.itemID] = d;
			}
		}

		// 4. Global bonus (PCBang & Events)
		if (root.contains("global_bonus") && root["global_bonus"].is_object()) {
			const auto& gObj = root["global_bonus"];
			if (gObj.contains("pcbang") && gObj["pcbang"].is_object()) {
				_pcBangBonus.pointPercent = gObj["pcbang"].value("pointPercent", (unsigned short)0);
				_pcBangBonus.expPercent = gObj["pcbang"].value("expPercent", (unsigned short)0);
			}

			// Base static event bonus
			if (gObj.contains("base_event") && gObj["base_event"].is_object()) {
				_baseEventBonus.pointPercent = gObj["base_event"].value("pointPercent", (unsigned short)0);
				_baseEventBonus.expPercent = gObj["base_event"].value("expPercent", (unsigned short)0);
			}
			else if (gObj.contains("event") && gObj["event"].is_object()) {
				_baseEventBonus.pointPercent = gObj["event"].value("pointPercent", (unsigned short)0);
				_baseEventBonus.expPercent = gObj["event"].value("expPercent", (unsigned short)0);
			}

			// Scheduled dynamic event rules
			auto parseScheduledEvents = [this](const json& evtsArray) {
				if (!evtsArray.is_array()) return;
				for (const auto& evVal : evtsArray) {
					ScheduledEventBonus evt;
					evt.name = evVal.value("name", "");
					evt.pointPercent = evVal.value("pointPercent", (unsigned short)0);
					evt.expPercent = evVal.value("expPercent", (unsigned short)0);
					evt.date = evVal.value("date", "");
					evt.dateRange = evVal.value("dateRange", "");
					evt.timeRange = evVal.value("timeRange", "");
					_scheduledEvents.push_back(evt);
				}
			};

			if (gObj.contains("scheduled_events") && gObj["scheduled_events"].is_array()) {
				parseScheduledEvents(gObj["scheduled_events"]);
			}
			else if (gObj.contains("events") && gObj["events"].is_array()) {
				parseScheduledEvents(gObj["events"]);
			}
		}

		// 5. Map Sequence bonus config (#CSO_MAP_SEQUENCE)
		_mapSeqBonusEnabled = true;
		_mapSeqStartMap = 2;
		_mapSeqStartPercent = 4;
		_mapSeqStepPercent = 2;
		_mapSeqMaxPercent = 10;
		if (root.contains("map_sequence") && root["map_sequence"].is_object()) {
			const auto& mObj = root["map_sequence"];
			_mapSeqBonusEnabled = mObj.value("enabled", true);
			_mapSeqStartMap = mObj.value("start_map", (unsigned short)2);
			_mapSeqStartPercent = mObj.value("start_percent", (unsigned short)4);
			_mapSeqStepPercent = mObj.value("step_percent", (unsigned short)2);
			_mapSeqMaxPercent = mObj.value("max_percent", (unsigned short)10);
		}

		serverConsole.Print(PrefixType::Info, format("[ BattleBonusManager ] Loaded {} pass items, {} classes, {} decorations, {} scheduled events (stackGeneralAndZombie: {}, mapSeq: {})\n",
			_passItems.size(), _classes.size(), _decorations.size(), _scheduledEvents.size(), _stackGeneralAndZombie, _mapSeqBonusEnabled));

		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ BattleBonusManager ] Exception parsing {}: {}\n", resolvedPath, e.what()));
		return false;
	}
}

bool BattleBonusManager::IsEventActive(const ScheduledEventBonus& evt, const tm& localTm) {
	char curDateBuf[32];
	snprintf(curDateBuf, sizeof(curDateBuf), "%04d-%02d-%02d", localTm.tm_year + 1900, localTm.tm_mon + 1, localTm.tm_mday);
	std::string curDate(curDateBuf);

	int curMinutes = localTm.tm_hour * 60 + localTm.tm_min;

	// 1. Specific date: "YYYY-MM-DD"
	if (!evt.date.empty()) {
		if (curDate != evt.date) {
			return false;
		}
	}

	// 2. Date range: "YYYY-MM-DD~YYYY-MM-DD" or "YYYY-MM-DD to YYYY-MM-DD" or "YYYY-MM-DD - YYYY-MM-DD"
	if (!evt.dateRange.empty()) {
		std::string startD, endD;
		size_t sep = evt.dateRange.find('~');
		if (sep != std::string::npos) {
			startD = evt.dateRange.substr(0, sep);
			endD = evt.dateRange.substr(sep + 1);
		}
		else if ((sep = evt.dateRange.find("to")) != std::string::npos) {
			startD = evt.dateRange.substr(0, sep);
			endD = evt.dateRange.substr(sep + 2);
		}
		else if ((sep = evt.dateRange.find(" - ")) != std::string::npos) {
			startD = evt.dateRange.substr(0, sep);
			endD = evt.dateRange.substr(sep + 3);
		}
		else if ((sep = evt.dateRange.find('/')) != std::string::npos) {
			startD = evt.dateRange.substr(0, sep);
			endD = evt.dateRange.substr(sep + 1);
		}

		auto trim = [](std::string s) {
			while (!s.empty() && (s.front() == ' ' || s.front() == '\t')) s.erase(s.begin());
			while (!s.empty() && (s.back() == ' ' || s.back() == '\t')) s.pop_back();
			return s;
		};

		startD = trim(startD);
		endD = trim(endD);

		if (!startD.empty() && curDate < startD) {
			return false;
		}
		if (!endD.empty() && curDate > endD) {
			return false;
		}
	}

	// 3. Time range: "HH:MM-HH:MM" or "HH:MM~HH:MM"
	if (!evt.timeRange.empty()) {
		size_t sep = evt.timeRange.find_first_of("-~");
		if (sep != std::string::npos) {
			std::string startT = evt.timeRange.substr(0, sep);
			std::string endT = evt.timeRange.substr(sep + 1);

			int startH = 0, startM = 0, endH = 0, endM = 0;
			if (sscanf(startT.c_str(), "%d:%d", &startH, &startM) >= 1 &&
				sscanf(endT.c_str(), "%d:%d", &endH, &endM) >= 1) {
				int startMinutes = startH * 60 + startM;
				int endMinutes = endH * 60 + endM;

				if (startMinutes <= endMinutes) {
					if (curMinutes < startMinutes || curMinutes > endMinutes) {
						return false;
					}
				}
				else {
					// Overnight window, e.g. 23:00 to 02:00
					if (curMinutes < startMinutes && curMinutes > endMinutes) {
						return false;
					}
				}
			}
		}
	}

	return true;
}

BattleBonusResult BattleBonusManager::CalculateBonus(User* user, GameMatchUser* gmu, unsigned char gameModeID, unsigned short baseExp, unsigned short basePoints) {
	BattleBonusResult res;
	if (user == nullptr || gmu == nullptr) {
		return res;
	}

	auto inventory = user->GetUserInventory();
	time_t now = time(nullptr);
	struct tm localTm;
	localtime_s(&localTm, &now);

	// 1. Pass item bonuses (Items present in inventory within expirationTime)
	std::unordered_map<std::string, unsigned short> groupMaxPoint;
	std::unordered_map<std::string, unsigned short> groupMaxExp;

	for (const auto& invItem : inventory) {
		if (invItem.expirationTime > 0 && invItem.expirationTime <= (unsigned long)now) {
			continue; // Expired
		}

		auto it = _passItems.find(invItem.itemID);
		if (it == _passItems.end()) {
			continue;
		}

		const auto& pass = it->second;
		// Check mode validity
		if (!pass.validModes.empty()) {
			bool modeValid = (std::find(pass.validModes.begin(), pass.validModes.end(), gameModeID) != pass.validModes.end());
			if (!modeValid) {
				continue;
			}
		}

		// Record max per exclusive group
		groupMaxPoint[pass.exclusiveGroup] = max(groupMaxPoint[pass.exclusiveGroup], pass.pointPercent);
		groupMaxExp[pass.exclusiveGroup] = max(groupMaxExp[pass.exclusiveGroup], pass.expPercent);
	}

	if (!_stackGeneralAndZombie) {
		// Non-stacking: take maximum between general_pass and zombie_pass
		unsigned short genPt = groupMaxPoint["general_pass"];
		unsigned short zombPt = groupMaxPoint["zombie_pass"];
		res.itemPoint = max(genPt, zombPt);

		unsigned short genExp = groupMaxExp["general_pass"];
		unsigned short zombExp = groupMaxExp["zombie_pass"];
		res.itemExp = max(genExp, zombExp);
	}
	else {
		// Stacking: sum general_pass and zombie_pass
		res.itemPoint = groupMaxPoint["general_pass"] + groupMaxPoint["zombie_pass"];
		res.itemExp = groupMaxExp["general_pass"] + groupMaxExp["zombie_pass"];
	}

	// Add any custom groups outside of general_pass and zombie_pass
	for (const auto& kv : groupMaxPoint) {
		if (kv.first != "general_pass" && kv.first != "zombie_pass") {
			res.itemPoint += kv.second;
		}
	}
	for (const auto& kv : groupMaxExp) {
		if (kv.first != "general_pass" && kv.first != "zombie_pass") {
			res.itemExp += kv.second;
		}
	}

	// 2. Class bonus: ONLY take from in-game selection. If not obtained, ignore (no warehouse fallback).
	unsigned short activeClassItemID = gmu->GetClassItemID();
	if (activeClassItemID > 0) {
		auto it = _classes.find(activeClassItemID);
		if (it == _classes.end()) {
			// Check if activeClassItemID was an inGameID (e.g. 26 for Carl CT 253, 27 for Carl TR 254)
			for (const auto& clsPair : _classes) {
				const ItemDefinition* def = itemDefinitionManager.GetItem(clsPair.first);
				if (def && def->inGameID == activeClassItemID) {
					it = _classes.find(clsPair.first);
					break;
				}
			}
		}

		if (it != _classes.end()) {
			res.classPoint = it->second.pointPercent;
			res.classExp = it->second.expPercent;
		}
	}

	// 3. Decoration bonus: at most one equipped decoration applies, no multi-item stacking.
	for (const auto& invItem : inventory) {
		if (!invItem.inUse) continue;
		if (invItem.expirationTime > 0 && invItem.expirationTime <= (unsigned long)now) {
			continue;
		}

		auto it = _decorations.find(invItem.itemID);
		if (it != _decorations.end()) {
			res.decoPoint = it->second.pointPercent;
			res.decoExp = it->second.expPercent;
			break; // 只計算裝備的那一件，不可多件疊加
		}
	}

	// 4. Global PCBang & Event bonus (Base + Scheduled Events)
	res.pcBangPoint = _pcBangBonus.pointPercent;
	res.pcBangExp = _pcBangBonus.expPercent;

	res.eventPoint = _baseEventBonus.pointPercent;
	res.eventExp = _baseEventBonus.expPercent;

	for (const auto& evt : _scheduledEvents) {
		if (IsEventActive(evt, localTm)) {
			res.eventPoint += evt.pointPercent;
			res.eventExp += evt.expPercent;
		}
	}

	// 5. Map Sequence bonus (#CSO_MAP_SEQUENCE)
	// 第2張地圖開始 4%，隨後每多一張圖 +2%，最多5張 10%
	unsigned short mapSeq = gmu->GetMapSequence();
	if (_mapSeqBonusEnabled && mapSeq >= _mapSeqStartMap) {
		unsigned short bonus = _mapSeqStartPercent + (mapSeq - _mapSeqStartMap) * _mapSeqStepPercent;
		if (bonus > _mapSeqMaxPercent) {
			bonus = _mapSeqMaxPercent;
		}
		res.mapSeqPoint = bonus;
		res.mapSeqExp = bonus;
	}

	// 6. Total percentages
	res.totalPointPct = res.pcBangPoint + res.itemPoint + res.classPoint + res.eventPoint + res.decoPoint + res.teamPoint + res.mapSeqPoint;
	res.totalExpPct = res.pcBangExp + res.itemExp + res.classExp + res.eventExp + res.decoExp + res.teamExp + res.mapSeqExp;

	// 7. Bonus Point & Exp values
	res.bonusPoints = (unsigned short)(((unsigned long)basePoints * res.totalPointPct) / 100);
	res.bonusExp = (unsigned short)(((unsigned long)baseExp * res.totalExpPct) / 100);

	serverConsole.Print(PrefixType::Info, format("[ BattleBonus ] User ({}) BattleBonus calculated - Mode: {}, InGameClassID: {}, Item: +{}%/+{}%, Class: +{}%/+{}%, Deco: +{}%/+{}%, Event: +{}%/+{}%, MapSeq(x{}): +{}%/+{}%, Total: +{}%/+{}%, BasePts: {}(+{}), BaseExp: {}(+{})\n",
		user->GetUserLogName(), gameModeID, activeClassItemID,
		res.itemPoint, res.itemExp,
		res.classPoint, res.classExp,
		res.decoPoint, res.decoExp,
		res.eventPoint, res.eventExp,
		mapSeq, res.mapSeqPoint, res.mapSeqExp,
		res.totalPointPct, res.totalExpPct,
		basePoints, res.bonusPoints,
		baseExp, res.bonusExp));

	return res;
}

bool BattleBonusManager::HasClassBonus(unsigned short itemID) const {
	if (_classes.find(itemID) != _classes.end()) {
		return true;
	}
	for (const auto& kv : _classes) {
		const ItemDefinition* def = itemDefinitionManager.GetItem(kv.first);
		if (def && def->inGameID == itemID) {
			return true;
		}
	}
	return false;
}
