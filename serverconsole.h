#pragma once
#include <thread>
#include <string>

using namespace std;

#ifdef GetCurrentTime
#undef GetCurrentTime
#endif

typedef void* HANDLE;

enum PrefixType {
	NoPrefix = 0,
	Info = 1,
	Warn = 2,
	Error = 3,
	Fatal = 4,
	Debug = 5
};

constexpr auto INFO_PREFIX = "[ Info ]";
constexpr auto WARN_PREFIX = "[ Warn ]";
constexpr auto ERROR_PREFIX = "[ Error ]";
constexpr auto FATAL_PREFIX = "[ Fatal ]";
constexpr auto DEBUG_PREFIX = "[ Debug ]";

class ServerConsole {
public:
	ServerConsole();
	~ServerConsole();

	bool Init();
	void Start();
	void Stop();
	void Log(PrefixType prefixType, const string& text);
	void Print(PrefixType prefixType, const string& text, bool logToFile = true);
	void StartRead();

	unsigned long long GetCurrentTime() const noexcept {
		return _currentTime;
	}
private:
	int run();
	int shutdown();
	void onSecondTick();
	void onMinuteTick();
	void log(const string& text);

private:
	thread _serverConsoleThread;
	bool _running;
	time_t _currentTime;
	string _currentTimeStr;
	char _secondCount;
	HANDLE _consoleOutput;
	string _logFile;
};

std::string Cp950ToUtf8(const std::string& cp950Str);
std::string Utf8ToCp950(const std::string& utf8Str);

extern ServerConsole serverConsole;