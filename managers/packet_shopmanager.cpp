#include "packet_shopmanager.h"
#include "usermanager.h"
#include "serverconsole.h"
#include "databasemanager.h"
#include "packetmanager.h"
#include "packet_updateinfomanager.h"
#include "packet_umsgmanager.h"
#include "itemdefinitionmanager.h"
#include "serverconfig.h"
#include "nlohmann/json.hpp"
#include <fstream>
#include <filesystem>
#include <format>
#include <algorithm>
#include <ctime>

using json = nlohmann::json;

Packet_ShopManager packet_ShopManager;

const ShopProduct* Packet_ShopManager::GetProductByID(unsigned int productID) const {
	for (const auto& prod : _products) {
		if (prod.productID == productID) {
			return &prod;
		}
	}
	return nullptr;
}

bool Packet_ShopManager::Init(const std::string& configPath) {
	serverConsole.Print(PrefixType::Info, "[ Packet_ShopManager ] Initializing shop catalog...\n");
	return LoadShopConfig(configPath);
}

bool Packet_ShopManager::LoadShopConfig(const std::string& configPath) {
	std::string loadedPath = configPath.empty() ? "configs/shop.json" : configPath;
	std::ifstream file(loadedPath);
	if (!file.is_open()) {
		serverConsole.Print(PrefixType::Error, format("[ Packet_ShopManager ] Failed to open shop config: {}\n", loadedPath));
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

			if (pJson.contains("CanGift")) {
				if (pJson["CanGift"].is_boolean()) {
					product.canGift = pJson["CanGift"].get<bool>();
				}
				else if (pJson["CanGift"].is_number()) {
					product.canGift = (pJson["CanGift"].get<int>() != 0);
				}
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

					offer.canGift = product.canGift;
					if (oJson.contains("CanGift")) {
						if (oJson["CanGift"].is_boolean()) {
							offer.canGift = oJson["CanGift"].get<bool>();
						}
						else if (oJson["CanGift"].is_number()) {
							offer.canGift = (oJson["CanGift"].get<int>() != 0);
						}
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
		case Packet_ShopType::RequestGiftList: {
			parsePacket_Shop_RequestGiftList(user);
			break;
		}
		case Packet_ShopType::Buy: {
			parsePacket_Shop_Buy(user, packet);
			break;
		}
		case Packet_ShopType::Gift: {
			parsePacket_Shop_Gift(user, packet);
			break;
		}
		case Packet_ShopType::ClaimGift: {
			parsePacket_Shop_ClaimGift(user, packet);
			break;
		}
		case Packet_ShopType::ViewGift: {
			parsePacket_Shop_ViewGift(user, packet);
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
		sendPacket_Shop_BuyReply(connection, 0, "BUY_FAIL_NOITEM");
		return;
	}

	const ShopOffer& offer = it->second;
	unsigned long userID = user->GetUserID();

	// Deduct currency atomically from database
	if (!databaseManager.DeductUserFunds(userID, offer.isPoints, offer.price)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] User ({}) has insufficient {} for product '{}' (offer {}, price {})\n",
			user->GetUserLogName(), offer.isPoints ? "Points" : "Cash", offer.productName, offer.subProductID, offer.price));
		sendPacket_Shop_BuyReply(connection, 0, offer.isPoints ? "BUY_FAIL_NO_POINT" : "BUY_FAIL_NO_CASH");
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
		std::vector<InventoryItem> deltaUpdatedItems;

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
						ownedItem.expirationTime = 0;
						deltaUpdatedItems.push_back(ownedItem);
					}
					else {
						// Extend expiration
						unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
						unsigned long newExp = baseTime + (unsigned long)effectiveDuration * 86400;
						databaseManager.UpdateUserInventoryExpiration(userID, reward.itemID, newExp);
						ownedItem.expirationTime = newExp;
						deltaUpdatedItems.push_back(ownedItem);
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
					deltaUpdatedItems.push_back(newItem);
				}
			}
			else {
				// Count-based
				if (itemOwned) {
					databaseManager.UpdateUserInventoryItemCount(userID, reward.itemID, reward.count);
					ownedItem.count += reward.count;
					deltaUpdatedItems.push_back(ownedItem);
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
					deltaUpdatedItems.push_back(newItem);
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
		std::vector<unsigned short> purchasedItemIDs;
		for (const auto& reward : offer.packageItems) {
			purchasedItemIDs.push_back(reward.itemID);
		}
		sendPacket_Shop_BuyReply(connection, 1, "", purchasedItemIDs);

		// 2. Immediately sync updated inventory to client (Delta 增量同步)
		packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, {});

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
	std::vector<InventoryItem> deltaUpdatedItems;

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
			ownedItem.count += offer.count;
			deltaUpdatedItems.push_back(ownedItem);
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
			deltaUpdatedItems.push_back(newItem);
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
				ownedItem.expirationTime = 0;
				deltaUpdatedItems.push_back(ownedItem);
			}
			else {
				// Extend expiration
				unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
				unsigned long newExp = baseTime + (unsigned long)offer.duration * 86400;
				databaseManager.UpdateUserInventoryExpiration(userID, (unsigned short)offer.productID, newExp);
				ownedItem.expirationTime = newExp;
				deltaUpdatedItems.push_back(ownedItem);
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
			deltaUpdatedItems.push_back(newItem);
		}
	}

	// Process bonus points rewards
	if (offer.additionalPoints > 0) {
		databaseManager.AddUserFunds(userID, true, offer.additionalPoints);
	}

	// 1. Send successful BuyReply to client
	sendPacket_Shop_BuyReply(connection, 1, "", { (unsigned short)offer.productID });

	// 2. Immediately sync updated inventory to client (Delta 增量同步)
	packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, {});

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
			unsigned char sendAdType = (offer.adType & 0x07);
			if (offer.canGift) {
				sendAdType |= 0x20;
			}
			packet->WriteUInt8(sendAdType);
		}
	}

	packet->Send();
}

void Packet_ShopManager::sendPacket_Shop_BuyReply(TCPConnection::pointer connection, unsigned char status, const std::string& message, const std::vector<unsigned short>& itemIDs) {
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
		packet->WriteUInt16_LE((unsigned short)itemIDs.size());
		for (unsigned short id : itemIDs) {
			packet->WriteUInt16_LE(id);
		}
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

void Packet_ShopManager::parsePacket_Shop_RequestGiftList(User* user) {
	if (user == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) has requested gift list\n", user->GetUserLogName()));
	SendPacket_Shop_GiftList(connection, user);
}

void Packet_ShopManager::SendPacket_Shop_GiftList(TCPConnection::pointer connection, User* user) {
	if (connection == NULL || user == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Shop });
	if (packet == NULL) {
		return;
	}

	auto gifts = databaseManager.GetUserGifts(user->GetUserID());

	packet->WriteUInt8(Packet_ShopType::GiftList); // 3
	packet->WriteUInt8(1); // isSuccess = 1 (success)
	packet->WriteUInt16_LE((unsigned short)gifts.size());

	for (const auto& gift : gifts) {
		packet->WriteUInt32_LE(gift.giftID);
		packet->WriteUInt32_LE(0); // giftID_high
		packet->WriteUInt32_LE(gift.productID);
		packet->WriteUInt16_LE(1); // Field 4: gift box entry count (1 entry per gift record)
		unsigned short sendDuration = gift.duration;
		if (gift.isCount) {
			sendDuration = 0; // count-based consumable
		}
		else if (gift.duration == 0 || gift.duration == 0xFFFF) {
			sendDuration = 0xFFFF; // #CSO_Permanent
		}
		packet->WriteUInt16_LE(sendDuration);
		packet->WriteUInt8(gift.isCount);
		packet->WriteUInt16_LE(gift.count);
		packet->WriteUInt8(0); // unk1
		packet->WriteUInt8(0); // unk2
		packet->WriteString(gift.senderNickName);

		std::string encodedMsg = "\x01\x01" + format("{:010d}", gift.createTime) + gift.message;
		packet->WriteUInt16_LE((unsigned short)encodedMsg.size());
		for (unsigned char c : encodedMsg) {
			packet->WriteUInt8(c);
		}
	}

	packet->Send();
}

void Packet_ShopManager::parsePacket_Shop_Gift(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned int subProductID = packet->ReadUInt32_LE();
	std::string receiverNickName = packet->ReadString();
	unsigned short messageLen = packet->ReadUInt16_LE();
	std::string giftMessage = "";
	if (messageLen > 0) {
		giftMessage = packet->ReadString();
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) requests gift of subProductID {} to '{}' (message: '{}')\n",
		user->GetUserLogName(), subProductID, Cp950ToUtf8(receiverNickName), Cp950ToUtf8(giftMessage)));

	auto it = _offersBySubId.find(subProductID);
	if (it == _offersBySubId.end()) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] SubProductID {} not found in shop catalog!\n", subProductID));
		sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_NOITEM");
		return;
	}

	const ShopOffer& offer = it->second;

	// Check if this offer is giftable
	if (!offer.canGift) {
		sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_NO_GIFT");
		return;
	}

	// 1. Sender level check: Point gift requires level >= serverConfig.shop.giftLevelLimit
	auto senderChar = user->GetUserCharacter(USERCHARACTER_FLAG_LEVEL | USERCHARACTER_FLAG_NICKNAME);
	unsigned char senderLevel = senderChar.userCharacter.level;
	std::string senderNickName = senderChar.userCharacter.nickName;

	if (offer.isPoints && senderLevel < serverConfig.shop.giftLevelLimit) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] User ({}, level {}) cannot gift Point item: level < {} limit!\n",
			user->GetUserLogName(), senderLevel, serverConfig.shop.giftLevelLimit));
		sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_GIFT_LEVEL_LIMIT");
		return;
	}

	// 2. Receiver existence check (supports gifting to other players or to oneself)
	if (receiverNickName.empty()) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] User ({}) attempted to gift to empty receiver!\n", user->GetUserLogName()));
		sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_NICKNAME_NOEXIST");
		return;
	}

	User* targetUser = nullptr;
	unsigned long targetUserID = 0;
	unsigned char targetLevel = 0;

	if (receiverNickName == senderNickName) {
		targetUser = user;
		targetUserID = user->GetUserID();
		targetLevel = senderLevel;
	}
	else {
		for (User* u : userManager.GetUsers()) {
			if (u != nullptr && userManager.IsUserLoggedIn(u)) {
				auto uChar = u->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME | USERCHARACTER_FLAG_LEVEL);
				if (uChar.userCharacter.nickName == receiverNickName) {
					targetUser = u;
					targetUserID = u->GetUserID();
					targetLevel = uChar.userCharacter.level;
					break;
				}
			}
		}

		if (targetUser == nullptr) {
			if (!databaseManager.GetUserByNickName(receiverNickName, targetUserID, targetLevel)) {
				serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] Gift receiver '{}' not found in database!\n", Cp950ToUtf8(receiverNickName)));
				sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_NICKNAME_NOEXIST");
				return;
			}
			targetUser = userManager.GetUserByUserID(targetUserID);
		}
	}

	// 3. Receiver level check: must meet gift level limit and item level requirement
	if (targetLevel < serverConfig.shop.giftLevelLimit) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] Receiver '{}' (level {}) does not meet giftLevelLimit ({})!\n",
			Cp950ToUtf8(receiverNickName), targetLevel, serverConfig.shop.giftLevelLimit));
		sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_RECEIVER_LOW_LEVEL");
		return;
	}

	bool receiverLevelSufficient = true;
	if (!offer.packageItems.empty()) {
		for (const auto& reward : offer.packageItems) {
			const ItemDefinition* itemDef = itemDefinitionManager.GetItem(reward.itemID);
			if (itemDef != nullptr && itemDef->level > 0 && targetLevel < itemDef->level) {
				receiverLevelSufficient = false;
				break;
			}
		}
	}
	else {
		const ItemDefinition* itemDef = itemDefinitionManager.GetItem((unsigned short)offer.productID);
		if (itemDef != nullptr && itemDef->level > 0 && targetLevel < itemDef->level) {
			receiverLevelSufficient = false;
		}
	}

	if (!receiverLevelSufficient) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] Receiver '{}' (level {}) does not meet item level requirement!\n",
			Cp950ToUtf8(receiverNickName), targetLevel));
		sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_RECEIVER_LOW_LEVEL");
		return;
	}

	// 4. Deduct sender funds atomically from database
	if (!databaseManager.DeductUserFunds(user->GetUserID(), offer.isPoints, offer.price)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] User ({}) has insufficient {} for gift offer {} (price {})\n",
			user->GetUserLogName(), offer.isPoints ? "Points" : "Cash", offer.subProductID, offer.price));
		sendPacket_Shop_GiftReply(connection, 0, offer.isPoints ? "BUY_FAIL_NO_POINT" : "BUY_FAIL_NO_CASH");
		return;
	}

	// 5. Save gift into user_gifts database table (receiver's Gift Box)
	UserGift gift;
	gift.receiverUserID = targetUserID;
	gift.senderUserID = user->GetUserID();
	gift.senderNickName = senderNickName;
	gift.productID = offer.productID;
	gift.itemID = (!offer.packageItems.empty()) ? 0 : (unsigned short)offer.productID;
	gift.duration = offer.duration;
	gift.isCount = offer.isCount;
	gift.count = offer.count;
	gift.message = giftMessage;
	gift.createTime = (unsigned int)time(nullptr);
	gift.claimed = 0;

	unsigned int newGiftID = 0;
	if (!databaseManager.AddUserGift(gift, newGiftID)) {
		serverConsole.Print(PrefixType::Error, format("[ Packet_ShopManager ] Failed to add gift to database for user ({})\n", user->GetUserLogName()));
		databaseManager.AddUserFunds(user->GetUserID(), offer.isPoints, offer.price); // refund
		sendPacket_Shop_GiftReply(connection, 0, "BUY_FAIL_UNKNOWN");
		return;
	}
	gift.giftID = newGiftID;

	// 6. Send successful GiftReply to sender client
	sendPacket_Shop_GiftReply(connection, 1, "GIFT_OK");

	// 7. Immediately sync sender's Points and Cash balance (Packet 157 & 69)
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });

	// 9. Send notices to sender and receiver
	std::string currencyName = offer.isPoints ? "Points" : "GASH";
	bool isPerm = (offer.duration == 0 || offer.duration == 0xFFFF);
	std::string durationText = (offer.isCount) ? format("{} 個", offer.count) : (isPerm ? "永久" : format("{} 天", offer.duration));
	std::string recNickUtf8 = Cp950ToUtf8(receiverNickName);
	std::string sendNickUtf8 = Cp950ToUtf8(senderNickName);
	std::string giftMsgUtf8 = Cp950ToUtf8(giftMessage);

	bool isSelfGift = (targetUserID == user->GetUserID());
	if (isSelfGift) {
		std::string selfNotice = format("[系統] 成功贈送「{}」({}) 給自己！已放入您的禮物盒。扣除 {} {}。",
			offer.productName, durationText, offer.price, currencyName);
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, selfNotice);

		// Live push gift to user's own GiftBox (Packet 72 SubType 5)
		sendPacket_Shop_GiftPush(connection, gift);
	}
	else {
		std::string senderNotice = format("[系統] 成功贈送「{}」({}) 給「{}」！扣除 {} {}。",
			offer.productName, durationText, recNickUtf8, offer.price, currencyName);
		packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, senderNotice);

		if (targetUser != nullptr) {
			// Live push gift to online receiver's GiftBox (Packet 72 SubType 5)
			sendPacket_Shop_GiftPush(targetUser->GetConnection(), gift);

			std::string receiverNotice = format("[系統] 收到來自「{}」贈送的禮物：「{}」({})！已放入您的禮物盒。{}",
				sendNickUtf8, offer.productName, durationText,
				giftMsgUtf8.empty() ? "" : format(" 留言：「{}」", giftMsgUtf8));
			packet_UMsgManager.SendPacket_UMsg_PublicNotice(targetUser->GetConnection(), receiverNotice);
		}
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) successfully gifted offer {} ('{}', {}) to '{}' (userID: {}, giftID: {})\n",
		user->GetUserLogName(), offer.subProductID, offer.productName, durationText, recNickUtf8, targetUserID, gift.giftID));
}

void Packet_ShopManager::sendPacket_Shop_GiftReply(TCPConnection::pointer connection, unsigned char status, const std::string& message) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Shop });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_ShopType::GiftReply);
	packet->WriteUInt8(status);
	packet->WriteString(message);
	packet->Send();
}

void Packet_ShopManager::sendPacket_Shop_GiftPush(TCPConnection::pointer connection, const UserGift& gift) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Shop });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_ShopType::GiftPush); // 5
	packet->WriteUInt32_LE(gift.giftID);
	packet->WriteUInt32_LE(0); // giftID_high
	packet->WriteUInt32_LE(gift.productID);
	packet->WriteUInt16_LE(1); // Field 4: gift box entry count (1 entry per gift record)
	unsigned short sendDuration = gift.duration;
	if (gift.isCount) {
		sendDuration = 0; // count-based consumable
	}
	else if (gift.duration == 0 || gift.duration == 0xFFFF) {
		sendDuration = 0xFFFF; // #CSO_Permanent
	}
	packet->WriteUInt16_LE(sendDuration);
	packet->WriteUInt8(gift.isCount);
	packet->WriteUInt16_LE(gift.count); // Field 7: amount of item
	packet->WriteUInt8(0); // unk1
	packet->WriteUInt8(0); // unk2
	packet->WriteString(gift.senderNickName);

	std::string encodedMsg = "\x01\x01" + format("{:010d}", gift.createTime) + gift.message;
	packet->WriteUInt16_LE((unsigned short)encodedMsg.size());
	for (unsigned char c : encodedMsg) {
		packet->WriteUInt8(c);
	}

	packet->Send();
}

void Packet_ShopManager::parsePacket_Shop_ClaimGift(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	unsigned int giftID_low = packet->ReadUInt32_LE();
	unsigned int giftID_high = packet->ReadUInt32_LE();
	unsigned short claimCount = packet->ReadUInt16_LE();
	if (claimCount == 0) {
		claimCount = 1;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) requests claim giftID {} (count {})\n",
		user->GetUserLogName(), giftID_low, claimCount));

	UserGift gift;
	if (!databaseManager.GetUserGiftByID(giftID_low, user->GetUserID(), gift) || gift.claimed != 0) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_ShopManager ] User ({}) attempted to claim invalid/already claimed giftID {}!\n",
			user->GetUserLogName(), giftID_low));
		sendPacket_Shop_ClaimGiftReply(connection, 0, giftID_low, giftID_high, 0, {}, "BUY_FAIL_NOITEM");
		return;
	}

	unsigned short remainingCount = 0;
	if (!databaseManager.ClaimUserGift(giftID_low, user->GetUserID(), gift.count, remainingCount)) {
		serverConsole.Print(PrefixType::Error, format("[ Packet_ShopManager ] Failed to claim giftID {} for user ({}) in database!\n",
			giftID_low, user->GetUserLogName()));
		sendPacket_Shop_ClaimGiftReply(connection, 0, giftID_low, giftID_high, 0, {}, "BUY_FAIL_UNKNOWN");
		return;
	}

	std::vector<unsigned short> awardedItemIDs;
	std::vector<InventoryItem> deltaUpdatedItems;
	grantProductRewardToUser(user->GetUserID(), gift.productID, gift.duration, gift.isCount, gift.count, awardedItemIDs, &deltaUpdatedItems);

	// Resolve bonus Points / ClanPoints from product offer
	const ShopProduct* prod = GetProductByID(gift.productID);
	unsigned int additionalPoints = 0;
	unsigned int additionalClanPoints = 0;

	if (prod != nullptr) {
		for (const auto& offer : prod->offers) {
			if (offer.duration == gift.duration && offer.isCount == gift.isCount) {
				additionalPoints = offer.additionalPoints;
				additionalClanPoints = offer.additionalClanPoints;
				break;
			}
		}
		if (additionalPoints == 0 && additionalClanPoints == 0 && !prod->offers.empty()) {
			if (prod->offers.size() == 1) {
				additionalPoints = prod->offers[0].additionalPoints;
				additionalClanPoints = prod->offers[0].additionalClanPoints;
			}
			else {
				for (const auto& offer : prod->offers) {
					if (offer.duration == gift.duration) {
						additionalPoints = offer.additionalPoints;
						additionalClanPoints = offer.additionalClanPoints;
						break;
					}
				}
			}
		}
	}

	if (gift.isCount && gift.count > 1) {
		additionalPoints *= gift.count;
		additionalClanPoints *= gift.count;
	}

	if (additionalPoints > 0) {
		databaseManager.AddUserFunds(user->GetUserID(), true, additionalPoints);
	}

	// Send ClaimGiftReply (Packet 72 SubType 4): client expects claimedCount (claimCount = 1) to remove gift from Gift Box
	sendPacket_Shop_ClaimGiftReply(connection, 1, giftID_low, giftID_high, claimCount, awardedItemIDs);

	// Sync updated inventory immediately to client (Delta 增量同步)
	packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, {});

	// Immediately sync updated Points and Cash balance to client HUD (Packet 157 & 69)
	auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
	packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });

	// Notice to user
	std::string prodName = "";
	if (prod != nullptr) {
		prodName = prod->name;
	}
	else {
		const ItemDefinition* itemDef = itemDefinitionManager.GetItem((unsigned short)gift.productID);
		prodName = (itemDef != nullptr) ? itemDef->name : format("道具 ID:{}", gift.productID);
	}
	bool isPerm = (gift.duration == 0 || gift.duration == 0xFFFF);
	std::string durationText = (gift.isCount) ? format("{} 個", gift.count) : (isPerm ? "永久" : format("{} 天", gift.duration));
	std::string senderNickUtf8 = Cp950ToUtf8(gift.senderNickName);
	std::string bonusNotice;
	if (additionalPoints > 0) {
		bonusNotice += format("，額外獲贈 +{} Points", additionalPoints);
	}
	if (additionalClanPoints > 0) {
		bonusNotice += format("，額外獲贈 +{} 軍團點數", additionalClanPoints);
	}
	std::string notice = format("[系統] 成功領取「{}」贈送的「{}」({})！已放入您的背包{}。",
		senderNickUtf8, prodName, durationText, bonusNotice);
	packet_UMsgManager.SendPacket_UMsg_PublicNotice(connection, notice);

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) claimed giftID {} ('{}', count {}{})\n",
		user->GetUserLogName(), giftID_low, prodName, gift.count, bonusNotice));
}

void Packet_ShopManager::parsePacket_Shop_ViewGift(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	unsigned int giftID_low = packet->ReadUInt32_LE();
	unsigned int giftID_high = packet->ReadUInt32_LE();

	serverConsole.Print(PrefixType::Info, format("[ Packet_ShopManager ] User ({}) has viewed giftID {}\n",
		user->GetUserLogName(), giftID_low));
}

void Packet_ShopManager::sendPacket_Shop_ClaimGiftReply(
	TCPConnection::pointer connection,
	unsigned char status,
	unsigned int giftID_low,
	unsigned int giftID_high,
	unsigned short claimedCount,
	const std::vector<unsigned short>& itemIDs,
	const std::string& message)
{
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::Shop });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_ShopType::ClaimGiftReply);
	packet->WriteUInt8(status);

	if (status == 1) {
		packet->WriteUInt32_LE(giftID_low);
		packet->WriteUInt32_LE(giftID_high);
		packet->WriteUInt16_LE(claimedCount);
		packet->WriteUInt16_LE((unsigned short)itemIDs.size());
		for (unsigned short id : itemIDs) {
			packet->WriteUInt16_LE(id);
		}
	}
	else {
		packet->WriteString(message);
	}

	packet->Send();
}

bool Packet_ShopManager::grantProductRewardToUser(
	unsigned long userID,
	unsigned int productID,
	unsigned short duration,
	unsigned char isCount,
	unsigned short count,
	std::vector<unsigned short>& outAwardedItemIDs,
	std::vector<InventoryItem>* outDeltaItems)
{
	outAwardedItemIDs.clear();
	const ShopProduct* prod = GetProductByID(productID);

	auto targetInventory = databaseManager.GetUserInventory(userID);
	time_t now = time(nullptr);
	unsigned short maxSlotID = 0;
	for (const auto& item : targetInventory) {
		if (item.slotID > maxSlotID) {
			maxSlotID = item.slotID;
		}
	}

	if (prod != nullptr && !prod->packageItems.empty()) {
		std::vector<InventoryItem> itemsToAdd;
		for (const auto& reward : prod->packageItems) {
			outAwardedItemIDs.push_back(reward.itemID);
			const ItemDefinition* itemDef = itemDefinitionManager.GetItem(reward.itemID);

			bool itemOwned = false;
			InventoryItem ownedItem;
			for (const auto& item : targetInventory) {
				if (item.itemID == reward.itemID) {
					itemOwned = true;
					ownedItem = item;
					break;
				}
			}

			bool isPeriod = (itemDef != nullptr) ? itemDef->isPeriod : (reward.duration > 0 || (reward.duration == 0 && reward.count == 1));

			if (isPeriod) {
				unsigned short effectiveDuration = reward.duration;
				if (duration > 0 && (reward.duration <= 1 || reward.duration == duration)) {
					effectiveDuration = duration;
				}

				if (itemOwned) {
					if (ownedItem.expirationTime == 0) {
						// Already permanent
					}
					else if (effectiveDuration == 0 || effectiveDuration == 0xFFFF) {
						databaseManager.UpdateUserInventoryExpiration(userID, reward.itemID, 0);
						if (outDeltaItems) {
							ownedItem.expirationTime = 0;
							outDeltaItems->push_back(ownedItem);
						}
					}
					else {
						unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
						unsigned long newExp = baseTime + (unsigned long)effectiveDuration * 86400;
						databaseManager.UpdateUserInventoryExpiration(userID, reward.itemID, newExp);
						if (outDeltaItems) {
							ownedItem.expirationTime = newExp;
							outDeltaItems->push_back(ownedItem);
						}
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
					targetInventory.push_back(newItem);
					if (outDeltaItems) {
						outDeltaItems->push_back(newItem);
					}
				}
			}
			else {
				// Count-based
				unsigned short addCount = reward.count * (count > 0 ? count : 1);
				if (itemOwned) {
					databaseManager.UpdateUserInventoryItemCount(userID, reward.itemID, addCount);
					if (outDeltaItems) {
						ownedItem.count += addCount;
						outDeltaItems->push_back(ownedItem);
					}
				}
				else {
					InventoryItem newItem;
					newItem.slotID = ++maxSlotID;
					newItem.itemID = reward.itemID;
					newItem.count = addCount;
					newItem.activeFlag = 1;
					newItem.inUse = false;
					newItem.obtainTime = (unsigned long)now;
					newItem.expirationTime = 0;
					newItem.enhanceBits = 0;
					itemsToAdd.push_back(newItem);
					targetInventory.push_back(newItem);
					if (outDeltaItems) {
						outDeltaItems->push_back(newItem);
					}
				}
			}
		}

		if (!itemsToAdd.empty()) {
			databaseManager.AddUserInventoryItems(userID, itemsToAdd);
		}
	}
	else {
		// Single item
		unsigned short itemID = (unsigned short)productID;
		outAwardedItemIDs.push_back(itemID);

		bool alreadyOwned = false;
		InventoryItem ownedItem;
		for (const auto& item : targetInventory) {
			if (item.itemID == itemID) {
				alreadyOwned = true;
				ownedItem = item;
				break;
			}
		}

		if (isCount) {
			if (alreadyOwned) {
				databaseManager.UpdateUserInventoryItemCount(userID, itemID, count);
				if (outDeltaItems) {
					ownedItem.count += count;
					outDeltaItems->push_back(ownedItem);
				}
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = itemID;
				newItem.count = count;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = 0;
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });
				if (outDeltaItems) {
					outDeltaItems->push_back(newItem);
				}
			}
		}
		else {
			bool isPermOffer = (duration == 0 || duration == 0xFFFF);
			if (alreadyOwned) {
				if (ownedItem.expirationTime == 0) {
					// Already permanent, keep permanent
				}
				else if (isPermOffer) {
					databaseManager.UpdateUserInventoryExpiration(userID, itemID, 0);
					if (outDeltaItems) {
						ownedItem.expirationTime = 0;
						outDeltaItems->push_back(ownedItem);
					}
				}
				else {
					unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
					unsigned long newExp = baseTime + (unsigned long)duration * 86400;
					databaseManager.UpdateUserInventoryExpiration(userID, itemID, newExp);
					if (outDeltaItems) {
						ownedItem.expirationTime = newExp;
						outDeltaItems->push_back(ownedItem);
					}
				}
			}
			else {
				InventoryItem newItem;
				newItem.slotID = ++maxSlotID;
				newItem.itemID = itemID;
				newItem.count = 1;
				newItem.activeFlag = 1;
				newItem.inUse = false;
				newItem.obtainTime = (unsigned long)now;
				newItem.expirationTime = isPermOffer ? 0 : (unsigned long)(now + (unsigned long)duration * 86400);
				newItem.enhanceBits = 0;
				databaseManager.AddUserInventoryItems(userID, { newItem });
				if (outDeltaItems) {
					outDeltaItems->push_back(newItem);
				}
			}
		}
	}

	return true;
}


