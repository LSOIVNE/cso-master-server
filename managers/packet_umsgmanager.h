#pragma once
#include "user.h"
#include <string>
#include <vector>

enum Packet_UMsgType {
	WhisperChat = 0,
	ChannelChat = 1,
	RoomChat = 2,
	ClanChat = 3,
	TeamChat = 4,
	InfoMessage = 5,
	ServerYell = 6,
	PublicNotice = 10,
	ServerMessage = 11,
	SystemDialog = 20,
	SystemReply_Red = 20,
	SystemReply_Green = 21,
	WarningMessage = 30,
	RollingBanner = 10,
	SystemReply_MsgBox = 30,
	GMMessage = 40,
	GMWhisper = 41,
	DisconnectWarning = 50,
	ItemAwardNotice = 60,
	BulletinNotice = 65
};

std::string Utf8ToCp950(const std::string& utf8Str);
std::string Cp950ToUtf8(const std::string& cp950Str);

struct AwardItem {
	unsigned short itemID = 0;
	unsigned short duration = 0; // 天數 (0 表示計數/永久)
	unsigned short count = 0;    // 數量
	unsigned int flags = 0;      // 0
};

class Packet_UMsgManager {
public:
	void ParsePacket_UMsg(TCPConnection::Packet::pointer packet);

	void SendPacket_UMsg_PublicNotice(TCPConnection::pointer connection, const string& text);
	void SendPacket_UMsg_ServerMessageText(TCPConnection::pointer connection, const string& text);
	void SendPacket_UMsg_RollingBanner(TCPConnection::pointer connection, const string& text);
	void SendPacket_UMsg_ServerYell(TCPConnection::pointer connection, const string& senderNickName, const string& text);
	void SendPacket_UMsg_BulletinNotice(TCPConnection::pointer connection, unsigned short noticeID, unsigned char flags, const string& title, const string& content);
	void SendPacket_UMsg_ItemAwardNotice(TCPConnection::pointer connection, const vector<AwardItem>& items, const string& titleKey, const string& msgKey, const vector<string>& args = {});
	void SendPacket_UMsg_NoticeMessage(TCPConnection::pointer connection, const string& text);
	void SendPacket_UMsg_ServerMessage(TCPConnection::pointer connection, Packet_UMsgType type, const string& text, const vector<string>& additionalText = {});
	void SendPacket_UMsg_SystemReply(TCPConnection::pointer connection, Packet_UMsgType type, const string& replyKey, const vector<string>& args);
	void SendPacket_UMsg_Chat(TCPConnection::pointer connection, Packet_UMsgType type, const string& senderNickName, const string& text);

	void BroadcastPublicNotice(const string& text);
	void BroadcastServerMessage(const string& text);
	void BroadcastRollingBanner(const string& text);
	void BroadcastServerYell(const string& senderNickName, const string& text);
	void BroadcastBulletinNotice(unsigned short noticeID, unsigned char flags, const string& title, const string& content);
	void BroadcastSystemReply(Packet_UMsgType type, const string& replyKey, const vector<string>& args);

	bool HandleChatCommand(User* user, TCPConnection::pointer connection, const string& rawText, Packet_UMsgType chatType);

private:
	void parsePacket_UMsg_WhisperChat(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_UMsg_ChannelChat(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_UMsg_RoomChat(User* user, TCPConnection::Packet::pointer packet);
	void parsePacket_UMsg_ServerYell(User* user, TCPConnection::Packet::pointer packet);
};

extern Packet_UMsgManager packet_UMsgManager;