#include "logger.h"
#include <stdarg.h>
#include <sstream>
#include <webrtc/base/timeutils.h>
#include <string.h>

void LogContext::setLogContext(std::map<std::string, std::string> context)
{
	context_ = context;
	return;
	context_log_ = "";
	for (const std::pair<std::string, std::string> &item : context) {
		context_log_ += item.first + ": " + item.second + ", ";
	}
}

void LogContext::set_log_context(const char* fmt, ...)
{
	if (fmt) {
		char buffer[64] = { 0 };
		va_list vl; va_start(vl, fmt);
		vsnprintf(buffer, 64, fmt, vl);
		va_end(vl);
		context_log_ = buffer;
	}
}

void LogContext::LogStr(log4cxx::LoggerPtr logger, const char* fmt, ...)
{
	char buffer[ELOG_MAX_BUFFER_SIZE] = { 0 };
  char* p = buffer;
  if (context_log_.length()) {
    strcpy(buffer, context_log_.c_str());
    p = buffer + context_log_.length();
    *p++ = ' ';
  }
	va_list vl;
	va_start(vl, fmt);
	p += vsnprintf(p, ELOG_MAX_BUFFER_SIZE - context_log_.length() - 2, fmt, vl);
	va_end(vl);
#ifdef WIN32
	LogOut(INFO, logger, buffer);
#else
	LOG4CXX_INFO(logger, buffer);
#endif
}


LogTrace::LogTrace() :m_msExpire(5000)
{
	LogTrace("UNDEFINED_MODULE", "NO_LOG");
}
/**
* @brief LogTrace构造函数
*
* @param strModule 模块名
* @param strLog 日志字符串前缀
* @param exp 超时时间（单位ms,缺省为5000）
*/
LogTrace::LogTrace(const char* strModule, const char* strLog, unsigned long exp)
{
	m_strModule = "UNDEFINED_MODULE";
	m_strLog = "NO_LOG";
	if (NULL != strModule)
		m_strModule = strModule;
	if (NULL != strLog)
		m_strLog = strLog;

	m_nTickCount = rtc::Time32();

	if (exp > 0)
		m_msExpire = exp;
	else
		m_msExpire = 5000;

	std::string log = "come in ";
	log += m_strLog;

#ifdef WIN32
	LogOut(TRACE, m_strModule.c_str(), log.c_str());
#else
	LOG4CXX_INFO(log4cxx::Logger::getLogger(m_strModule.c_str()), log.c_str());
#endif
}

LogTrace::~LogTrace()
{
	std::stringstream ss;
	unsigned long nUsed = rtc::Time32() - m_nTickCount;
	if (nUsed <= m_msExpire)
	{
		ss << "it took " << nUsed << " ms, exit " << m_strLog.c_str();
	}
	else
	{
		ss << "it took " << nUsed << " ms, exit " << m_strLog.c_str() << ". +++MORE TIME NEEDED+++";
	}

#ifdef WIN32
	LogOut(TRACE, m_strModule.c_str(), ss.str().c_str());
#else
	LOG4CXX_INFO(log4cxx::Logger::getLogger(m_strModule.c_str()), ss.str().c_str());
#endif
}

#ifdef WIN32
#include <tchar.h>
#include <fstream>
#include <mutex>
#include <WinSock2.h>
void LogOut(char* strLog, int nLen)
{
	OutputDebugStringA(strLog);
	static long nLastTick = 0;
	static HWND hWndLogView = NULL;
	static std::mutex sec;
	std::lock_guard<std::mutex> lock(sec);
	long nTickCount = GetTickCount();
	if ((nTickCount - nLastTick) > (10 * 1000))
	{
		hWndLogView = ::FindWindow(_T("Coobol_LogView"), NULL);
	}
	if (hWndLogView)
	{
		COPYDATASTRUCT Data;
		Data.dwData = 0;
		Data.lpData = (void *)strLog;
		Data.cbData = nLen + 1;
		::SendMessage(hWndLogView, WM_COPYDATA, 0, (LPARAM)&Data);
	}

	static std::fstream fpLog;
	if (!fpLog || fpLog.tellp() > 50*1024*1024)
	{
		char tcPath[MAX_PATH] = { 0 };
		int nLen = GetModuleFileNameA(NULL, tcPath, MAX_PATH);
		while (nLen)
		{
			if (tcPath[nLen] == '\\') {
				tcPath[nLen + 1] = 0;
				break;
			}
			nLen--;
		}
		if (0 == nLen)
			return;
		strcat(tcPath, "Log\\");
		nLen += 4;
		CreateDirectoryA(tcPath, NULL);
		SYSTEMTIME t; GetLocalTime(&t);
		sprintf(tcPath + nLen + 1, "%0.4d%0.2d%0.2d%0.2d%0.2d%0.2d.log",
			t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond);
		fpLog.open(tcPath, std::ios::in | std::ios::out | std::ios::app);
	}
	fpLog.clear();
	fpLog << strLog;
	fpLog.flush();
}

int gLevel = DEBUG;
void LogOut(int level, const char* module, const char* fmt, ...)
{
	if (level < gLevel) return;
	char buffer[ELOG_MAX_BUFFER_SIZE] = { 0 };
	char *p = buffer;
	SYSTEMTIME st;
	GetLocalTime(&st);
	p += _snprintf(p, ELOG_MAX_BUFFER_SIZE, "(%0.5d) %02d:%02d:%02d:%04d %s: ",
		::GetCurrentThreadId(), st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, module);
	//p += snprintf(buffer, ELOG_MAX_BUFFER_SIZE, "%s ", module);
	va_list vl;
	va_start(vl, fmt);
	p += vsnprintf(p, ELOG_MAX_BUFFER_SIZE + buffer - p - 1, fmt, vl);
	va_end(vl);
	printf("%s\n", buffer);
}

void LogOut2(int level, const char* module, const char* file, int line, const char* fmt, ...)
{
	if (level < gLevel) return;
	char buffer[ELOG_MAX_BUFFER_SIZE] = { 0 };
	char *p = buffer;
	SYSTEMTIME st;
	GetLocalTime(&st);
	p += _snprintf(p, ELOG_MAX_BUFFER_SIZE, "(%0.5d) %02d:%02d:%02d:%04d %s(%s:%d): ",
		::GetCurrentThreadId(), st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, 
		module, file, line);
	//p += snprintf(buffer, ELOG_MAX_BUFFER_SIZE, "%s(%s:%d) ", module, file, line);
	va_list vl;
	va_start(vl, fmt);
	p += vsnprintf(p, ELOG_MAX_BUFFER_SIZE + buffer - p - 1, fmt, vl);
	va_end(vl);
	printf("%s\n", buffer);
}
#endif