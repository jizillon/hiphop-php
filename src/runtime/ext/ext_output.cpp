/*
   +----------------------------------------------------------------------+
   | HipHop for PHP                                                       |
   +----------------------------------------------------------------------+
   | Copyright (c) 2010 Facebook, Inc. (http://www.facebook.com)          |
   | Copyright (c) 1997-2010 The PHP Group                                |
   +----------------------------------------------------------------------+
   | This source file is subject to version 3.01 of the PHP license,      |
   | that is bundled with this package in the file LICENSE, and is        |
   | available through the world-wide-web at the following url:           |
   | http://www.php.net/license/3_01.txt                                  |
   | If you did not receive a copy of the PHP license and are unable to   |
   | obtain it through the world-wide-web, please send a note to          |
   | license@php.net so we can mail you a copy immediately.               |
   +----------------------------------------------------------------------+
*/

#include <runtime/ext/ext_output.h>
#include <runtime/ext/ext_json.h>
#include <runtime/base/runtime_option.h>
#include <util/lock.h>
#include <util/logger.h>

namespace HPHP {
///////////////////////////////////////////////////////////////////////////////

static ReadWriteMutex s_loggers_mutex;
typedef std::map<std::string, LogFileData> LoggerMap;
typedef std::map<std::string, Cronolog> CronLoggerMap;
static LoggerMap s_loggers;
static CronLoggerMap s_cronLoggers;

bool f_hphp_log(CStrRef filename, CStrRef message) {
  if (!RuntimeOption::EnableApplicationLog) {
    return false;
  }

  FILE *f = NULL;
  int *bytesWritten = NULL;
  int *prevBytesWritten = NULL;
  {
    ReadLock lock(s_loggers_mutex);
    if (Logger::UseCronolog) {
      CronLoggerMap::iterator iter = s_cronLoggers.find(filename.data());
      if (iter != s_cronLoggers.end()) {
        f = iter->second.getOutputFile();
        bytesWritten = &(iter->second.m_bytesWritten);
        prevBytesWritten = &(iter->second.m_prevBytesWritten);
      }
    } else {
      LoggerMap::iterator iter = s_loggers.find(filename.data());
      if (iter != s_loggers.end()) {
        f = iter->second.log;
        if (filename.charAt(0) != '|') {
          bytesWritten = &(iter->second.bytesWritten);
          prevBytesWritten = &(iter->second.prevBytesWritten);
        }
      }
    }
  }
  if (f == NULL) {
    WriteLock lock(s_loggers_mutex);
    if (Logger::UseCronolog) {
      CronLoggerMap::iterator iter = s_cronLoggers.find(filename.data());
      if (iter != s_cronLoggers.end()) {
        f = iter->second.getOutputFile();
        bytesWritten = &(iter->second.m_bytesWritten);
        prevBytesWritten = &(iter->second.m_prevBytesWritten);
      } else {
        Cronolog cl;
        if (strchr(filename.c_str(), '%')) {
          cl.m_template = filename;
          cl.setPeriodicity();
        } else {
          cl.m_file = fopen(filename.data(), "a");
        }
        s_cronLoggers[filename.data()] = cl;
        f = cl.getOutputFile();
        if (f == NULL) {
          return false;
        }
        iter = s_cronLoggers.find(filename.data());
        bytesWritten = &(iter->second.m_bytesWritten);
        prevBytesWritten = &(iter->second.m_prevBytesWritten);
      }
    } else {
      LoggerMap::iterator iter = s_loggers.find(filename.data());
      if (iter != s_loggers.end()) {
        f = iter->second.log;
        if (filename.charAt(0) != '|') {
          bytesWritten = &(iter->second.bytesWritten);
          prevBytesWritten = &(iter->second.prevBytesWritten);
        }
      } else {
        if (filename.charAt(0) == '|') {
          f = popen(filename.data() + 1, "w");
        } else {
          f = fopen(filename.data(), "a");
        }
        if (f == NULL) {
          return false;
        }
        s_loggers[filename.data()] = LogFileData(f);
        if (filename.charAt(0) != '|') {
          iter = s_loggers.find(filename.data());
          bytesWritten = &(iter->second.bytesWritten);
          prevBytesWritten = &(iter->second.prevBytesWritten);
        }
      }
    }
  }
  bool ret = (fwrite(message.data(), message.size(), 1, f) == 1);
  if (ret) {
    fflush(f);
    if (bytesWritten) {
      atomic_add(*bytesWritten, message.size());
      Logger::checkDropCache(*bytesWritten, *prevBytesWritten, f);
    }
  }
  return ret;
}

void f_hphp_crash_log(CStrRef name, CStrRef value) {
  StackTraceNoHeap::AddExtraLogging(name.data(), value.data());
}

Array f_hphp_get_status() {
  std::string out;
  ServerStats::ReportStatus(out, ServerStats::JSON);
  return f_json_decode(String(out));
}

static double ts_float(const timespec &ts) {
  return (double)ts.tv_sec + (double)ts.tv_nsec / 1000000000;
}

static String ts_microtime(const timespec &ts) {
  char ret[100];
  snprintf(ret, 100, "%.8F %ld", (double)ts.tv_nsec / 1000000000, ts.tv_sec);
  return String(ret, CopyString);
}

Variant f_hphp_get_timers(bool get_as_float /* = true */) {
  Transport *transport = g_context->getTransport();
  if (transport == NULL) {
    return false;
  }

  const timespec &tsQueue = transport->getQueueTime();
  const timespec &tsWall = transport->getWallTime();
  const timespec &tsCpu = transport->getCpuTime();

  Array ret;
  if (get_as_float) {
    ret.set("queue",        ts_float(tsQueue));
    ret.set("process-wall", ts_float(tsWall));
    ret.set("process-cpu",  ts_float(tsCpu));
  } else {
    ret.set("queue",        ts_microtime(tsQueue));
    ret.set("process-wall", ts_microtime(tsWall));
    ret.set("process-cpu",  ts_microtime(tsCpu));
  }
  return ret;
}

Variant f_hphp_output_global_state(bool serialize /* = true */) {
  Array r(get_global_state());
  if (serialize) {
    return f_serialize(r);
  } else {
    return r;
  }
}

///////////////////////////////////////////////////////////////////////////////
}
