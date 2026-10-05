#include "packet_shopmanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_umsgmanager.h"
#include "itemdefinitionmanager.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;

Packet_ShopManager packet_ShopManager;

bool Packet_ShopManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ Packet_ShopManager ] Initializing shop catalog...\n");
	return LoadShopConfig(configPath);
}

bool Packet_ShopManager::LoadShopConfig(const std::string& configPath) {
	std::vector<std::string> searchPaths = {
		configPath,
		"configs/shop.json",
		"configs\\shop.json",
		"bin/configs/shop.json",
		"bin\\configs\\shop.json",
		"shop.json",
		"..\\configs\\shop.json"
	};

	std::ifstream file;
	std::string loadedPath;

	for (const auto& path : searchPaths) {
		if (path.empty()) continue;
		if (std::filesystem::exists(path)) {
			file.open(path);
			if (file.is_open()) {
				loadedPath = path;
				break;
			}
		}
	}

	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, "[ Packet_ShopManager ] Failed to locate shop.json in any search path!\n");
		return false;
	}

	try {
		json j;
		file >> j;

		if (!j.contains("Products") || !j["Products"].is_array()) {
			serverConsole.Print(PrefixType::Error, "[ Packet_ShopManager ] Invalid shop.json: missing 'Products' array!\n");
			return false;
		}

		_products.clear();
		_offersBySubId.clear();

		unsigned int nextSubProductID = 1;

		for (const auto& pJson : j["Products"]) {
			if (!pJson.is_object()) continue;

			ShopProduct product;
			if (pJson.contains("ProductID") && pJson["ProductID"].is_number()) {
				product.productID = pJson["ProductID"].get<unsigned int>();
			}
			else {
				continue;
			}

			if (pJson.contains("Name") && pJson["Name"].is_string()) {
				product.name = pJson["Name"].get<std::string>();
			}

			if (pJson.contains("IsPoints") && pJson["IsPoints"].is_number()) {
				product.isPoints = (pJson["IsPoints"].get<int>() != 0);
			}

			// Parse PackageItems (e.g. for bundles like yurialice2, skull set, etc.)
			auto parsePackageList = [](const json& jArray) -> std::vector<ShopPackageReward> {
				std::vector<ShopPackageReward> list;
				for (const auto& itemJson : jArray) {
					ShopPackageReward reward;
					if (itemJson.is_number()) {
						reward.itemID = (unsigned short)itemJson.get<unsigned int>();
						reward.duration = 0;
						reward.count = 1;
					}
					else if (itemJson.is_object()) {
						if (itemJson.contains("ItemID") && itemJson["ItemID"].is_number()) {
							reward.itemID = (unsigned short)itemJson["ItemID"].get<unsigned int>();
						}
						if (itemJson.contains("Duration") && itemJson["Duration"].is_number()) {
							reward.duration = itemJson["Duration"].get<unsigned short>();
						}
						if (itemJson.contains("Count") && itemJson["Count"].is_number()) {
							reward.count = itemJson["Count"].get<unsigned short>();
						}
					}
					if (reward.itemID > 0) {
						list.push_back(reward);
					}
				}
				return list;
			};

			if (pJson.contains("PackageItems") && pJson["PackageItems"].is_array()) {
				product.packageItems = parsePackageList(pJson["PackageItems"]);
			}
			else if (pJson.contains("Items") && pJson["Items"].is_array()) {
				product.packageItems = parsePackageList(pJson["Items"]);
			}

			const ItemDefinition* itemDef = itemDefinitionManager.GetItem((unsigned short)product.productID);

			if (pJson.contains("Offers") && pJson["Offers"].is_array()) {
				for (const auto& oJson : pJson["Offers"]) {
					if (!oJson.is_object()) continue;

					ShopOffer offer;
					if (oJson.contains("SubProductID") && oJson["SubProductID"].is_number()) {
						offer.subProductID = oJson["SubProductID"].get<unsigned int>();
						if (offer.subProductID >= nextSubProductID) {
							nextSubProductID = offer.subProductID + 1;
						}
					}
					else {
						offer.subProductID = nextSubProductID++;
					}

					if (oJson.contains("Duration") && oJson["Duration"].is_number()) {
						offer.duration = oJson["Duration"].get<unsigned short>();
					}
					if (oJson.contains("Count") && oJson["Count"].is_number()) {
						offer.count = oJson["Count"].get<unsigned short>();
					}
					if (oJson.contains("Price") && oJson["Price"].is_number()) {
						offer.price = oJson["Price"].get<unsigned int>();
					}
					if (oJson.contains("AdditionalPoints") && oJson["AdditionalPoints"].is_number()) {
						offer.additionalPoints = oJson["AdditionalPoints"].get<unsigned int>();
					}
					if (oJson.contains("AdditionalClanPoints") && oJson["AdditionalClanPoints"].is_number()) {
						offer.additionalClanPoints = oJson["AdditionalClanPoints"].get<unsigned int>();
					}
					if (oJson.contains("AdType") && oJson["AdType"].is_number()) {
						offer.adType = oJson["AdType"].get<unsigned char>();
					}

					// Inherit package items or allow per-offer override
					offer.packageItems = product.packageItems;
					if (oJson.contains("PackageItems") && oJson["PackageItems"].is_array()) {
						offer.packageItems = parsePackageList(oJson["PackageItems"]);
					}
					else if (oJson.contains("Items") && oJson["Items"].is_array()) {
						offer.packageItems = parsePackageList(oJson["Items"]);
					}

					// Check isCount
					if (oJson.contains("IsCount") && oJson["IsCount"].is_number()) {
						offer.isCount = (unsigned char)oJson["IsCount"].get<int>();
					}
					else {
						if (!offer.packageItems.empty()) {
							offer.isCount = 0;
						}
						else if (offer.duration == 0 && (offer.count > 1 || (itemDef != nullptr && !itemDef->isPeriod))) {
							offer.isCount = 1;
						}
						else {
							offer.isCount = 0;
						}
					}

					offer.productID = product.productID;
					offer.productName = product.name;
					offer.isPoints = product.isPoints;

					product.offers.push_back(offer);
					_offersBySubId[offer.subProductID] = offer;
				}
			}

			_products.push_back(product);
		}

		serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] Loaded {} shop products ({} offers) from {}\n",
			_products.size(), _offersBySubId.size(), loadedPath));
		return true;
	}
	catch (const std::exception& e) {
		serverConsole.Print(PrefixType::Error, format("[ Packet_ShopManager ] Error loading shop config: {}\n", e.what()));
		return false;
	}
}

void Packet_ShopManager::ParsePacket_Shop(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] Client ({}) has sent Packet_Shop, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	unsigned char type = packet->ReadUInt8();

	switch (type) {
		case Packet_ShopType::RequestShopList: {
			parsePacket_Shop_RequestShopList(user);
			break;
		}
		case Packet_ShopType::Buy: {
			parsePacket_Shop_Buy(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] User ({}) has sent unregistered Packet_Shop type {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_ShopManager::parsePacket_Shop_RequestShopList(User* user) {
	if (user == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) has requested shop list\n", user->GetUserLogName()));

	SendPacket_Shop_ShopList(connection);
}

void Packet_ShopManager::parsePacket_Shop_Buy(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned int subProductID = packet->ReadUInt32_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) requests purchase of subProductID {}\n",
		user->GetUserLogName(), subProductID));

	auto it = _offersBySubId.find(subProductID);
	if (it == _offersBySubId.end()) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] SubProductID {} not found in shop catalog!\n", subProductID));
		sendPacket_Shop_BuyReply(connection, 2, "Product offer not found");
		return;
	}

	const ShopOffer& offer = it->second;
	unsigned long userID = user->GetUserID();

	// Deduct currency atomically from database
	if (!databaseManager.DeductUserFunds(userID, offer.isPoints, offer.price)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] User ({}) has insufficient {} for product '{}' (offer {}, price {})\n",
			user->GetUserLogName(), offer.isPoints ? "Points" : "Cash", offer.productName, offer.subProductID, offer.price));
		sendPacket_Shop_BuyReply(connection, 3, "Insufficient funds");
		return;
	}

	// Update user inventory
	auto currentInventory = user->GetUserInventory();
	time_t now = time(nullptr);

	// Check if this offer is a package/bundle
	if (!offer.packageItems.empty()) {
		unsigned short maxSlotID = 0;
		for (const auto& item : currentInventory) {
			if (item.slotID > maxSlotID) {
				maxSlotID = item.slotID;
			}
		}

		std::vector<InventoryItem> itemsToAdd;
		std::vector<std::string> rewardedNames;

		for (const auto& reward : offer.packageItems) {
			const ItemDefinition* itemDef = itemDefinitionManager.GetItem(reward.itemID);
			std::string itemName = (itemDef != nullptr) ? itemDef->name : format("ID:{}", reward.itemID);
			rewardedNames.push_back(itemName);

			bool itemOwned = false;
			InventoryItem ownedItem;
			for (const auto& item : currentInventory) {
				if (item.itemID == reward.itemID) {
					itemOwned = true;
					ownedItem = item;
					break;
				}
			}

			bool isPeriod = (itemDef != nullptr) ? itemDef->isPeriod : (reward.duration > 0 || (reward.duration == 0 && reward.count == 1));

			if (isPeriod) {
				unsigned short effectiveDuration = reward.duration;
				if (offer.duration > 0 && (reward.duration <= 1 || reward.duration == offer.duration)) {
					effectiveDuration = offer.duration;
				}

				if (itemOwned) {
					if (ownedItem.expirationTime == 0) {
						// Already permanent
					}
					else if (effectiveDuration == 0 || effectiveDuration == 0xFFFF) {
						// Upgrade to permanent
						databaseManager.UpdateUserInventoryExpiration(userID, reward.itemID, 0);
					}
					else {
						// Extend expiration
						unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
						unsigned long newExp = baseTime + (unsigned long)effectiveDuration * 86400;
						databaseManager.UpdateUserInventoryExpiration(userID, reward.itemID, newExp);
					}
				}
				else {
					InventoryItem newItem;
					newItem.slotID = ++maxSlotID;
					newItem.itemID = reward.itemID;
					newItem.count = 1;
					newItem.activeFlag = 1;
					newItem.inUse = false;
					newItem.obtainTime = (unsigned long)now;
					newItem.expirationTime = (effectiveDuration == 0 || effectiveDuration == 0xFFFF) ? 0 : (unsigned long)(now + (unsigned long)effectiveDuration * 86400);
					newItem.enhanceBits = 0;
					itemsToAdd.push_back(newItem);
					currentInventory.push_back(newItem);
				}
			}
			else {
				// Count-based
				if (itemOwned) {
					databaseManager.UpdateUserInventoryItemCount(userID, reward.itemID, reward.count);
				}
				else {
					InventoryItem newItem;
					newItem.slotID = ++maxSlotID;
					newItem.itemID = reward.itemID;
					newItem.count = reward.count;
					newItem.activeFlag = 1;
					newItem.inUse = false;
					newItem.obtainTime = (unsigned long)now;
					newItem.expirationTime = 0;
					newItem.enhanceBits = 0;
					itemsToAdd.push_back(newItem);
					currentInventory.push_back(newItem);
				}
			}
		}

		if (!itemsToAdd.empty()) {
			databaseManager.AddUserInventoryItems(userID, itemsToAdd);
		}

		// Process bonus points rewards
		if (offer.additionalPoints > 0) {
			databaseManager.AddUserFunds(userID, true, offer.additionalPoints);
		}

		// 1. Send successful BuyReply to client
		sendPacket_Shop_BuyReply(connection, 1, "");

		// 2. Immediately sync updated inventory to client (Packet 154)
		packetManager.SendPacket_Inventory(connection, user->GetUserInventory());

		// 3. Immediately sync updated Points and Cash balance to client HUD (Packet 157 & 69)
		auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });

		// 4. Send informative chat notice
		std::string currencyName = offer.isPoints ? "Points" : "GASH";
		std::string itemsStr;
		for (size_t i = 0; i < rewardedNames.size(); ++i) {
			if (i > 0) itemsStr += "、";
			itemsStr += rewardedNames[i];
		}
		std::string durationNotice = "";
		if (offer.duration > 0) {
			durationNotice = format(" ({}日)", offer.duration);
		}
		std::string bonusNotice;
		if (offer.additionalPoints > 0) {
			bonusNotice += format("，額外獲贈 +{} Points", offer.additionalPoints);
		}
		if (offer.additionalClanPoints > 0) {
			bonusNotice += format("，額外獲贈 +{} 軍團點數", offer.additionalClanPoints);
		}
		std::string notice = format("[系統] 成功購買「{}」{}！獲得：[{}]。扣除 {} {}{}。",
			offer.productName, durationNotice, itemsStr, offer.price, currencyName, bonusNotice);
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, notice);

		serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) successfully purchased package offer {} ('{}'{}, [{}], {} {}{})\n",
			user->GetUserLogName(), offer.subProductID, offer.productName, durationNotice, itemsStr, offer.price, currencyName, bonusNotice));
		return;
	}

	// Single item purchase
	bool alreadyOwned = false;
	InventoryItem ownedItem;
	unsigned short maxSlotID = 0;

	for (const auto& item : currentInventory) {
		if (item.slotID > maxSlotID) {
			maxSlotID = item.slotID;
		}
		if (item.itemID == (unsigned short)offer.productID) {
			alreadyOwned = true;
			ownedItem = item;
		}
	}

	if (offer.isCount) {
		if (alreadyOwned) {
			databaseManager.UpdateUserInventoryItemCount(userID, (unsigned short)offer.productID, offer.count);
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = (unsigned short)offer.productID;
			newItem.count = offer.count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}
	else {
		// Duration-based equipment
		bool isPermOffer = (offer.duration == 0 || offer.duration == 0xFFFF);
		if (alreadyOwned) {
			if (ownedItem.expirationTime == 0) {
				// Already permanent, keep permanent
			}
			else if (isPermOffer) {
				// Upgrade to permanent
				databaseManager.UpdateUserInventoryExpiration(userID, (unsigned short)offer.productID, 0);
			}
			else {
				// Extend expiration
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)offer.duration * 86400;
				databaseManager.UpdateUserInventoryExpiration(userID, (unsigned short)offer.productID, newExp);
			}
		}
		else {
			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = (unsigned short)offer.productID;
			newItem.count = 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = isPermOffer ? 0 : (unsigned long)(now + (unsigned long)offer.duration * 86400);
			newItem.enhanceBits = 0;
			databaseManager.AddUserInventoryItems(userID, { newItem });
		}
	}

	// Process bonus points rewards
	if (offer.additionalPoints > 0) {
		databaseManager.AddUserFunds(userID, true, offer.additionalPoints);
	}

	// 1. Send successful BuyReply to client
	sendPacket_Shop_BuyReply(connection, 1, "");

	// 2. Immediately sync updated inventory to client (Packet 154)
	packetManager.SendPacket_Inventory(connection, user->GetUserInventory());

	// 3. Immediately sync updated Points and Cash balance to client HUD (Packet 157 & 69)
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });

	// 4. Send informative chat notice to user
	std::string currencyName = offer.isPoints ? "Points" : "GASH";
	bool isPerm = (offer.duration == 0 || offer.duration == 0xFFFF);
	std::string durationText = (offer.isCount) ? format("{} 個", offer.count) : (isPerm ? "永久" : format("{} 天", offer.duration));
	std::string bonusNotice;
	if (offer.additionalPoints > 0) {
		bonusNotice += format("，額外獲贈 +{} Points", offer.additionalPoints);
	}
	if (offer.additionalClanPoints > 0) {
		bonusNotice += format("，額外獲贈 +{} 軍團點數", offer.additionalClanPoints);
	}
	std::string notice = format("[系統] 成功購買「{}」({})！扣除 {} {}{}。", offer.productName, durationText, offer.price, currencyName, bonusNotice);
	packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, notice);

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) successfully purchased offer {} ('{}', {}, {} {}{})\n",
		user->GetUserLogName(), offer.subProductID, offer.productName, durationText, offer.price, currencyName, bonusNotice));
}

void Packet_ShopManager::SendPacket_Shop_ShopList(TCPConnection::pointer connection) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Shop });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_ShopType::ShopList);
	packet->WriteUInt8((unsigned char)std::min(_products.size(), (size_t)255));

	for (size_t i = 0; i < std::min(_products.size(), (size_t)255); i++) {
		const auto& product = _products[i];
		packet->WriteUInt32_LE(product.productID);
		packet->WriteUInt8(product.isPoints ? 1 : 0);
		packet->WriteUInt8((unsigned char)std::min(product.offers.size(), (size_t)255));

		for (const auto& offer : product.offers) {
			packet->WriteUInt32_LE(offer.subProductID);
			unsigned short sendDuration = offer.duration;
			if (!offer.isCount && (offer.duration == 0 || offer.duration == 0xFFFF)) {
				sendDuration = 0xFFFF; // 0xFFFF triggers client "#CSO_Permanent" display
			}
			packet->WriteUInt16_LE(sendDuration);
			packet->WriteUInt8(offer.isCount);
			packet->WriteUInt16_LE(offer.count);
			packet->WriteUInt32_LE(offer.price);
			packet->WriteUInt32_LE(offer.additionalPoints);
			packet->WriteUInt32_LE(offer.additionalClanPoints);
			packet->WriteUInt8(offer.adType);
		}
	}

	packet->Send();
}

void Packet_ShopManager::sendPacket_Shop_BuyReply(TCPConnection::pointer connection, unsigned char status, const std::string& message) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Shop });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_ShopType::BuyReply);
	packet->WriteUInt8(status);
	packet->WriteString(message);

	if (status == 1) {
		packet->WriteUInt16_LE(0);
	}

	packet->Send();
}

void Packet_ShopManager::BroadcastShopList() {
	for (User* user : userManager.GetUsers()) {
		if (user != NULL && userManager.IsUserLoggedIn(user)) {
			SendPacket_Shop_ShopList(user->GetConnection());
		}
	}
}
