#include "packet_umsgmanager.h"
#include "packet_roommanager.h"
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
#include "rewarditemmanager.h"
#include "packet_messengermanager.h"
#include "serverconfig.h"
#include "scenariorewardmanager.h"
#include "battlebonusmanager.h"
#include <sstream>
#include <unordered_set>
#include <algorithm>
#include <windows.h>

Packet_UMsgManager packet_UMsgManager;

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

void Packet_UMsgManager::SendPacket_UMsg_DialogBox(TCPConnection::pointer connection, const string& text) {
	// In lobby/inventory/room, PublicNotice (Type 10) triggers IGameUI::ShowMessageBox with #CSO_ServerMessage_Title and CP950 text
	SendPacket_UMsg_PublicNotice(connection, text);
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

void Packet_UMsgManager::SendPacket_UMsg_ItemAwardNotice(TCPConnection::pointer connection, const vector<AwardItem>& items, unsigned int points, unsigned int exp, unsigned int honor, const string& titleKey, const string& msgKey, const vector<string>& args, Packet_UMsgType type) {
	if (connection == NULL) {
		return;
	}

	auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
	if (packet == NULL) {
		return;
	}

	packet->WriteUInt8((unsigned char)type);

	// 1. List 1: Award Items (type 1 in client: 10 bytes each)
	packet->WriteUInt8((unsigned char)items.size());
	for (const auto& item : items) {
		packet->WriteUInt16_LE(item.itemID);
		packet->WriteUInt16_LE(item.count);
		packet->WriteUInt16_LE(item.duration);
		packet->WriteUInt32_LE(item.flags);
	}

	// 2. List 2: Unknown (type 0 in client) - empty
	packet->WriteUInt8(0);

	// 3. List 3: Titles (type 3 in client) - empty
	packet->WriteUInt8(0);

	// 4. List 4: Award Points (type 4 in client: format "%d點Point", icon "Point" -> "gfx/vgui/basket/point.tga")
	if (points > 0) {
		packet->WriteUInt8(1);
		packet->WriteUInt32_LE(points);
	}
	else {
		packet->WriteUInt8(0);
	}

	// 5. List 5: Exp (type 5 in client: format "經驗值%d", icon "Exp" -> "gfx/vgui/basket/exp.tga")
	if (exp > 0) {
		packet->WriteUInt8(1);
		packet->WriteUInt32_LE(exp);
	}
	else {
		packet->WriteUInt8(0);
	}

	// 6. List 6: Honor (type 6 in client: format "榮譽點數%d點", icon "Honor" -> "gfx/vgui/basket/honor.tga")
	if (honor > 0) {
		packet->WriteUInt8(1);
		packet->WriteUInt32_LE(honor);
	}
	else {
		packet->WriteUInt8(0);
	}

	auto SanitizeKey = [](const string& key) -> string {
		if (key.rfind("#CSO_Warning_", 0) == 0) return key.substr(13);
		if (key.rfind("CSO_Warning_", 0) == 0) return key.substr(12);
		return key;
	};

	// 7. titleKey (client formats as "#CSO_Warning_%s")
	packet->WriteString(SanitizeKey(titleKey));

	// 8. msgKey (client formats as "#CSO_Warning_%s")
	packet->WriteString(SanitizeKey(msgKey));

	// 9. Formatted arguments list
	packet->WriteUInt8((unsigned char)args.size());
	for (const auto& arg : args) {
		packet->WriteString(Utf8ToCp950(arg));
	}

	packet->Send();
}

void Packet_UMsgManager::SendPacket_UMsg_ItemAwardNotice(TCPConnection::pointer connection, const vector<AwardItem>& items, unsigned int points, const string& titleKey, const string& msgKey, const vector<string>& args, Packet_UMsgType type) {
	SendPacket_UMsg_ItemAwardNotice(connection, items, points, 0, 0, titleKey, msgKey, args, type);
}

void Packet_UMsgManager::SendPacket_UMsg_ItemAwardNotice(TCPConnection::pointer connection, const vector<AwardItem>& items, const string& titleKey, const string& msgKey, const vector<string>& args, Packet_UMsgType type) {
	SendPacket_UMsg_ItemAwardNotice(connection, items, 0, 0, 0, titleKey, msgKey, args, type);
}

void Packet_UMsgManager::SendPacket_UMsg_ItemExpireNotice(TCPConnection::pointer connection, const vector<unsigned short>& itemIDs) {
	if (connection == NULL || itemIDs.empty()) {
		return;
	}

	size_t offset = 0;
	while (offset < itemIDs.size()) {
		size_t chunkSize = min(itemIDs.size() - offset, (size_t)255);
		auto packet = TCPConnection::Packet::Create(PacketSource::Server, connection, { (unsigned char)PacketID::UMsg });
		if (packet == NULL) {
			return;
		}

		packet->WriteUInt8(Packet_UMsgType::ItemExpireNotice);
		packet->WriteUInt8((unsigned char)chunkSize);
		for (size_t i = 0; i < chunkSize; ++i) {
			packet->WriteUInt16_LE(itemIDs[offset + i]);
		}

		packet->Send();
		offset += chunkSize;
	}
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

	string cleanText = text;
	if (type == Packet_UMsgType::WarningMessage) {
		if (cleanText.rfind("#CSO_Warning_", 0) == 0) cleanText = cleanText.substr(13);
		else if (cleanText.rfind("CSO_Warning_", 0) == 0) cleanText = cleanText.substr(12);
	}

	packet->WriteUInt8(type);
	packet->WriteString(cleanText);
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

	packet_MessengerManager.SendPacket_Messenger_ChatCount(connection, 255);
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

	if (cmdLower != "/additem" && cmdLower != "/addallitem" && cmdLower != "/deleteitem" && cmdLower != "/delitem" && cmdLower != "/resetinv" && cmdLower != "/resetitem" &&
		cmdLower != "/bighead" &&
		cmdLower != "/notice" && cmdLower != "/msg" && cmdLower != "/gm" && cmdLower != "/dialog" &&
		cmdLower != "/banner" && cmdLower != "/yell" && cmdLower != "/reloadnotice" &&
		cmdLower != "/addpoints" && cmdLower != "/addgash" &&
		cmdLower != "/setlevel" && cmdLower != "/level" && cmdLower != "/addexp" &&
		cmdLower != "/setzombiestats" && cmdLower != "/zombiestats" &&
		cmdLower != "/reloadshop" &&
		cmdLower != "/reloadcraft" &&
		cmdLower != "/reloadenhance" &&
		cmdLower != "/reloadreward" &&
		cmdLower != "/reloadrewarditem" &&
		cmdLower != "/resetdaily" &&
		cmdLower != "/nextdaily" &&
		cmdLower != "/addtitle" &&
		cmdLower != "/unlockalltitles" &&
		cmdLower != "/sethonor" &&
		cmdLower != "/title" &&
		cmdLower != "/reloadtitle" &&
		cmdLower != "/finishquest" &&
		cmdLower != "/questfinish" &&
		cmdLower != "/questdone" &&
		cmdLower != "/fq" &&
		cmdLower != "/resetquests" &&
		cmdLower != "/questreset" &&
		cmdLower != "/resetquest" &&
		cmdLower != "/rq" &&
		cmdLower != "/rdq" &&
		cmdLower != "/resetdailyquest" &&
		cmdLower != "/resetdailyquests" &&
		cmdLower != "/rwq" &&
		cmdLower != "/resetweeklyquest" &&
		cmdLower != "/resetweeklyquests" &&
		cmdLower != "/rsq" &&
		cmdLower != "/resetspecialquest" &&
		cmdLower != "/resetquestcount" &&
		cmdLower != "/questresetcount" &&
		cmdLower != "/rqc" &&
		cmdLower != "/reloadquest" &&
		cmdLower != "/questreload" &&
		cmdLower != "/qmp" &&
		cmdLower != "/questminplayers" &&
		cmdLower != "/qe" &&
		cmdLower != "/questevent" &&
		cmdLower != "/setexpire" &&
		cmdLower != "/additemsec") {
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

	if (cmdLower == "/additem" || cmdLower == "/addallitem" || cmdLower == "/deleteitem" || cmdLower == "/delitem" || cmdLower == "/resetinv" || cmdLower == "/resetitem" ||
		cmdLower == "/addpoints" || cmdLower == "/addgash" || cmdLower == "/setlevel" || cmdLower == "/level" || cmdLower == "/addexp" ||
		cmdLower == "/setzombiestats" || cmdLower == "/zombiestats" ||
		cmdLower == "/setexpire" || cmdLower == "/additemsec") {
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
	else if (cmdLower == "/reloadreward" || cmdLower == "/reloadrewarditem") {
		if (!serverConfig.permissions.allowPlayerReloadShop && !isGM) {
			SendNotice("權限不足：配置重載指令已被管理員限制，僅限 GM 使用！");
			return true;
		}
	}
	else if (cmdLower == "/reloadquest" || cmdLower == "/questreload") {
		if (!serverConfig.permissions.allowPlayerReloadQuest && !isGM) {
			SendNotice("權限不足：任務配置重載指令已被管理員限制，僅限 GM 使用！");
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
						ownedItem.expirationTime = 0;
						packetManager.SendPacket_InventoryDelta(connection, { ownedItem }, {});
						SendNotice(format("[系統] 物品「{}」(ID: {}) 原為期限物品，已成功升級為永久！", itemDef->name, itemID));
						AwardItem award;
						award.itemID = itemID;
						award.count = 1;
						award.duration = 0xFFFF;
						award.flags = 0;
						SendPacket_UMsg_ItemAwardNotice(connection, { award }, "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_TITLE", "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_GIVE");
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
						ownedItem.expirationTime = newExp;
						packetManager.SendPacket_InventoryDelta(connection, { ownedItem }, {});
						SendNotice(format("[系統] 物品「{}」(ID: {}) 成功延長 {} 天期限！", itemDef->name, itemID, days));
						AwardItem award;
						award.itemID = itemID;
						award.count = 1;
						award.duration = (unsigned short)days;
						award.flags = 0;
						SendPacket_UMsg_ItemAwardNotice(connection, { award }, "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_TITLE", "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_GIVE");
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
				packetManager.SendPacket_InventoryDelta(connection, { newItem }, {});
				if (expirationTime == 0) {
					SendNotice(format("[系統] 成功添加「{}」(ID: {})，期限：永久。", itemDef->name, itemID));
				}
				else {
					SendNotice(format("[系統] 成功添加「{}」(ID: {})，期限：{} 天。", itemDef->name, itemID, days));
				}
				AwardItem award;
				award.itemID = itemID;
				award.count = 1;
				award.duration = (expirationTime == 0) ? 0xFFFF : (unsigned short)days;
				award.flags = 0;
				SendPacket_UMsg_ItemAwardNotice(connection, { award }, "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_TITLE", "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_GIVE");
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
						ownedItem.count += addCount;
						packetManager.SendPacket_InventoryDelta(connection, { ownedItem }, {});
						SendNotice(format("[系統] 物品「{}」(ID: {}) 已在倉庫，疊加數量 +{}，總計：{} 個。", itemDef->name, itemID, addCount, min((unsigned int)ownedItem.count, 999U)));
						AwardItem award;
						award.itemID = itemID;
						award.count = addCount;
						award.duration = 0;
						award.flags = 0;
						SendPacket_UMsg_ItemAwardNotice(connection, { award }, "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_TITLE", "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_GIVE");
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
				packetManager.SendPacket_InventoryDelta(connection, { newItem }, {});
				SendNotice(format("[系統] 成功添加「{}」(ID: {})，數量：{} 個。", itemDef->name, itemID, count));
				AwardItem award;
				award.itemID = itemID;
				award.count = count;
				award.duration = 0;
				award.flags = 0;
				SendPacket_UMsg_ItemAwardNotice(connection, { award }, "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_TITLE", "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_GIVE");
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
		std::vector<InventoryItem> deltaUpdatedItems;
		if (!itemsToAdd.empty()) {
			if (databaseManager.AddUserInventoryItems(userID, itemsToAdd)) {
				changed = true;
				deltaUpdatedItems.insert(deltaUpdatedItems.end(), itemsToAdd.begin(), itemsToAdd.end());
			}
		}

		for (auto item : itemsToUpgradePermanent) {
			if (databaseManager.UpdateUserInventoryExpiration(userID, item.itemID, 0)) {
				changed = true;
				item.expirationTime = 0;
				deltaUpdatedItems.push_back(item);
			}
		}

		for (const auto& update : itemsToExtend) {
			if (databaseManager.UpdateUserInventoryExpiration(userID, update.first, update.second)) {
				changed = true;
				for (const auto& invItem : inventory) {
					if (invItem.itemID == update.first) {
						InventoryItem updated = invItem;
						updated.expirationTime = update.second;
						deltaUpdatedItems.push_back(updated);
						break;
					}
				}
			}
		}

		if (changed) {
			packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, {});
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
	else if (cmdLower == "/deleteitem" || cmdLower == "/delitem") {
		string itemIDStr;
		if (!(ss >> itemIDStr)) {
			SendNotice("用法：/deleteitem <物品ID> [數量] (別名：/delitem，未指定數量則全部刪除)");
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

		auto inventory = user->GetUserInventory();
		bool owned = false;
		for (const auto& item : inventory) {
			if (item.itemID == itemID) {
				owned = true;
				break;
			}
		}

		if (!owned) {
			SendNotice(format("[系統] 您的倉庫中找不到 ID 為 {} 的物品！", itemID));
			return true;
		}

		string p2Str;
		bool hasP2 = false;
		unsigned short countToDelete = 0;
		if (ss >> p2Str) {
			try {
				countToDelete = (unsigned short)min(stoul(p2Str), 65535UL);
				hasP2 = true;
			}
			catch (...) {}
		}

		vector<unsigned short> deletedSlots;
		unsigned short actualDeleted = 0;
		if (databaseManager.DeleteUserInventoryItem(userID, itemID, hasP2 ? countToDelete : 0, deletedSlots, actualDeleted)) {
			// 1. Realtime sync updated inventory to client (Delta 增量同步)
			packetManager.SendPacket_InventoryDelta(connection, {}, deletedSlots);

			// 2. Pop up native ItemExpireNotice dialog (Packet 67 Type 62: CSO_ItemExpireTitle / CSO_ItemExpireMsg with item icon)
			SendPacket_UMsg_ItemExpireNotice(connection, { itemID });

			// 3. Send system notice in chat
			const ItemDefinition* itemDef = itemDefinitionManager.GetItem(itemID);
			string itemName = (itemDef != NULL) ? itemDef->name : format("物品 (ID: {})", itemID);
			SendNotice(format("[系統] 成功刪除「{}」(ID: {})，數量：{} 個。", itemName, itemID, actualDeleted));
		}
		else {
			SendNotice("[系統] 刪除物品失敗，數據庫操作錯誤！");
		}

		return true;
	}
	else if (cmdLower == "/setexpire") {
		string itemIDStr, secStr;
		if (!(ss >> itemIDStr) || !(ss >> secStr)) {
			SendNotice("用法：/setexpire <物品ID> <秒數> (將背包中指定物品的到期時間設定為 N 秒後)");
			return true;
		}

		unsigned short itemID = 0;
		unsigned int seconds = 0;
		try {
			itemID = (unsigned short)stoul(itemIDStr);
			seconds = (unsigned int)stoul(secStr);
		}
		catch (...) {
			SendNotice("[系統] 格式錯誤，物品 ID 與秒數必須為數字。");
			return true;
		}

		if (databaseManager.SetUserInventoryItemExpirationSeconds(userID, itemID, seconds)) {
			vector<InventoryItem> deltaUpdatedItems;
			for (const auto& it : user->GetUserInventory()) {
				if (it.itemID == itemID) {
					deltaUpdatedItems.push_back(it);
					break;
				}
			}
			packetManager.SendPacket_InventoryDelta(connection, deltaUpdatedItems, {});
			const ItemDefinition* itemDef = itemDefinitionManager.GetItem(itemID);
			string itemName = (itemDef != NULL) ? itemDef->name : format("物品 (ID: {})", itemID);
			SendNotice(format("[系統] 成功將「{}」(ID: {}) 的有效期限設定為 {} 秒後到期！", itemName, itemID, seconds));
		}
		else {
			SendNotice("[系統] 設定失敗，您的背包中找不到該物品！");
		}

		return true;
	}
	else if (cmdLower == "/additemsec") {
		string itemIDStr, secStr;
		if (!(ss >> itemIDStr) || !(ss >> secStr)) {
			SendNotice("用法：/additemsec <物品ID> <秒數> (添加一個在 N 秒後到期的指定道具)");
			return true;
		}

		unsigned short itemID = 0;
		unsigned int seconds = 0;
		try {
			itemID = (unsigned short)stoul(itemIDStr);
			seconds = (unsigned int)stoul(secStr);
		}
		catch (...) {
			SendNotice("[系統] 格式錯誤，物品 ID 與秒數必須為數字。");
			return true;
		}

		const ItemDefinition* itemDef = itemDefinitionManager.GetItem(itemID);
		if (itemDef == NULL) {
			SendNotice(format("[系統] 找不到 ID 為 {} 的物品！", itemID));
			return true;
		}

		auto inventory = user->GetUserInventory();
		unsigned short maxSlotID = 0;
		for (const auto& item : inventory) {
			if (item.slotID > maxSlotID) {
				maxSlotID = item.slotID;
			}
		}

		time_t now = time(nullptr);
		unsigned long expTime = (unsigned long)now + seconds;
		InventoryItem newItem;
		newItem.slotID = maxSlotID + 1;
		newItem.itemID = itemID;
		newItem.count = 1;
		newItem.activeFlag = 1;
		newItem.inUse = false;
		newItem.obtainTime = (unsigned long)now;
		newItem.expirationTime = expTime;
		newItem.enhanceBits = 0;

		if (databaseManager.AddUserInventoryItems(userID, { newItem })) {
			packetManager.SendPacket_InventoryDelta(connection, { newItem }, {});
			AwardItem award;
			award.itemID = itemID;
			award.count = 1;
			award.duration = 1;
			award.flags = 0;
			SendPacket_UMsg_ItemAwardNotice(connection, { award }, "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_TITLE", "CSO_Warning_HSCENARIO_MONSTER_DROP_ITEM_GIVE");
			SendNotice(format("[系統] 成功添加「{}」(ID: {})，將在 {} 秒後到期！", itemDef->name, itemID, seconds));
		}
		else {
			SendNotice("[系統] 添加物品失敗，數據庫操作錯誤！");
		}

		return true;
	}
	else if (cmdLower == "/bighead") {
		if (user->GetUserStatus() != UserStatus::InRoom) {
			SendNotice("[房間] 必須在房間內使用此指令！");
			return true;
		}

		Room* room = roomManager.GetRoomByRoomID(user->GetCurrentRoomID());
		if (!room) {
			SendNotice("[房間] 找不到當前房間！");
			return true;
		}

		if (room->GetRoomHostUser() != user) {
			SendNotice("[房間] 只有室長可以調整大頭模式！");
			return true;
		}

		unsigned char modeID = room->GetRoomSettings().gameModeID;
		if (!IsBigHeadCompatibleMode(modeID)) {
			SendNotice("[房間] 大頭模式僅適用於災厄之章/庇護所/殭屍/BOT等支援模式！");
			return true;
		}

		if (!user->HasItem(439) && !isGM) {
			SendNotice("[房間] 您未持有「大頭製造器」，無法啟用大頭模式！");
			return true;
		}

		string subCmd;
		ss >> subCmd;
		transform(subCmd.begin(), subCmd.end(), subCmd.begin(), [](unsigned char c) { return (char)tolower(c); });

		bool enable = false;
		if (subCmd == "on" || subCmd == "1" || subCmd == "true") {
			enable = true;
		}
		else if (subCmd == "off" || subCmd == "0" || subCmd == "false") {
			enable = false;
		}
		else {
			enable = (room->GetRoomSettings().unkh1000 == 0);
		}

		room->SetBigHeadDisabled(!enable);

		RoomSettings updateSettings;
		updateSettings.highFlag = ROOMSETTINGS_HFLAG_UNK1000;
		updateSettings.unkh1000 = enable ? 1 : 0;

		room->UpdateRoomSettings(updateSettings);

		for (auto& u : room->GetRoomUsers()) {
			if (u && u->GetConnection()) {
				packet_RoomManager.SendPacket_Room_ReplyUpdateRoomSettings(u->GetConnection(), updateSettings);
			}
		}

		roomManager.SendUpdateRoomPacketToAll(room, ROOMLIST_FLAG_UNK2000);

		if (enable) {
			SendNotice("[房間] 大頭模式已開啟！");
		}
		else {
			SendNotice("[房間] 大頭模式已關閉。");
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
	else if (cmdLower == "/setlevel" || cmdLower == "/level") {
		string lvStr;
		if (!(ss >> lvStr)) {
			SendNotice("[系統] 用法：/setlevel <等級> (1~72)");
			return true;
		}

		unsigned char lv = 0;
		try {
			lv = (unsigned char)stoul(lvStr);
		}
		catch (...) {
			SendNotice("[系統] 請輸入有效的等級 (1~72)！用法：/setlevel <等級>");
			return true;
		}

		if (lv < 1) lv = 1;
		if (lv > 72) lv = 72;

		if (databaseManager.SetUserLevel(userID, lv)) {
			UserCharacterResult ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });
			packet_MessengerManager.SendPacket_Messenger_ChatCount(connection, 255);
			SendNotice(format("[系統] 您的等級已成功設置為 {} 級 (經驗值: {})！", (int)lv, ucr.userCharacter.exp));
		}
		else {
			SendNotice("[系統] 設置等級失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/addexp") {
		string expStr;
		if (!(ss >> expStr)) {
			SendNotice("[系統] 用法：/addexp <經驗值>");
			return true;
		}

		unsigned long long expAdd = 0;
		try {
			expAdd = stoull(expStr);
		}
		catch (...) {
			SendNotice("[系統] 請輸入有效的經驗值！用法：/addexp <經驗值>");
			return true;
		}

		if (expAdd == 0) {
			SendNotice("[系統] 請輸入大於 0 的有效經驗值！");
			return true;
		}

		unsigned char newLevel = 0;
		if (databaseManager.AddUserExp(userID, expAdd, &newLevel)) {
			UserCharacterResult ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });
			packet_MessengerManager.SendPacket_Messenger_ChatCount(connection, 255);
			SendNotice(format("[系統] 成功增加經驗值：+{}！當前等級：{} 級，累計經驗值：{}。", expAdd, (int)newLevel, ucr.userCharacter.exp));
		}
		else {
			SendNotice("[系統] 增加經驗值失敗，數據庫寫入錯誤！");
		}
		return true;
	}
	else if (cmdLower == "/setzombiestats" || cmdLower == "/zombiestats") {
		string killsStr, survsStr;
		if (!(ss >> killsStr >> survsStr)) {
			SendNotice("[系統] 用法：/setzombiestats <殭屍擊殺數> <生存次數>");
			return true;
		}

		unsigned int kills = 0, survs = 0;
		try {
			kills = (unsigned int)stoul(killsStr);
			survs = (unsigned int)stoul(survsStr);
		}
		catch (...) {
			SendNotice("[系統] 請輸入有效的數字！用法：/setzombiestats <殭屍擊殺數> <生存次數>");
			return true;
		}

		if (databaseManager.SetUserZombieStats(userID, kills, survs)) {
			UserCharacterResult ucr = user->GetUserCharacter(USERCHARACTER_FLAG_ALL);
			packet_UpdateInfoManager.SendPacket_UpdateInfo({ user, ucr.userCharacter });
			packet_MessengerManager.SendPacket_Messenger_ChatCount(connection, 255);
			SendNotice(format("[系統] 成功設置個人狀態殭屍擊殺數: {}，生存次數: {}！", kills, survs));
		}
		else {
			SendNotice("[系統] 設置失敗，數據庫寫入錯誤！");
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
	else if (cmdLower == "/reloadreward" || cmdLower == "/reloadrewarditem") {
		if (rewardItemManager.Reload()) {
			SendNotice(format("[系統] 獎勵道具配置已成功重載！共載入 {} 個獎勵道具。(configs/rewarditem.json)",
				rewardItemManager.GetRewardItemCount()));
		}
		else {
			SendNotice("[系統] 重載獎勵道具配置失敗，請檢查 configs/rewarditem.json 格式！");
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
	else if (cmdLower == "/finishquest" || cmdLower == "/questfinish" || cmdLower == "/questdone" || cmdLower == "/fq") {
		string qidStr;
		if (!(ss >> qidStr)) {
			unsigned int currentSpecial = questManager.GetUserDailySpecialQuestID(user);
			const QuestConfig* spCfg = questManager.GetQuest(currentSpecial);
			string spName = spCfg ? spCfg->name : "未知";
			SendNotice("[系統] 用法：/fq <QuestID> (或輸入 /fq special 直接完成當前特殊任務)");
			SendNotice(format("[系統] 提示：您當前的特殊任務 ID 為 [{}]【{}】。", currentSpecial, spName));
			return true;
		}

		unsigned int qid = 0;
		if (qidStr == "special" || qidStr == "s") {
			qid = questManager.GetUserDailySpecialQuestID(user);
		}
		else {
			try {
				qid = stoul(qidStr);
			}
			catch (...) {
				SendNotice("[系統] 請輸入有效的任務 ID (或輸入 /fq special)！");
				return true;
			}
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
	else if (cmdLower == "/questreset" || cmdLower == "/resetquest" || cmdLower == "/resetquests" ||
			cmdLower == "/rq" || cmdLower == "/rsq" || cmdLower == "/resetspecialquest") {
		string arg1;
		if (!(ss >> arg1) || cmdLower == "/rsq" || cmdLower == "/resetspecialquest") {
			// 無參數 或 /rsq：默認重設當日特殊任務為未完成狀態
			unsigned int targetID = 0;
			if (!arg1.empty() && (cmdLower == "/rsq" || cmdLower == "/resetspecialquest")) {
				try { targetID = stoul(arg1); } catch (...) {}
			}
			if (questManager.ForceResetSpecialQuest(user, targetID)) {
				unsigned int curSp = (targetID > 0) ? targetID : questManager.GetUserDailySpecialQuestID(user);
				const QuestConfig* spCfg = questManager.GetQuest(curSp);
				string spName = spCfg ? spCfg->name : "特殊任務";
				SendNotice(format("[系統] 已成功重置特殊任務 [{}]【{}】為進行中（未完成）狀態！", curSp, spName));
			}
			else {
				SendNotice("[系統] 重置特殊任務失敗！");
			}
			return true;
		}

		string argLower = arg1;
		transform(argLower.begin(), argLower.end(), argLower.begin(), [](unsigned char c) { return (char)tolower(c); });

		if (argLower == "special" || argLower == "s") {
			unsigned int targetID = 0;
			ss >> targetID;
			if (questManager.ForceResetSpecialQuest(user, targetID)) {
				unsigned int curSp = (targetID > 0) ? targetID : questManager.GetUserDailySpecialQuestID(user);
				const QuestConfig* spCfg = questManager.GetQuest(curSp);
				string spName = spCfg ? spCfg->name : "特殊任務";
				SendNotice(format("[系統] 已成功重置特殊任務 [{}]【{}】為進行中（未完成）狀態！", curSp, spName));
			}
			else {
				SendNotice("[系統] 重置特殊任務失敗，請檢查任務 ID 是否正確！");
			}
			return true;
		}
		else if (argLower == "daily" || argLower == "d") {
			unsigned int specificQID = 0;
			ss >> specificQID;
			if (questManager.ResetDailyQuests(user, specificQID)) {
				if (specificQID > 0) {
					const QuestConfig* qCfg = questManager.GetQuest(specificQID);
					string qName = qCfg ? qCfg->name : to_string(specificQID);
					SendNotice(format("[系統] 已成功重置每日任務 [{}]【{}】為進行中（未完成）狀態！", specificQID, qName));
				}
				else {
					SendNotice("[系統] 已成功重置所有每日任務為進行中（未完成）狀態！");
				}
			}
			else {
				SendNotice("[系統] 重置每日任務失敗！");
			}
			return true;
		}
		else if (argLower == "weekly" || argLower == "w") {
			unsigned int specificQID = 0;
			ss >> specificQID;
			if (questManager.ResetWeeklyQuests(user, specificQID)) {
				if (specificQID > 0) {
					const QuestConfig* qCfg = questManager.GetQuest(specificQID);
					string qName = qCfg ? qCfg->name : to_string(specificQID);
					SendNotice(format("[系統] 已成功重置每周任務 [{}]【{}】為進行中（未完成）狀態！", specificQID, qName));
				}
				else {
					SendNotice("[系統] 已成功重置所有每周任務為進行中（未完成）狀態！");
				}
			}
			else {
				SendNotice("[系統] 重置每周任務失敗！");
			}
			return true;
		}
		else if (argLower == "all" || argLower == "a") {
			if (questManager.ResetAllQuests(user)) {
				SendNotice("[系統] 已成功將所有每日任務、每周任務與特殊任務重置為進行中（未完成）狀態！");
			}
			else {
				SendNotice("[系統] 重置任務失敗！");
			}
			return true;
		}
		else {
			// 數字參數：自動判斷 ID 類型
			try {
				unsigned int qid = stoul(arg1);
				if (qid >= 1001 && qid <= 1019) {
					// 指定切換並重設特殊任務
					if (questManager.ForceResetSpecialQuest(user, qid)) {
						const QuestConfig* spCfg = questManager.GetQuest(qid);
						string spName = spCfg ? spCfg->name : "特殊任務";
						SendNotice(format("[系統] 已成功切換並重置特殊任務為 [{}]【{}】（進行中狀態）！", qid, spName));
					}
					else {
						SendNotice(format("[系統] 特殊任務 ID {} 無效或重置失敗！", qid));
					}
				}
				else if (qid <= 100) {
					// 每日任務
					if (questManager.ResetDailyQuests(user, qid)) {
						const QuestConfig* qCfg = questManager.GetQuest(qid);
						string qName = qCfg ? qCfg->name : to_string(qid);
						SendNotice(format("[系統] 已成功重置每日任務 [{}]【{}】為進行中（未完成）狀態！", qid, qName));
					}
					else {
						SendNotice(format("[系統] 每日任務 ID {} 重置失敗！", qid));
					}
				}
				else if (qid >= 501 && qid <= 600) {
					// 每周任務
					if (questManager.ResetWeeklyQuests(user, qid)) {
						const QuestConfig* qCfg = questManager.GetQuest(qid);
						string qName = qCfg ? qCfg->name : to_string(qid);
						SendNotice(format("[系統] 已成功重置每周任務 [{}]【{}】為進行中（未完成）狀態！", qid, qName));
					}
					else {
						SendNotice(format("[系統] 每周任務 ID {} 重置失敗！", qid));
					}
				}
				else {
					// 其他任務 (如榮譽任務等)
					if (questManager.ResetSingleQuest(user, qid)) {
						const QuestConfig* qCfg = questManager.GetQuest(qid);
						string qName = qCfg ? qCfg->name : to_string(qid);
						SendNotice(format("[系統] 已成功重置任務 [{}]【{}】為進行中（未完成）狀態！", qid, qName));
					}
					else {
						SendNotice(format("[系統] 任務 ID {} 不存在或重置失敗！", qid));
					}
				}
			}
			catch (...) {
				SendNotice("[系統] 用法：/questreset [ID | daily | weekly | special [ID] | all]");
				SendNotice("[系統] 提示：直接輸入 /questreset 默認重置當日特殊任務；/questreset 1005 可重設為指定特殊任務。");
			}
			return true;
		}
	}
	else if (cmdLower == "/rdq" || cmdLower == "/resetdailyquest" || cmdLower == "/resetdailyquests") {
		unsigned int specificQID = 0;
		ss >> specificQID;
		if (questManager.ResetDailyQuests(user, specificQID)) {
			if (specificQID > 0) {
				const QuestConfig* qCfg = questManager.GetQuest(specificQID);
				string qName = qCfg ? qCfg->name : to_string(specificQID);
				SendNotice(format("[系統] 已成功重置每日任務 [{}]【{}】為進行中（未完成）狀態！", specificQID, qName));
			}
			else {
				SendNotice("[系統] 已成功重置所有每日任務為進行中（未完成）狀態！");
			}
		}
		else {
			SendNotice("[系統] 重置每日任務失敗！");
		}
		return true;
	}
	else if (cmdLower == "/rwq" || cmdLower == "/resetweeklyquest" || cmdLower == "/resetweeklyquests") {
		unsigned int specificQID = 0;
		ss >> specificQID;
		if (questManager.ResetWeeklyQuests(user, specificQID)) {
			if (specificQID > 0) {
				const QuestConfig* qCfg = questManager.GetQuest(specificQID);
				string qName = qCfg ? qCfg->name : to_string(specificQID);
				SendNotice(format("[系統] 已成功重置每周任務 [{}]【{}】為進行中（未完成）狀態！", specificQID, qName));
			}
			else {
				SendNotice("[系統] 已成功重置所有每周任務為進行中（未完成）狀態！");
			}
		}
		else {
			SendNotice("[系統] 重置每周任務失敗！");
		}
		return true;
	}
	else if (cmdLower == "/resetquestcount" || cmdLower == "/questresetcount" || cmdLower == "/rqc") {
		questManager.ResetUserSpecialQuestResetCount(user);
		SendNotice("[系統] 已成功重設您的特殊任務重置次數！剩餘次數：5 次。");
		return true;
	}
	else if (cmdLower == "/reloadquest" || cmdLower == "/questreload") {
		if (questManager.Reload()) {
			SendNotice("[系統] 任務與獎勵池配置已成功重載！(configs/quest.json)");
		}
		else {
			SendNotice("[系統] 重載任務配置失敗，請檢查 configs/quest.json 格式！");
		}
		return true;
	}
	else if (cmdLower == "/qmp" || cmdLower == "/questminplayers") {
		string countStr;
		if (ss >> countStr) {
			try {
				int cnt = stoi(countStr);
				if (cnt >= 1 && cnt <= 32) {
					serverConfig.questMinPlayers = (unsigned char)cnt;
					SendNotice(format("[系統] 任務最少玩家人數門檻已動態設置為：{} 人！", cnt));
				}
				else {
					SendNotice("[系統] 請輸入有效的人數範圍 (1~32)！");
				}
			}
			catch (...) {
				SendNotice("[系統] 用法：/qmp <人數> (預設為 4，測試時可設為 1)");
			}
		}
		else {
			SendNotice(format("[系統] 當前任務最少玩家人數門檻為：{} 人。(用法：/qmp <人數>)", serverConfig.questMinPlayers));
		}
		return true;
	}
	else if (cmdLower == "/qe" || cmdLower == "/questevent") {
		string evtType;
		if (!(ss >> evtType)) {
			SendNotice("[系統] 用法：/qe <c4_plant|c4_defuse|hostage|morale|supply|wall|boss|skill|capture>");
			return true;
		}
		transform(evtType.begin(), evtType.end(), evtType.begin(), [](unsigned char c) { return (char)tolower(c); });
		if (evtType == "c4_plant") {
			questManager.OnBombEvent(user, true);
			SendNotice("[系統] 已觸發事件：C4 安裝 (每日任務 [4] 爆破專家)");
		}
		else if (evtType == "c4_defuse") {
			questManager.OnBombEvent(user, false);
			SendNotice("[系統] 已觸發事件：C4 拆除 (每日任務 [4] 爆破專家)");
		}
		else if (evtType == "hostage") {
			questManager.OnHostageRescued(user);
			SendNotice("[系統] 已觸發事件：解救人質 (每日任務 [5] 人質危機)");
		}
		else if (evtType == "morale") {
			questManager.OnMoraleBoost(user);
			SendNotice("[系統] 已觸發事件：士氣提升 (每日任務 [11] 士氣提升)");
		}
		else if (evtType == "supply") {
			questManager.OnSupplyBoxPicked(user);
			SendNotice("[系統] 已觸發事件：拾取補給箱 (每日任務 [14] 奪回補給箱)");
		}
		else if (evtType == "wall") {
			questManager.OnWallUpgraded(user);
			SendNotice("[系統] 已觸發事件：升級防線 (每日任務 [17] 銅牆鐵壁)");
		}
		else if (evtType == "boss" || evtType == "starman" || evtType == "titan") {
			string bossName = (evtType == "titan") ? "titan" : "starman";
			questManager.OnBossKilled(user, bossName);
			SendNotice(format("[系統] 已觸發事件：擊殺 Boss {} (每日任務 [16]殘暴達叔 / [18]獵殺泰坦 / 榮譽)", bossName));
		}
		else if (evtType == "skill") {
			questManager.OnSpecialSkillKill(user);
			SendNotice("[系統] 已觸發事件：特殊技能擊殺 (每日任務 [19] 空中轟炸)");
		}
		else if (evtType == "capture") {
			questManager.OnPointCaptured(user);
			SendNotice("[系統] 已觸發事件：佔領據點 (每日任務 [21] 佔領據點)");
		}
		else {
			SendNotice("[系統] 未知事件！支援事件：c4_plant, c4_defuse, hostage, morale, supply, wall, boss, skill, capture");
		}
		return true;
	}
	else if (cmdLower == "/testbox") {
		string boxIdStr;
		unsigned int boxID = 3000;
		if (ss >> boxIdStr) {
			try {
				boxID = (unsigned int)stoul(boxIdStr);
			}
			catch (...) {}
		}
		if (scenarioRewardManager.HandleBoxPickup(user, boxID)) {
			SendNotice(format("[系統] 成功模擬拾取補給箱 ID: {}！", boxID));
		}
		else {
			SendNotice(format("[系統] 模擬拾取補給箱 ID: {} 失敗（箱子未配置或功能未開啟）！", boxID));
		}
		return true;
	}
	else if (cmdLower == "/reloadscenario") {
		if (scenarioRewardManager.Reload()) {
			SendNotice(format("[系統] 災厄之章獎勵配置已成功重載！共載入 {} 個地圖獎勵、{} 個補給箱配置。(configs/scenario_reward.json)",
				scenarioRewardManager.GetRewardCount(), scenarioRewardManager.GetBoxRewardCount()));
		}
		else {
			SendNotice("[系統] 重載災厄之章獎勵配置失敗，請檢查 configs/scenario_reward.json 格式！");
		}
		return true;
	}
	else if (cmdLower == "/reloadbonus") {
		if (battleBonusManager.Reload()) {
			SendNotice("[系統] 對局結算獎勵與加成配置已成功重載！(configs/battle_bonus.json)");
		}
		else {
			SendNotice("[系統] 重載對局獎勵配置失敗，請檢查 configs/battle_bonus.json 格式！");
		}
		return true;
	}

	return false;
}
