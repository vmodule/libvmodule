/*
 * Logger.h
 *
 *  Created on: Jan 1, 2017
 *      Author: jeffrey
 */

#pragma once

#include <stdio.h>
#include <string>
#include <stdarg.h>
#include <memory>
#include <vutils/SingletonHelper.h>
#include <threads/Condition.h>

#define LOG_LEVEL_NONE         -1 // nothing at all is logged
#define LOG_LEVEL_NORMAL        0 // shows notice, error, severe and fatal
#define LOG_LEVEL_DEBUG         1 // shows all
#define LOG_LEVEL_DEBUG_FREEMEM 2 // shows all + shows freemem on screen
#define LOG_LEVEL_MAX           LOG_LEVEL_DEBUG_FREEMEM

// ones we use in the code
#define LOGDEBUG   0
#define LOGINFO    1
#define LOGNOTICE  2
#define LOGWARNING 3
#define LOGERROR   4
#define LOGSEVERE  5
#define LOGFATAL   6
#define LOGNONE    7
// extra masks - from bit 5
#define LOGMASKBIT  5
#define LOGMASK     ((1 << LOGMASKBIT) - 1)

namespace vmodule {
class StringUtils;
class Logger {
public:
	Logger();
	virtual ~Logger();
	static void Close();
	static void Log(int loglevel, const char *format, ...);
	static void Log(int loglevel, const char *tag, const char *format, ...);
	static void LogFunction(int loglevel, const char* functionName,
			const char* format, ...);
	static bool Init(const std::string& path);
	static void PrintDebugString(const std::string& line); // universal interface for printing debug strings
	static void MemDump(char *pData, int length);
	static void SetLogLevel(int level);
	static int GetLogLevel();
	static void SetExtraLogLevels(int level);
	static bool IsLogLevelLogged(int loglevel);
#if defined(TARGET_ANDROID)
	static int android_printf(int logLevel, const char *tag, const char *msg, ...);
#endif
protected:
	class LoggerGlobals {
	public:
		LoggerGlobals() :
				m_repeatCount(0), m_repeatLogLevel(-1), m_logLevel(
						LOG_LEVEL_DEBUG), m_extraLogLevels(0) {
		}
		~LoggerGlobals() {
		}
		int m_repeatCount;
		int m_repeatLogLevel;
		std::string m_repeatLine;
		int m_logLevel;
		int m_extraLogLevels;
		CCriticalSection critSec;
	};
	class LoggerGlobals m_globalInstance; // used as static global variable
	static void LogString(int logLevel, const std::string& logString);
	static bool WriteLogString(int logLevel, const std::string& logString);
};

}

#define LogF(loglevel,format,...) vmodule::Logger::LogFunction((loglevel),__FUNCTION__,(format),##__VA_ARGS__)
#define INFOD(format,...) 		vmodule::Logger::Log(LOGDEBUG,format,##__VA_ARGS__)
#define INFOI(format,...) 		vmodule::Logger::Log(LOGINFO,format,##__VA_ARGS__)
#define INFOW(format,...) 		vmodule::Logger::Log(LOGWARNING,format,##__VA_ARGS__)
#define INFOE(format,...) 		vmodule::Logger::Log(LOGERROR,format,##__VA_ARGS__)

#if defined(_DEBUG)
#define XLOGD(tag,format,...) 	vmodule::Logger::Log(LOGDEBUG,tag,format,##__VA_ARGS__)
#define XLOGI(tag,format,...) 	vmodule::Logger::Log(LOGINFO,tag,format,##__VA_ARGS__)
#define XLOGW(tag,format,...) 	vmodule::Logger::Log(LOGWARNING,tag,format,##__VA_ARGS__)
#define XLOGE(tag,format,...) 	vmodule::Logger::Log(LOGERROR,tag,format,##__VA_ARGS__)
#else
#define XLOGD(tag,format,...)
#define XLOGI(tag,format,...)
#define XLOGW(tag,format,...) 	vmodule::Logger::Log(LOGWARNING,tag,format,##__VA_ARGS__)
#define XLOGE(tag,format,...) 	vmodule::Logger::Log(LOGERROR,tag,format,##__VA_ARGS__)
#endif

#define CONDITION(cond)     (__builtin_expect((cond)!=0, 0))

#ifndef _DEBUG
#define XLOGD_IF(cond,tag,format,...)   ((void)0)
#else
#define XLOGD_IF(cond,tag,format,...) \
    ( (CONDITION(cond)) \
    ? ((void)vmodule::Logger::Log(LOGDEBUG,tag,format,##__VA_ARGS__) \
    : (void)0 )
#endif

//APPLICATION_GLOBAL_REF(vmodule::Logger, g_log);
//#define g_log APPLICATION_GLOBAL_USE(vmodule::Logger)

