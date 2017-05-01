/*
 * Logger.cpp
 *
 *  Created on: Jan 1, 2017
 *      Author: jeffrey
 */

#include <iostream>
#include <string.h>
#if defined(TARGET_ANDROID)
#include <android/log.h> 
#endif
#include <vutils/Logger.h>
#include <vutils/StringUtils.h>

namespace vmodule {

static const char* const levelNames[] = { "DEBUG", "INFO", "NOTICE", "WARNING",
		"ERROR", "SEVERE", "FATAL", "NONE" };

// add 1 to level number to get index of name
static const char* const logLevelNames[] = { "LOG_LEVEL_NONE" /*-1*/,
		"LOG_LEVEL_NORMAL" /*0*/, "LOG_LEVEL_DEBUG" /*1*/,
		"LOG_LEVEL_DEBUG_FREEMEM" /*2*/};

#define s_globals APPLICATION_GLOBAL_USE(Logger).m_globalInstance

Logger::Logger() {}

Logger::~Logger() {}

void Logger::Close() {
	CSingleLock waitLock(s_globals.critSec);
	//s_globals.m_platform.CloseLogFile();
	s_globals.m_repeatLine.clear();
}

void Logger::Log(int loglevel, const char *format, ...) {
	if (IsLogLevelLogged(loglevel)) {
		va_list va;
		va_start(va, format);
		LogString(loglevel, StringUtils::FormatV(format, va));
		va_end(va);
	}
}

void Logger::Log(int loglevel, const char *tag, const char *format, ...) {
	if (IsLogLevelLogged(loglevel)) {
		std::string tagInfo;
		if (tag && tag[0] && (strlen(tag) != 0))
			tagInfo.assign(tag).append(": ");
		else
			tagInfo = "Logger:";
		va_list va;
		va_start(va, format);
		LogString(loglevel, tagInfo + StringUtils::FormatV(format, va));
		va_end(va);
	}
}

void Logger::LogFunction(int loglevel, const char* functionName,
		const char* format, ...) {
	if (IsLogLevelLogged(loglevel)) {
		std::string fNameStr;
		if (functionName && functionName[0])
			fNameStr.assign(functionName).append(": ");
		va_list va;
		va_start(va, format);
		LogString(loglevel, fNameStr + StringUtils::FormatV(format, va));
		va_end(va);
	}
}

bool Logger::Init(const std::string& path) {
	return true;
}

void Logger::PrintDebugString(const std::string& line) {
#if defined(TARGET_ANDROID)
	int tagIndex = line.find(":");
	std::string tag = line.substr(0,tagIndex);
	std::string logInfo = line.substr(tagIndex + 1);
	android_printf(GetLogLevel(),tag.c_str(),logInfo.c_str());
#elif defined(TARGET_POSIX)
	printf("%s\n",line.c_str());
	//printf("%s\n",logInfo.c_str());
#endif
}

#if defined(TARGET_ANDROID)
int Logger::android_printf(int logLevel, const char *tag, const char *msg, ...)
{
	va_list argptr;
	int result=-1;

	va_start(argptr, msg);
	switch (logLevel) {
		case LOGDEBUG:
		__android_log_vprint(ANDROID_LOG_DEBUG, tag, msg, argptr);
		result = 0;
		break;
		case LOGINFO:
		__android_log_vprint(ANDROID_LOG_INFO, tag, msg, argptr);
		result = 0;
		break;
		case LOGWARNING:
		__android_log_vprint(ANDROID_LOG_WARN, tag, msg, argptr);
		result = 0;
		break;
		case LOGERROR:
		__android_log_vprint(ANDROID_LOG_ERROR, tag, msg, argptr);
		result = -1;
		break;
		default:
		__android_log_vprint(ANDROID_LOG_VERBOSE, tag, msg, argptr);
		result = 0;
	}
	va_end(argptr);
	return result;
}
#endif

void Logger::MemDump(char *pData, int length) {
	Log(LOGDEBUG, "MEM_DUMP: Dumping from %p", pData);
	for (int i = 0; i < length; i += 16) {
		std::string strLine = StringUtils::Format("MEM_DUMP: %04x ", i);
		char *alpha = pData;
		for (int k = 0; k < 4 && i + 4 * k < length; k++) {
			for (int j = 0; j < 4 && i + 4 * k + j < length; j++) {
				std::string strFormat = StringUtils::Format(" %02x",
						(unsigned char) *pData++);
				strLine += strFormat;
			}
			strLine += " ";
		}
		// pad with spaces
		while (strLine.size() < 13 * 4 + 16)
			strLine += " ";
		for (int j = 0; j < 16 && i + j < length; j++) {
			if (*alpha > 31)
				strLine += *alpha;
			else
				strLine += '.';
			alpha++;
		}
		Log(LOGDEBUG, "%s", strLine.c_str());
	}
}

void Logger::SetLogLevel(int level) {
	CSingleLock waitLock(s_globals.critSec);
	if (level >= LOG_LEVEL_NONE && level <= LOG_LEVEL_MAX) {
		s_globals.m_logLevel = level;
		Logger::Log(LOGNOTICE, "Log level changed to \"%s\"",
				logLevelNames[s_globals.m_logLevel + 1]);
	} else
		Logger::Log(LOGERROR, "%s: Invalid log level requested: %d",
				__FUNCTION__, level);
}

int Logger::GetLogLevel() {
	return s_globals.m_logLevel;
}

void Logger::SetExtraLogLevels(int level) {
	CSingleLock waitLock(s_globals.critSec);
	s_globals.m_extraLogLevels = level;
}

bool Logger::IsLogLevelLogged(int loglevel) {
	const int extras = (loglevel & ~LOGMASK);
	if (extras != 0 && (s_globals.m_extraLogLevels & extras) == 0)
		return false;
#if defined(_DEBUG) || defined(PROFILE)
	return true;
#else
	if (s_globals.m_logLevel >= LOG_LEVEL_DEBUG) {
		return true;
	}

	if (s_globals.m_logLevel <= LOG_LEVEL_NONE) {
		return false;
	}

	// "m_logLevel" is "LOG_LEVEL_NORMAL"
	return (loglevel & LOGMASK) >= LOGNOTICE;

#endif
}

void Logger::LogString(int logLevel, const std::string& logString) {
	CSingleLock waitLock(s_globals.critSec);
	std::string strData(logString);
	StringUtils::TrimRight(strData);
	if (!strData.empty()) {
		if (s_globals.m_repeatLogLevel == logLevel
				&& s_globals.m_repeatLine == strData) {
			s_globals.m_repeatCount++;
			return;
		} else if (s_globals.m_repeatCount) {
			std::string strData2 = StringUtils::Format(
					"Previous line repeats %d times.", s_globals.m_repeatCount);
			PrintDebugString(strData2);
			WriteLogString(s_globals.m_repeatLogLevel, strData2);
			s_globals.m_repeatCount = 0;
		}

		s_globals.m_repeatLine = strData;
		s_globals.m_repeatLogLevel = logLevel;

		PrintDebugString(strData);

		WriteLogString(logLevel, strData);
	}
}

bool Logger::WriteLogString(int logLevel, const std::string& logString) {
#if 0
	static const char* prefixFormat = "%02.2d:%02.2d:%02.2d T:%llu %7s: ";

	std::string strData(logString);
	/* fixup newline alignment, number of spaces should equal prefix length */
	StringUtils::Replace(strData, "\n",
			"\n                                            ");

	int hour, minute, second;
	s_globals.m_platform.GetCurrentLocalTime(hour, minute, second);

	strData = StringUtils::Format(prefixFormat, hour, minute, second,
			(uint64_t) CThread::GetCurrentThreadId(), levelNames[logLevel])
	+ strData;

	return s_globals.m_platform.WriteStringToLog(strData);
#endif
	return false;
}

}

