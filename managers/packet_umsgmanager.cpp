#include "packet_umsgmanager.h"
#include "usermanager.h"
#include "roommanager.h"
#include "packetmanager.h"
#include "serverconsole.h"
#include "databasemanager.h"
#include "itemdefinitionmanager.h"
#include "noticemanager.h"
#include "packet_shopmanager.h"
#include "packet_updateinfomanager.h"
#include "attendancemanager.h"
#include "craftmanager.h"
#include "enhancemanager.h"
#include "titlemanager.h"
#include "questmanager.h"
#include "serverconfig.h"
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <windows.h>

Packet_UMsgManager packet_UMsgManager;

std::string Utf8ToCp950(const std::string& utf8Str) {
	if (utf8Str.empty()) return "";
	int wlen = MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), NULL, 0);
	if (wlen <= 0) return utf8Str;
	std::wstring wstr(wlen, 0);
	MultiByteToWideChar(CP_UTF8, 0, utf8Str.data(), (int)utf8Str.size(), &wstr[0], wlen);
	int mblen = WideCharToMultiByte(950, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
	if (mblen <= 0) return utf8Str;
	std::string result(mblen, 0);
	WideCharToMultiByte(950, 0, wstr.data(), (int)wstr.size(), &result[0], mblen, NULL, NULL);
	return result;
}

std::string Cp950ToUtf8(const std::string& cp950Str) {
	if (cp950Str.empty()) return "";
	int wlen = MultiByteToWideChar(950, 0, cp950Str.data(), (int)cp950Str.size(), NULL, 0);
	if (wlen <= 0) return cp950Str;
	std::wstring wstr(wlen, 0);
	MultiByteToWideChar(950, 0, cp950Str.data(), (int)cp950Str.size(), &wstr[0], wlen);
	int u8len = WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), NULL, 0, NULL, NULL);
	if (u8len <= 0) return cp950Str;
	std::string result(u8len, 0);
	WideCharToMultiByte(CP_UTF8, 0, wstr.data(), (int)wstr.size(), &result[0], u8len, NULL, NULL);
	return result;
}

static void SendClientNotice(TCPConnection::pointer connection, Packet_UMsgType chatType, const std::string& utf8Msg) {
	std::string cleanMsg = utf8Msg;
	const std::string prefix1 = "[系統] ";
	const std::string prefix2 = "[系統]";
	if (cleanMsg.rfind(prefix1, 0) == 0) {
		cleanMsg = cleanMsg.substr(prefix1.length());
	}
	else if (cleanMsg.rfind(prefix2, 0) == 0) {
		cleanMsg = cleanMsg.substr(prefix2.length());
	}

	std::string cp950Sender = Utf8ToCp950("系統");
	std::string cp950Text = Utf8ToCp950(cleanMsg);
	packet_UMsgManager.SendPacket_UMsg_Chat(connection, chatType, cp950Sender, cp950Text);
}


void Packet_UMsgManager::ParsePacket_UMsg(TCPConnection::Packet::pointer packet) {
	if (packet == NULL) {
		return;
	}

	auto& connection = packet->GetConnection();
	if (connection == NULL) {
		return;
	}

	User* user = userManager.GetUserByConnection(connection);
	if (!userManager.IsUserLoggedIn(user)) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_UMsgManager ] Client ({}) has sent Packet_UMsg, but it's not logged in!\n", connection->GetLogEndpoint()));
		return;
	}

	serverConsole.Print(PrefixType::Info, format("[ Packet_UMsgManager ] Parsing Packet_UMsg from user ({})\n", user->GetUserLogName()));

	unsigned char type = packet->ReadUInt8();

	switch (type) {
		case Packet_UMsgType::WhisperChat: {
			parsePacket_UMsg_WhisperChat(user, packet);
			break;
		}
		case Packet_UMsgType::ChannelChat: {
			parsePacket_UMsg_ChannelChat(user, packet);
			break;
		}
		case Packet_UMsgType::RoomChat: {
			parsePacket_UMsg_RoomChat(user, packet);
			break;
		}
		case Packet_UMsgType::ServerYell: {
			parsePacket_UMsg_ServerYell(user, packet);
			break;
		}
		default: {
			serverConsole.Print(PrefixType::Warn, format("[ Packet_UMsgManager ] User ({}) has sent unregistered Packet_UMsg type {}!\n", user->GetUserLogName(), type));
			break;
		}
	}
}

void Packet_UMsgManager::SendPacket_UMsg_PublicNotice(TCPConnection::pointer connection, const string& text) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_UMsgType::PublicNotice);
	packet->WriteString(Utf8ToCp950(text));

	packet->Send();
}

void Packet_UMsgManager::SendPacket_UMsg_ServerMessageText(TCPConnection::pointer connection, const string& text) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	string cleanText = text;
	const string prefix1 = "[伺服器訊息] ";
	const string prefix2 = "[伺服器訊息]";
	if (cleanText.rfind(prefix1, 0) == 0) {
		cleanText = cleanText.substr(prefix1.length());
	}
	else if (cleanText.rfind(prefix2, 0) == 0) {
		cleanText = cleanText.substr(prefix2.length());
	}

	packet->WriteUInt8(Packet_UMsgType::ServerMessage);
	packet->WriteString(Utf8ToCp950(cleanText));

	packet->Send();
}

void Packet_UMsgManager::SendPacket_UMsg_RollingBanner(TCPConnection::pointer connection, const string& text) {
	SendPacket_UMsg_PublicNotice(connection, text);
}

void Packet_UMsgManager::SendPacket_UMsg_BulletinNotice(TCPConnection::pointer connection, unsigned short noticeID, unsigned char flags, const string& title, const string& content) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_UMsgType::BulletinNotice);
	packet->WriteUInt16_LE(noticeID);
	packet->WriteUInt8(flags);
	packet->WriteString(Utf8ToCp950(title));
	packet->WriteString(Utf8ToCp950(content));

	packet->Send();
}

void Packet_UMsgManager::SendPacket_UMsg_ItemAwardNotice(TCPConnection::pointer connection, const vector<AwardItem>& items, const string& titleKey, const string& msgKey, const vector<string>& args) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_UMsgType::ItemAwardNotice);

	// 1. List 1: Award Items
	packet->WriteUInt8((unsigned char)items.size());
	for (const auto& item : items) {
		packet->WriteUInt16_LE(item.itemID);
		packet->WriteUInt16_LE(item.count);
		packet->WriteUInt16_LE(item.duration);
		packet->WriteUInt32_LE(item.flags);
	}

	// 2 ~ 6. Five empty lists (Lists 2, 3, 4, 5, 6)
	packet->WriteUInt8(0);
	packet->WriteUInt8(0);
	packet->WriteUInt8(0);
	packet->WriteUInt8(0);
	packet->WriteUInt8(0);

	// 7. titleKey (client formats as "#CSO_Warning_%s")
	packet->WriteString(titleKey);

	// 8. msgKey (client formats as "#CSO_Warning_%s")
	packet->WriteString(msgKey);

	// 9. Formatted arguments list
	packet->WriteUInt8((unsigned char)args.size());
	for (const auto& arg : args) {
		packet->WriteString(Utf8ToCp950(arg));
	}

	packet->Send();
}

void Packet_UMsgManager::SendPacket_UMsg_ServerYell(TCPConnection::pointer connection, const string& senderNickName, const string& text) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(Packet_UMsgType::ServerYell);
	packet->WriteString(Utf8ToCp950(senderNickName));
	packet->WriteString(Utf8ToCp950(text));

	packet->Send();
}

void Packet_UMsgManager::SendPacket_UMsg_NoticeMessage(TCPConnection::pointer connection, const string& text) {
	SendPacket_UMsg_PublicNotice(connection, text);
}

void Packet_UMsgManager::SendPacket_UMsg_ServerMessage(TCPConnection::pointer connection, Packet_UMsgType type, const string& text, const vector<string>& additionalText) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(type);
	packet->WriteString(text);
	packet->WriteUInt8((unsigned char)additionalText.size());
	for (auto& t : additionalText) {
		packet->WriteString(t);
	}

	packet->Send();
}

void Packet_UMsgManager::BroadcastPublicNotice(const string& text) {
	vector<User*> users = userManager.GetUsers();
	for (auto& u : users) {
		if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u) && u->GetUserStatus() != UserStatus::InServerList) {
			SendPacket_UMsg_PublicNotice(u->GetConnection(), text);
		}
	}
}

void Packet_UMsgManager::BroadcastServerMessage(const string& text) {
	vector<User*> users = userManager.GetUsers();
	for (auto& u : users) {
		if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u) && u->GetUserStatus() != UserStatus::InServerList) {
			SendPacket_UMsg_ServerMessageText(u->GetConnection(), text);
		}
	}
}

void Packet_UMsgManager::BroadcastRollingBanner(const string& text) {
	vector<User*> users = userManager.GetUsers();
	for (auto& u : users) {
		if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u) && u->GetUserStatus() != UserStatus::InServerList) {
			SendPacket_UMsg_RollingBanner(u->GetConnection(), text);
		}
	}
}

void Packet_UMsgManager::BroadcastServerYell(const string& senderNickName, const string& text) {
	vector<User*> users = userManager.GetUsers();
	for (auto& u : users) {
		if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u) && u->GetUserStatus() != UserStatus::InServerList) {
			SendPacket_UMsg_ServerYell(u->GetConnection(), senderNickName, text);
		}
	}
}

void Packet_UMsgManager::BroadcastBulletinNotice(unsigned short noticeID, unsigned char flags, const string& title, const string& content) {
	vector<User*> users = userManager.GetUsers();
	for (auto& u : users) {
		if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u) && u->GetUserStatus() != UserStatus::InServerList) {
			SendPacket_UMsg_BulletinNotice(u->GetConnection(), noticeID, flags, title, content);
		}
	}
}

void Packet_UMsgManager::SendPacket_UMsg_SystemReply(TCPConnection::pointer connection, Packet_UMsgType type, const string& replyKey, const vector<string>& args) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8((unsigned char)type);
	packet->WriteString(replyKey);
	packet->WriteUInt8((unsigned char)args.size());
	for (const auto& a : args) {
		packet->WriteString(Utf8ToCp950(a));
	}

	packet->Send();
}

void Packet_UMsgManager::BroadcastSystemReply(Packet_UMsgType type, const string& replyKey, const vector<string>& args) {
	vector<User*> users = userManager.GetUsers();
	for (auto& u : users) {
		if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u) && u->GetUserStatus() != UserStatus::InServerList) {
			SendPacket_UMsg_SystemReply(u->GetConnection(), type, replyKey, args);
		}
	}
}

void Packet_UMsgManager::parsePacket_UMsg_WhisperChat(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() == UserStatus::InServerList) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_UMsgManager ] User ({}) has sent Packet_UMsg WhisperChat, but it's in the server list!\n", user->GetUserLogName()));
		return;
	}

	const UserCharacterResult& userCharacterResult = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
	if (!userCharacterResult.result) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
		return;
	}

	const string& receiverNickName = packet->ReadString();
	const string& text = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UMsgManager ] User ({}) has sent Packet_UMsg WhisperChat - receiverNickName: {}, text: {}\n", user->GetUserLogName(), Cp950ToUtf8(receiverNickName), Cp950ToUtf8(text)));

	vector<User*> users = userManager.GetUsers();

	users.erase(remove_if(users.begin(), users.end(), [](User* u) {
		return (u == NULL || u->GetConnection() == NULL);
	}), users.end());

	UserCharacterResult uCharacterResult;
	bool foundNickName = false;

	for (auto& u : users) {
		uCharacterResult = u->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
		if (uCharacterResult.result && uCharacterResult.userCharacter.nickName == receiverNickName) {
			packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::InfoMessage, "MSG_TELL_OK", { receiverNickName });
			packet_UMsgManager.SendPacket_UMsg_Chat(u->GetConnection(), Packet_UMsgType::WhisperChat, userCharacterResult.userCharacter.nickName, text);

			foundNickName = true;
			break;
		}
	}

	if (!foundNickName) {
		packet_UMsgManager.SendPacket_UMsg_ServerMessage(connection, Packet_UMsgType::InfoMessage, "MSG_TELL_USER_NOT_FOUND", { receiverNickName });
	}
}

void Packet_UMsgManager::parsePacket_UMsg_ChannelChat(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InLobby) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_UMsgManager ] User ({}) has sent Packet_UMsg ChannelChat, but it's not in the lobby!\n", user->GetUserLogName()));
		return;
	}

	const UserCharacterResult& userCharacterResult = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
	if (!userCharacterResult.result) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
		return;
	}

	const string& text = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UMsgManager ] User ({}) has sent Packet_UMsg ChannelChat - text: {}\n", user->GetUserLogName(), Cp950ToUtf8(text)));

	if (HandleChatCommand(user, connection, text, Packet_UMsgType::ChannelChat)) {
		return;
	}

	vector<User*> users = userManager.GetUsers();

	users.erase(remove_if(users.begin(), users.end(), [](User* u) {
		return (u == NULL || u->GetConnection() == NULL || u->GetUserStatus() != UserStatus::InLobby);
	}), users.end());

	for (auto& u : users) {
		packet_UMsgManager.SendPacket_UMsg_Chat(u->GetConnection(), Packet_UMsgType::ChannelChat, userCharacterResult.userCharacter.nickName, text);
	}
}

void Packet_UMsgManager::parsePacket_UMsg_RoomChat(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	if (user->GetUserStatus() != UserStatus::InRoom) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_UMsgManager ] User ({}) has sent Packet_UMsg RoomChat, but it's not in a room!\n", user->GetUserLogName()));
		return;
	}

	Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
	if (room == NULL) {
		serverConsole.Print(PrefixType::Warn, format("[ Packet_RoomManager ] User ({}) has sent Packet_UMsg RoomChat, but its room is NULL!\n", user->GetUserLogName()));
		return;
	}

	const UserCharacterResult& userCharacterResult = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
	if (!userCharacterResult.result) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
		return;
	}

	const string& text = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UMsgManager ] User ({}) has sent Packet_UMsg RoomChat - text: {}\n", user->GetUserLogName(), Cp950ToUtf8(text)));

	if (HandleChatCommand(user, connection, text, Packet_UMsgType::RoomChat)) {
		return;
	}

	vector<User*> roomUsers = room->GetRoomUsers();

	roomUsers.erase(remove_if(roomUsers.begin(), roomUsers.end(), [](User* u) {
		return (u == NULL || u->GetConnection() == NULL || u->GetUserStatus() != UserStatus::InRoom);
	}), roomUsers.end());

	for (auto& u : roomUsers) {
		packet_UMsgManager.SendPacket_UMsg_Chat(u->GetConnection(), Packet_UMsgType::RoomChat, userCharacterResult.userCharacter.nickName, text);
	}
}

void Packet_UMsgManager::parsePacket_UMsg_ServerYell(User* user, TCPConnection::Packet::pointer packet) {
	if (user == NULL || packet == NULL) {
		return;
	}

	auto& connection = user->GetConnection();
	if (connection == NULL) {
		return;
	}

	const UserCharacterResult& userCharacterResult = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
	if (!userCharacterResult.result) {
		packetManager.SendPacket_Reply(connection, Packet_ReplyType::SysError);
		return;
	}

	const string& text = packet->ReadString();

	serverConsole.Print(PrefixType::Info, format("[ Packet_UMsgManager ] User ({}) has sent Packet_UMsg ServerYell - text: {}\n", user->GetUserLogName(), Cp950ToUtf8(text)));

	vector<User*> users = userManager.GetUsers();
	for (auto& u : users) {
		if (u != NULL && u->GetConnection() != NULL && userManager.IsUserLoggedIn(u)) {
			packet_UMsgManager.SendPacket_UMsg_Chat(u->GetConnection(), Packet_UMsgType::ServerYell, userCharacterResult.userCharacter.nickName, text);
		}
	}
}

void Packet_UMsgManager::SendPacket_UMsg_Chat(TCPConnection::pointer connection, Packet_UMsgType type, const string& senderNickName, const string& text) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8(type);
	packet->WriteString(senderNickName);
	packet->WriteString(text);

	packet->Send();
}

bool Packet_UMsgManager::HandleChatCommand(User* user, TCPConnection::pointer connection, const string& rawText, Packet_UMsgType chatType) {
	if (user == NULL || connection == NULL || rawText.empty()) {
		return false;
	}

	if (rawText[0] != '/') {
		return false;
	}

	string textUtf8 = Cp950ToUtf8(rawText);
	stringstream ss(textUtf8);
	string cmd;
	if (!(ss >> cmd)) {
		return false;
	}

	string cmdLower = cmd;
	transform(cmdLower.begin(), cmdLower.end(), cmdLower.begin(), [](unsigned char c) { return (char)tolower(c); });

	if (cmdLower != "/additem" && cmdLower != "/addallitem" && cmdLower != "/resetinv" && cmdLower != "/resetitem" &&
		cmdLower != "/notice" && cmdLower != "/msg" && cmdLower != "/gm" && cmdLower != "/dialog" &&
		cmdLower != "/banner" && cmdLower != "/yell" && cmdLower != "/reloadnotice" &&
		cmdLower != "/addpoints" && cmdLower != "/addgash" &&
		cmdLower != "/reloadshop" &&
		cmdLower != "/reloadcraft" &&
		cmdLower != "/reloadenhance" &&
		cmdLower != "/resetdaily" &&
		cmdLower != "/nextdaily" &&
		cmdLower != "/addtitle" &&
		cmdLower != "/unlockalltitles" &&
		cmdLower != "/sethonor" &&
		cmdLower != "/title" &&
		cmdLower != "/reloadtitle" &&
		cmdLower != "/completequest" &&
		cmdLower != "/resetquests" &&
		cmdLower != "/reloadquest") {
		return false;
	}

	// Echo the command back to the sender so they can see their input in the chat
	const UserCharacterResult& userCharResult = user->GetUserCharacter(USERCHARACTER_FLAG_NICKNAME);
	string senderNick = userCharResult.result ? userCharResult.userCharacter.nickName : "";
	SendPacket_UMsg_Chat(connection, chatType, senderNick, rawText);

	unsigned long userID = user->GetUserID();
	bool isGM = userManager.IsUserGM(user);

	auto SendNotice = [&](const string& msg) {
		SendClientNotice(connection, chatType, msg);
	};

	if (cmdLower == "/additem" || cmdLower == "/addallitem" || cmdLower == "/resetinv" || cmdLower == "/resetitem" ||
		cmdLower == "/addpoints" || cmdLower == "/addgash") {
		if (!serverConfig.permissions.allowPlayerItemCommands && !isGM) {
			SendNotice("權限不足：測試指令已被管理員停用！");
			return true;
		}
	}
	else if (cmdLower == "/resetdaily" || cmdLower == "/nextdaily") {
		if (!serverConfig.permissions.allowPlayerDailyCommands && !isGM) {
			SendNotice("權限不足：每日簽到測試指令已被管理員停用！");
			return true;
		}
	}
	else if (cmdLower == "/reloadshop" || cmdLower == "/reloadcraft") {
		if (!serverConfig.permissions.allowPlayerReloadShop && !isGM) {
			SendNotice("權限不足：配置重載指令已被管理員限制，僅限 GM 使用！");
			return true;
		}
	}
	else if (cmdLower == "/reloadenhance") {
		if (!serverConfig.permissions.allowPlayerReloadEnhance && !isGM) {
			SendNotice("權限不足：配置重載指令已被管理員限制，僅限 GM 使用！");
			return true;
		}
	}
	else if (cmdLower == "/notice" || cmdLower == "/msg" || cmdLower == "/gm" || cmdLower == "/dialog" ||
			cmdLower == "/banner" || cmdLower == "/reloadnotice") {
		if (!serverConfig.permissions.allowPlayerNotice && !isGM) {
			SendNotice("權限不足：全服廣播與管理指令僅限 GM 使用！");
			return true;
		}
	}
	else if (cmdLower == "/yell") {
		if (!serverConfig.permissions.allowPlayerNotice && !isGM) {
			SendNotice("權限不足：全服喇叭廣播指令僅限 GM 使用！");
			return true;
		}
	}

	if (cmdLower == "/additem") {
		string itemIDStr;
		if (!(ss >> itemIDStr)) {
			SendNotice("[系統] 請指定物品 ID，用法：/additem <ItemID> [天數/數量]");
			return true;
		}

		unsigned short itemID = 0;
		try {
			itemID = (unsigned short)stoul(itemIDStr);
		}
		catch (...) {
			SendNotice("[系統] 物品 ID 格式錯誤，必須為數字。");
			return true;
		}

		if (itemDefinitionManager.IsItemDisabled(itemID)) {
			SendNotice("[系統] 該物品已被管理員禁用，無法添加。");
			return true;
		}

		const ItemDefinition* itemDef = itemDefinitionManager.GetItem(itemID);
		if (itemDef == NULL) {
			SendNotice(format("[系統] 找不到 ID 為 {} 的物品！", itemID));
			return true;
		}

		string p2Str;
		bool hasP2 = false;
		unsigned long p2Val = 0;
		if (ss >> p2Str) {
			try {
				p2Val = stoul(p2Str);
				hasP2 = true;
			}
			catch (...) {}
		}

		auto inventory = user->GetUserInventory();
		bool alreadyOwned = false;
		InventoryItem ownedItem;
		unsigned short maxSlotID = 0;
		for (const auto& item : inventory) {
			if (item.itemID == itemID) {
				alreadyOwned = true;
				ownedItem = item;
			}
			if (item.slotID > maxSlotID) {
				maxSlotID = item.slotID;
			}
		}

		time_t now = time(NULL);

		if (itemDef->isPeriod) {
			// Duration-based item
			if (alreadyOwned) {
				if (ownedItem.expirationTime == 0) {
					// Already permanent
					SendNotice(format("[系統] 物品「{}」(ID: {}) 已是永久擁有，無需重複添加。", itemDef->name, itemID));
					return true;
				}

				// Currently timed
				if (!hasP2 || p2Val == 0) {
					// Upgrade to permanent
					if (databaseManager.UpdateUserInventoryExpiration(userID, itemID, 0)) {
						packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
						SendNotice(format("[系統] 物品「{}」(ID: {}) 原為期限物品，已成功升級為永久！", itemDef->name, itemID));
					}
					else {
						SendNotice("[系統] 更新物品期限失敗，數據庫寫入錯誤！");
					}
					return true;
				}
				else {
					// Extend duration
					unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
					unsigned long days = min(p2Val, 3650UL);
					unsigned long newExp = min(baseTime + days * 86400, (unsigned long)(now + 3650UL * 86400));
					if (databaseManager.UpdateUserInventoryExpiration(userID, itemID, newExp)) {
						packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
						SendNotice(format("[系統] 物品「{}」(ID: {}) 成功延長 {} 天期限！", itemDef->name, itemID, days));
					}
					else {
						SendNotice("[系統] 延長物品期限失敗，數據庫寫入錯誤！");
					}
					return true;
				}
			}

			// Not owned yet
			unsigned long days = 0;
			unsigned long expirationTime = 0;
			if (hasP2 && p2Val > 0) {
				days = min(p2Val, 3650UL);
				expirationTime = (unsigned long)(now + days * 86400);
			}

			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = 1;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = expirationTime;
			newItem.enhanceBits = 0;

			if (databaseManager.AddUserInventoryItems(userID, { newItem })) {
				packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
				if (expirationTime == 0) {
					SendNotice(format("[系統] 成功添加「{}」(ID: {})，期限：永久。", itemDef->name, itemID));
				}
				else {
					SendNotice(format("[系統] 成功添加「{}」(ID: {})，期限：{} 天。", itemDef->name, itemID, days));
				}
			}
			else {
				SendNotice("[系統] 添加物品失敗，數據庫寫入錯誤！");
			}
		}
		else {
			// Count-based / Consumable item
			if (alreadyOwned) {
				if (hasP2 && p2Val > 0) {
					unsigned short addCount = (unsigned short)min(p2Val, 999UL);
					if (databaseManager.UpdateUserInventoryItemCount(userID, itemID, addCount)) {
						packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
						SendNotice(format("[系統] 物品「{}」(ID: {}) 已在倉庫，疊加數量 +{}，總計：{} 個。", itemDef->name, itemID, addCount, min((unsigned int)ownedItem.count + addCount, 999U)));
					}
					else {
						SendNotice("[系統] 更新物品數量失敗，數據庫寫入錯誤！");
					}
				}
				else {
					SendNotice(format("[系統] 物品「{}」(ID: {}) 已存在於倉庫中，已自動忽略。", itemDef->name, itemID));
				}
				return true;
			}

			unsigned short count = 1;
			if (hasP2 && p2Val > 0) {
				count = (unsigned short)min(p2Val, 999UL);
			}

			InventoryItem newItem;
			newItem.slotID = ++maxSlotID;
			newItem.itemID = itemID;
			newItem.count = count;
			newItem.activeFlag = 1;
			newItem.inUse = false;
			newItem.obtainTime = (unsigned long)now;
			newItem.expirationTime = 0;
			newItem.enhanceBits = 0;

			if (databaseManager.AddUserInventoryItems(userID, { newItem })) {
				packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
				SendNotice(format("[系統] 成功添加「{}」(ID: {})，數量：{} 個。", itemDef->name, itemID, count));
			}
			else {
				SendNotice("[系統] 添加物品失敗，數據庫寫入錯誤！");
			}
		}

		return true;
	}
	else if (cmdLower == "/addallitem") {
		string token1;
		string token2;
		bool hasToken1 = (bool)(ss >> token1);
		bool hasToken2 = false;
		if (hasToken1) {
			hasToken2 = (bool)(ss >> token2);
		}

		unsigned char category = 0;
		bool hasP2 = false;
		unsigned long p2Val = 0;

		if (!hasToken1) {
			category = 0; // All categories
		}
		else if (!hasToken2) {
			// One argument provided: could be a category OR a day count for all items
			unsigned char parsedCat = itemDefinitionManager.ParseCategory(token1);
			if (parsedCat != 255) {
				category = parsedCat;
			}
			else {
				// Check if token1 is numeric days (e.g. /addallitem 30)
				bool isNum = true;
				for (char c : token1) {
					if (!isdigit((unsigned char)c)) { isNum = false; break; }
				}
				if (isNum) {
					try {
						p2Val = stoul(token1);
						hasP2 = true;
						category = 0;
					}
					catch (...) {
						category = 0;
					}
				}
				else {
					SendNotice(format("未知分類「{}」。可用分類：手槍、散彈槍、衝鋒槍、步槍、機槍、裝備、人物、道具、消耗品、近戰、服飾。（獲取全部物品請直接使用「/addallitem [天數]」）", token1));
					return true;
				}
			}
		}
		else {
			// Two arguments provided: arg1 must be category, arg2 must be days
			unsigned char parsedCat = itemDefinitionManager.ParseCategory(token1);
			if (parsedCat != 255) {
				category = parsedCat;
				try {
					p2Val = stoul(token2);
					hasP2 = true;
				}
				catch (...) {}
			}
			else {
				SendNotice(format("未知分類「{}」。可用分類：手槍、散彈槍、衝鋒槍、步槍、機槍、裝備、人物、道具、消耗品、近戰、服飾。（獲取全部物品請直接使用「/addallitem [天數]」）", token1));
				return true;
			}
		}

		if (category != 0 && !itemDefinitionManager.IsCategoryAllowed(category)) {
			string catName = itemDefinitionManager.GetCategoryName(category);
			SendNotice(format("該分類「{}」已被服務端配置禁用或排除，無法批量添加。", catName));
			return true;
		}

		vector<ItemDefinition> candidates;
		if (category == 0) {
			candidates = itemDefinitionManager.GetAllItems();
		}
		else {
			candidates = itemDefinitionManager.GetItemsByCategory(category);
		}

		if (candidates.empty()) {
			SendNotice("該分類下沒有任何有效物品！");
			return true;
		}

		auto inventory = user->GetUserInventory();
		unordered_map<unsigned short, InventoryItem> ownedMap;
		unsigned short maxSlotID = 0;
		for (const auto& item : inventory) {
			ownedMap[item.itemID] = item;
			if (item.slotID > maxSlotID) {
				maxSlotID = item.slotID;
			}
		}

		time_t now = time(NULL);
		vector<InventoryItem> itemsToAdd;
		vector<InventoryItem> itemsToUpgradePermanent;
		vector<pair<unsigned short, unsigned long>> itemsToExtend;
		size_t permanentSkippedCount = 0;

		for (const auto& def : candidates) {
			auto it = ownedMap.find(def.id);
			if (it != ownedMap.end()) {
				// Already owned
				const auto& ownedItem = it->second;
				if (def.isPeriod) {
					if (ownedItem.expirationTime == 0) {
						permanentSkippedCount++;
					}
					else {
						// Timed item
						if (!hasP2 || p2Val == 0) {
							// Upgrade to permanent
							itemsToUpgradePermanent.push_back(ownedItem);
						}
						else {
							// Extend duration
							unsigned long baseTime = (ownedItem.expirationTime > (unsigned long)now) ? ownedItem.expirationTime : (unsigned long)now;
							unsigned long days = min(p2Val, 3650UL);
							unsigned long newExp = min(baseTime + days * 86400, (unsigned long)(now + 3650UL * 86400));
							itemsToExtend.push_back({ ownedItem.itemID, newExp });
						}
					}
				}
				else {
					// Count-based already owned: skip in batch addallitem
					permanentSkippedCount++;
				}
				continue;
			}

			// Not owned: add new item
			InventoryItem item;
			item.slotID = ++maxSlotID;
			item.itemID = def.id;
			item.activeFlag = 1;
			item.inUse = false;
			item.obtainTime = (unsigned long)now;
			item.enhanceBits = 0;

			if (def.isPeriod) {
				if (hasP2 && p2Val > 0) {
					unsigned long days = min(p2Val, 3650UL);
					item.expirationTime = (unsigned long)(now + days * 86400);
				}
				else {
					item.expirationTime = 0; // Permanent
				}
				item.count = 1;
			}
			else {
				// Count-based consumable: default 1 if not specified
				if (hasP2 && p2Val > 0) {
					item.count = (unsigned short)min(p2Val, 999UL);
				}
				else {
					item.count = 1;
				}
				item.expirationTime = 0;
			}

			itemsToAdd.push_back(item);
		}

		string catName = itemDefinitionManager.GetCategoryName(category);

		bool changed = false;
		if (!itemsToAdd.empty()) {
			if (databaseManager.AddUserInventoryItems(userID, itemsToAdd)) {
				changed = true;
			}
		}

		for (const auto& item : itemsToUpgradePermanent) {
			if (databaseManager.UpdateUserInventoryExpiration(userID, item.itemID, 0)) {
				changed = true;
			}
		}

		for (const auto& update : itemsToExtend) {
			if (databaseManager.UpdateUserInventoryExpiration(userID, update.first, update.second)) {
				changed = true;
			}
		}

		if (changed) {
			packetManager.SendPacket_Inventory(connection, user->GetUserInventory());
		}

		// Feedback message
		if (itemsToAdd.empty() && itemsToUpgradePermanent.empty() && itemsToExtend.empty()) {
			SendNotice(format("您已永久擁有「{}」的所有物品，無需重複添加。", catName));
			return true;
		}

		if (itemsToUpgradePermanent.empty() && itemsToExtend.empty() && !itemsToAdd.empty()) {
			if (hasP2 && p2Val > 0) {
				SendNotice(format("成功為您添加「{}」共 {} 件物品，期限：{} 天！", catName, itemsToAdd.size(), min(p2Val, 3650UL)));
			}
			else {
				SendNotice(format("成功為您添加「{}」共 {} 件物品，期限：永久！", catName, itemsToAdd.size()));
			}
		}
		else if (itemsToAdd.empty() && itemsToUpgradePermanent.empty() && !itemsToExtend.empty()) {
			SendNotice(format("成功為您延長「{}」共 {} 件物品，延長：{} 天！", catName, itemsToExtend.size(), min(p2Val, 3650UL)));
		}
		else if (itemsToAdd.empty() && itemsToExtend.empty() && !itemsToUpgradePermanent.empty()) {
			SendNotice(format("成功為您將「{}」共 {} 件期限物品升級為永久！", catName, itemsToUpgradePermanent.size()));
		}
		else {
			string detail = format("「{}」處理完成：", catName);
			if (!itemsToAdd.empty()) {
				detail += format("新增 {} 件, ", itemsToAdd.size());
			}
			if (!itemsToExtend.empty()) {
				detail += format("延長 {} 件(+{}天), ", itemsToExtend.size(), min(p2Val, 3650UL));
			}
			if (!itemsToUpgradePermanent.empty()) {
				detail += format("升級 {} 件為永久, ", itemsToUpgradePermanent.size());
			}
			if (permanentSkippedCount > 0) {
				detail += format("{} 件已為永久已跳過", permanentSkippedCount);
			}
			if (detail.ends_with(", ")) {
				detail = detail.substr(0, detail.size() - 2);
			}
			SendNotice(detail);
		}

		return true;
	}
	else if (cmdLower == "/resetinv" || cmdLower == "/resetitem") {
		auto oldInventory = user->GetUserInventory();
		if (databaseManager.ResetUserInventory(userID)) {
			auto newInventory = user->GetUserInventory();

			unordered_set<unsigned short> newSlots;
			for (const auto& item : newInventory) {
				newSlots.insert(item.slotID);
			}

			vector<unsigned short> deletedSlots;
			unsigned short maxOldSlot = 0;
			for (const auto& item : oldInventory) {
				if (!newSlots.count(item.slotID)) {
					deletedSlots.push_back(item.slotID);
				}
				if (item.slotID > maxOldSlot) {
					maxOldSlot = item.slotID;
				}
			}

			// Purge all slots up to maxOldSlot (and at least 700) that are not in the new default inventory
			unsigned short purgeLimit = max(maxOldSlot, (unsigned short)700);
			unordered_set<unsigned short> addedDeleted(deletedSlots.begin(), deletedSlots.end());
			for (unsigned short s = 1; s <= purgeLimit; ++s) {
				if (!newSlots.count(s) && !addedDeleted.count(s)) {
					deletedSlots.push_back(s);
					addedDeleted.insert(s);
				}
			}

			packetManager.SendPacket_Inventory(connection, newInventory, deletedSlots);
			SendNotice("您的個人倉庫已成功重置並還原為官方初始預設裝備！");
		}
		else {
			SendNotice("重置個人倉庫失敗，數據庫寫入錯誤！");
		}
		return true;
	}

	else if (cmdLower == "/notice") {
		string noticeText;
		getline(ss >> ws, noticeText);
		if (noticeText.empty()) {
			SendNotice("用法：/notice <公告內容> (向全服廣播頂部跑馬燈及大廳公告)");
			return true;
		}
		BroadcastPublicNotice(noticeText);
		serverConsole.Print(PrefixType::Info, format("[ Admin Command ] User ({}) broadcast PublicNotice: {}\n", user->GetUserLogName(), noticeText));
		SendNotice("公告已成功向全服廣播！");
		return true;
	}
	else if (cmdLower == "/msg" || cmdLower == "/gm" || cmdLower == "/dialog") {
		string msgText;
		getline(ss >> ws, msgText);
		if (msgText.empty()) {
			SendNotice("用法：/msg <訊息內容> (向全服發送伺服器文字訊息)");
			return true;
		}
		BroadcastServerMessage(msgText);
		serverConsole.Print(PrefixType::Info, format("[ Admin Command ] User ({}) broadcast ServerMessage: {}\n", user->GetUserLogName(), msgText));
		SendNotice("訊息已向全服發送！");
		return true;
	}
	else if (cmdLower == "/banner") {
		string bannerText;
		getline(ss >> ws, bannerText);
		if (bannerText.empty()) {
			SendNotice("用法：/banner <內容> (向全服廣播頂部即時跑馬燈)");
			return true;
		}
		BroadcastRollingBanner(bannerText);
		serverConsole.Print(PrefixType::Info, format("[ Admin Command ] User ({}) broadcast RollingBanner: {}\n", user->GetUserLogName(), bannerText));
		SendNotice("跑馬燈已向全服發送！");
		return true;
	}
	else if (cmdLower == "/yell") {
		string yellText;
		getline(ss >> ws, yellText);
		if (yellText.empty()) {
			SendNotice("用法：/yell <內容> (以全服喇叭向全服廣播)");
			return true;
		}
		BroadcastServerYell("管理員", yellText);
		serverConsole.Print(PrefixType::Info, format("[ Admin Command ] User ({}) broadcast ServerYell: {}\n", user->GetUserLogName(), yellText));
		SendNotice("全服喇叭已成功廣播！");
		return true;
	}
	else if (cmdLower == "/reloadnotice") {
		noticeManager.Reload();
		SendNotice("公告配置 configs/notice.json 已成功重新載入！");
		return true;
	}
	else if (cmdLower == "/addpoints") {
		string amountStr;
		if (!(ss >> amountStr)) {
			SendNotice("[系統] 請指定數量！用法：/addpoints <數量>");
			return true;
		}

		unsigned long long amount = 0;
		try {
			amount = stoull(amountStr);
		}
		catch (...) {
			SendNotice("[系統] 請指定大於 0 的有效金額！用法：/addpoints <數量>");
			return true;
		}

		if (amount == 0) {
			SendNotice("[系統] 請指定大於 0 的有效金額！用法：/addpoints <數量>");
			return true;
		}

		auto currentChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		if (currentChar.userCharacter.points + amount > 2000000000ULL) {
			SendNotice("[系統] 增加失敗！Points 總額不可超過上限 2,000,000,000（20 億）。");
			return true;
		}

		if (databaseManager.AddUserFunds(userID, true, amount)) {
			auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
			SendNotice(format("[系統] 成功增加 Points: +{}！當前 Points: {}。", amount, charResult.userCharacter.points));
		}
		else {
			SendNotice("[系統] 增加 Points 失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/addgash") {
		string amountStr;
		if (!(ss >> amountStr)) {
			SendNotice("[系統] 請指定數量！用法：/addgash <數量>");
			return true;
		}

		unsigned long long amount = 0;
		try {
			amount = stoull(amountStr);
		}
		catch (...) {
			SendNotice("[系統] 請指定大於 0 的有效金額！用法：/addgash <數量>");
			return true;
		}

		if (amount == 0) {
			SendNotice("[系統] 請指定大於 0 的有效金額！用法：/addgash <數量>");
			return true;
		}

		auto currentChar = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
		if (currentChar.userCharacter.cash + amount > 2000000000ULL) {
			SendNotice("[系統] 增加失敗！GASH 總額不可超過上限 2,000,000,000（20 億）。");
			return true;
		}

		if (databaseManager.AddUserFunds(userID, false, amount)) {
			auto charResult = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, charResult.userCharacter });
			SendNotice(format("[系統] 成功增加 GASH: +{}！當前 GASH: {}。", amount, charResult.userCharacter.cash));
		}
		else {
			SendNotice("[系統] 增加 GASH 失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/reloadshop") {
		if (packet_ShopManager.LoadShopConfig()) {
			if (serverConfig.shop.broadcastOnReload) {
				packet_ShopManager.BroadcastShopList();
				SendNotice(format("[系統] 商城配置已重新加載並廣播給在線玩家！共 {} 件商品，{} 種方案。（註：在線客戶端商品將疊加，需重新登入刷新）",
					packet_ShopManager.GetProductCount(), packet_ShopManager.GetOfferCount()));
			}
			else {
				SendNotice(format("[系統] 商城配置已重新加載！共 {} 件商品，{} 種方案。新登入玩家即刻生效，在線玩家重新登入後即可刷新商城介面。",
					packet_ShopManager.GetProductCount(), packet_ShopManager.GetOfferCount()));
			}
		}
		else {
			SendNotice("[系統] 加載商城配置失敗，請檢查 configs/shop.json！");
		}
		return true;
	}
	else if (cmdLower == "/reloadcraft") {
		if (craftManager.Reload()) {
			SendNotice("[系統] 工坊製造配置已成功重載！(configs/craft.json)");
		}
		else {
			SendNotice("[系統] 重載工坊製造配置失敗，請檢查 configs/craft.json 格式！");
		}
		return true;
	}
	else if (cmdLower == "/reloadenhance") {
		if (enhanceManager.Reload()) {
			SendNotice("[系統] 武器強化配置已成功重載！(configs/enhance.json)");
		}
		else {
			SendNotice("[系統] 重載武器強化配置失敗，請檢查 configs/enhance.json 格式！");
		}
		return true;
	}
	else if (cmdLower == "/resetdaily") {
		string dayStr;
		unsigned char targetStreak = 255;
		if (ss >> dayStr) {
			try {
				int d = stoi(dayStr);
				if (d >= 1 && d <= 7) {
					targetStreak = (unsigned char)(d - 1);
				}
			}
			catch (...) {}
		}

		if (databaseManager.ResetUserAttendance(userID, targetStreak)) {
			attendanceManager.ClearUserCandidates(userID);
			attendanceManager.SendAttendanceInfo(user, true);

			AttendanceRecord record;
			databaseManager.GetUserAttendance(userID, record);
			unsigned char nextDay = (record.consecutiveDays % 7) + 1;
			SendNotice(format("[系統] 今日簽到抽獎已重置！當前已解鎖第 {} 天簽到獎勵，簽到介面已刷新。", nextDay));
		}
		else {
			SendNotice("[系統] 重置今日簽到失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/nextdaily") {
		string dayStr;
		bool specificDay = false;
		unsigned char targetStreak = 0;
		if (ss >> dayStr) {
			try {
				int d = stoi(dayStr);
				if (d >= 1 && d <= 7) {
					specificDay = true;
					targetStreak = (unsigned char)(d - 1);
				}
			}
			catch (...) {}
		}

		bool ok = false;
		if (specificDay) {
			ok = databaseManager.ResetUserAttendance(userID, targetStreak);
		}
		else {
			ok = databaseManager.AdvanceUserAttendanceNextDay(userID);
		}

		if (ok) {
			attendanceManager.ClearUserCandidates(userID);
			attendanceManager.SendAttendanceInfo(user, true);

			AttendanceRecord record;
			databaseManager.GetUserAttendance(userID, record);
			unsigned char nextDay = (record.consecutiveDays % 7) + 1;
			SendNotice(format("[系統] 已開啟下一天簽到抽獎！當前已解鎖第 {} 天簽到獎勵，簽到介面已刷新。", nextDay));
		}
		else {
			SendNotice("[系統] 開啟下一天簽到失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/addtitle") {
		string tidStr;
		if (!(ss >> tidStr)) {
			SendNotice("[系統] 請指定稱號 ID！用法：/addtitle <TitleID> (1~76)");
			return true;
		}

		unsigned short tid = 0;
		try {
			tid = (unsigned short)stoul(tidStr);
		}
		catch (...) {
			SendNotice("[系統] 請輸入有效的稱號 ID (1~76)！用法：/addtitle <TitleID>");
			return true;
		}

		if (tid == 0 || tid > 76) {
			SendNotice("[系統] 稱號 ID 超出範圍 (有效範圍：1~76)！");
			return true;
		}

		const TitleConfig* cfg = titleManager.GetTitle(tid);
		if (cfg == nullptr) {
			SendNotice(format("[系統] 找不到 ID 為 {} 的稱號配置！", tid));
			return true;
		}

		if (databaseManager.HasUserTitle(userID, tid)) {
			SendNotice(format("[系統] 您已經擁有稱號 [{}]「{}」！", tid, cfg->name));
			return true;
		}

		if (titleManager.UnlockTitle(user, tid, true)) {
			SendNotice(format("[系統] 成功為您解鎖稱號 [{}]「{}」！獲得榮譽點數 +{}。", tid, cfg->name, cfg->honorPoints));
		}
		else {
			SendNotice("[系統] 解鎖稱號失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/unlockalltitles") {
		if (titleManager.UnlockAllTitles(user, true)) {
			SendNotice(format("[系統] 成功為您解鎖全套 76 個榮譽稱號！"));
		}
		else {
			SendNotice("[系統] 解鎖全稱號失敗！");
		}
		return true;
	}
	else if (cmdLower == "/sethonor") {
		string honorStr;
		if (!(ss >> honorStr)) {
			SendNotice("[系統] 請指定點數！用法：/sethonor <點數>");
			return true;
		}

		unsigned short honor = 0;
		try {
			honor = (unsigned short)stoul(honorStr);
		}
		catch (...) {
			SendNotice("[系統] 請輸入有效的榮譽點數！用法：/sethonor <點數>");
			return true;
		}

		if (databaseManager.SetUserHonorPoints(userID, honor)) {
			titleManager.SyncTitleUpdate(user);
			SendNotice(format("[系統] 您的榮譽點數已更新為：{}！", honor));
		}
		else {
			SendNotice("[系統] 更新榮譽點數失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/title") {
		string tidStr;
		if (!(ss >> tidStr)) {
			SendNotice("[系統] 用法：/title <TitleID> (1~76，輸入 0 為卸下稱號)");
			return true;
		}

		unsigned short tid = 0;
		try {
			tid = (unsigned short)stoul(tidStr);
		}
		catch (...) {
			SendNotice("[系統] 請輸入有效的稱號 ID (0~76)！");
			return true;
		}

		if (tid == 0) {
			titleManager.EquipTitle(user, 0);
			SendNotice("[系統] 已成功卸下稱號！");
			return true;
		}

		if (!databaseManager.HasUserTitle(userID, tid)) {
			SendNotice(format("[系統] 您尚未解鎖稱號 ID {}，無法佩戴！", tid));
			return true;
		}

		if (titleManager.EquipTitle(user, tid)) {
			const TitleConfig* cfg = titleManager.GetTitle(tid);
			string tname = cfg ? cfg->name : to_string(tid);
			SendNotice(format("[系統] 成功佩戴稱號【{}】！", tname));
		}
		else {
			SendNotice("[系統] 佩戴稱號失敗！");
		}
		return true;
	}
	else if (cmdLower == "/reloadtitle") {
		if (titleManager.Reload()) {
			SendNotice("[系統] 稱號配置已成功重載！(configs/title.json)");
		}
		else {
			SendNotice("[系統] 重載稱號配置失敗，請檢查 configs/title.json 格式！");
		}
		return true;
	}
	else if (cmdLower == "/completequest") {
		string qidStr;
		if (!(ss >> qidStr)) {
			SendNotice("[系統] 用法：/completequest <QuestID>");
			return true;
		}

		unsigned int qid = 0;
		try {
			qid = stoul(qidStr);
		}
		catch (...) {
			SendNotice("[系統] 請輸入有效的任務 ID！");
			return true;
		}

		if (questManager.CompleteQuest(user, qid)) {
			const QuestConfig* cfg = questManager.GetQuest(qid);
			string qname = cfg ? cfg->name : to_string(qid);
			SendNotice(format("[系統] 任務 [{}]【{}】已手動設置為達成狀態，可在任務面板領獎！", qid, qname));
		}
		else {
			SendNotice(format("[系統] 任務 ID {} 不存在！", qid));
		}
		return true;
	}
	else if (cmdLower == "/resetquests") {
		if (questManager.ResetUserQuests(user)) {
			SendNotice("[系統] 您的每日任務已重置為初始進行中狀態！");
		}
		else {
			SendNotice("[系統] 重置任務失敗！");
		}
		return true;
	}
	else if (cmdLower == "/reloadquest") {
		if (questManager.Reload()) {
			SendNotice("[系統] 任務配置已成功重載！(configs/quest.json)");
		}
		else {
			SendNotice("[系統] 重載任務配置失敗，請檢查 configs/quest.json 格式！");
		}
		return true;
	}

	return false;
}
