#ifndef OP_H
#define OP_H

#include "preload.hh"

#include <string>
#include <vector>
#include <map>
#include <any>
#include <iostream>
#include <sstream>
#include <regex>
#include <fstream>
#include <span>
#include <thread>
#include <filesystem>

#if defined(_WIN32)
#include <Windows.h>
#include <tchar.h>
#include <wrl.h>
#include <functional>

//
// A cross platform MAIN macro that
// magically gives us argc and argv.
//

#define MAIN \
  int argc = __argc; \
  char** argv = __argv; \
  \
  int CALLBACK WinMain(\
    _In_ HINSTANCE instanceId,\
    _In_ HINSTANCE hPrevInstance,\
    _In_ LPSTR lpCmdLine,\
    _In_ int nCmdShow)

#else
#define MAIN \
  int instanceId = 0; \
  int main (int argc, char** argv)
#endif

#define TO_STR(arg) #arg
#define STR_VALUE(arg) TO_STR(arg)

#define IMAX_BITS(m) ((m)/((m) % 255+1) / 255 % 255 * 8 + 7-86 / ((m) % 255+12))
#define RAND_MAX_WIDTH IMAX_BITS(RAND_MAX)

namespace fs = std::filesystem;
using Map = std::map<std::string, std::string>;

enum {
  FILE_DIALOG_OPEN    = 1 << 0,   // Create an open file dialog.
  FILE_DIALOG_SAVE    = 1 << 1,   // Create a save file dialog.
  FILE_DIALOG_DIR     = 1 << 2,   // Open a directory.
  FILE_DIALOG_OVERWRITE_CONFIRMATION = 1 << 3,
};

enum {
  WINDOW_HINT_NONE = 0,  // Width and height are default size
  WINDOW_HINT_MIN = 1,   // Width and height are minimum bounds
  WINDOW_HINT_MAX = 2,   // Width and height are maximum bounds
  WINDOW_HINT_FIXED = 3  // Window size can not be changed by a user
};

namespace Operator {
  //
  // Cross platform support for strings
  //
  #if defined(_WIN32)
    using String = std::wstring;
    using Stringstream = std::wstringstream;
    using namespace Microsoft::WRL;
    #define Str(s) L##s
    #define RegExp std::wregex

    inline std::wstring StringToWString(const std::string& s) {
      std::wstring temp(s.length(), L' ');
      std::copy(s.begin(), s.end(), temp.begin());
      return temp;
    }

    inline std::string WStringToString(const std::wstring& s) {
      std::string temp(s.length(), ' ');
      std::copy(s.begin(), s.end(), temp.begin());
      return temp;
    }

  #else
    using String = std::string;
    using Stringstream = std::stringstream;
    #define RegExp std::regex
    #define Str(s) s
    #define StringToWString(s) s
    #define WStringToString(s) s

  #endif

  //
  // Reporting on the platform (for the cli).
  //
  struct {
    #if defined(__x86_64__) || defined(_M_X64)
      const std::string arch = "x86_64";
    #elif defined(__aarch64__) || defined(_M_ARM64)
      const std::string arch = "arm64";
    #else
      const std::string arch = "unknown";
    #endif

    #if defined(_WIN32)
      bool mac = false;
      bool win = true;
      bool linux = false;
      const std::string os = "win32";

    #elif defined(__APPLE__)
      bool mac = true;
      bool win = false;
      bool linux = false;
      const std::string os = "mac";

    #elif defined(__linux__)
      bool mac = false;
      bool win = false;
      bool linux = true;
      const std::string os = "linux";

    #endif
  } platform;

  //
  // Application data
  //
  Map appData;

  //
  // Window data
  //
  struct WindowOptions {
    bool resizable = true;
    bool frameless = false;
    bool utility = false;
    bool canExit = true;
    int height = 0;
    int width = 0;
    int index = 0;
    int debug = 0;
    bool isTest = false;
    bool forwardConsole = 0;
    std::string cwd = "";
    std::string executable = "";
    std::string title = "";
    std::string url = "data:text/html,<html>";
    std::string version = "";
    std::string argv = "";
    std::string preload = "";
    std::string env;
  };

  template <typename ...Args> std::string format (const std::string& s, Args ...args) {
    auto copy = s;
    std::stringstream res;
    std::vector<std::any> vec;
    using unpack = int[];

    (void) unpack { 0, (vec.push_back(args), 0)... };

    std::regex re("\\$[^$\\s]");
    std::smatch match;
    auto first = std::regex_constants::format_first_only;
    int index = 0;

    while (std::regex_search(copy, match, re) != 0) {
      if (match.str() == "$S") {
        auto value = std::any_cast<std::string>(vec[index++]);
        copy = std::regex_replace(copy, re, value, first);
      } else if (match.str() == "$i") {
        auto value = std::any_cast<int>(vec[index++]);
        copy = std::regex_replace(copy, re, std::to_string(value), first);
      } else if (match.str() == "$C") {
        auto value = std::any_cast<char*>(vec[index++]);
        copy = std::regex_replace(copy, re, std::string(value), first);
      } else if (match.str() == "$c") {
        auto value = std::any_cast<char>(vec[index++]);
        copy = std::regex_replace(copy, re, std::string(1, value), first);
      } else {
        copy = std::regex_replace(copy, re, match.str(), first);
      }
    }

    return copy;
  }

  inline std::string replace(const std::string& src, const std::string& re, const std::string& val) {
    return std::regex_replace(src, std::regex(re), val);
  }

  std::string gMobilePreload = "";

  std::string createPreload(WindowOptions opts) {
    std::string cleanCwd = std::string(opts.cwd);
    std::replace(cleanCwd.begin(), cleanCwd.end(), '\\', '/');

    return std::string(
      "(() => {"
      "  window.system = {};\n"
      "  window.process = {};\n"
      "  window.process.index = Number('" + std::to_string(opts.index) + "');\n"
      "  window.process.cwd = () => '" + cleanCwd + "';\n"
      "  window.process.title = '" + opts.title + "';\n"
      "  window.process.executable = '" + opts.executable + "';\n"
      "  window.process.version = '" + opts.version + "';\n"
      "  window.process.debug = " + std::to_string(opts.debug) + ";\n"
      "  window.process.platform = '" + platform.os + "';\n"
      "  window.process.env = Object.fromEntries(new URLSearchParams('" +  opts.env + "'));\n"
      "  window.process.argv = [" + opts.argv + "];\n"
      "  " + gPreload + "\n"
      "  " + opts.preload + "\n"
      "})()\n"
      "//# sourceURL=preload.js"
    );
  }

  std::string resolveToRenderProcess(const std::string& seq, const std::string& state, const std::string& value) {
    return std::string(
      "(() => {"
      "  const seq = Number('" + seq + "');"
      "  const state = Number('" + state + "');"
      "  const value = '" + value + "';"
      "  window._ipc.resolve(seq, state, value);"
      "})()"
    );
  }

  std::string emitToRenderProcess(const std::string& event, const std::string& value) {
    return std::string(
      "(() => {"
      "  const name = '" + event + "';"
      "  const value = '" + value + "';"
      "  window._ipc.emit(name, value);"
      "})()"
    );
  }

  std::string streamToRenderProcess(const std::string& id, const std::string& value) {
    return std::string(
      "(() => {"
      "  const id = '" + id + "';"
      "  const value = '" + value + "';"
      "  window._ipc.callbacks[id] && window._ipc.callbacks[id](null, value);"
      "})()"
    );
  }

  std::string resolveMenuSelection(const std::string& seq, const std::string& title, const std::string& parent) {
    return std::string(
      "(() => {"
      "  const detail = {"
      "    title: '" + title + "',"
      "    parent: '" + parent + "',"
      "    state: '0'"
      "  };"

      "  if (" + seq + " > 0 && window._ipc[" + seq + "]) {"
      "    window._ipc[" + seq + "].resolve(detail);"
      "    delete window._ipc[" + seq + "];"
      "    return;"
      "  }"

      "  const event = new window.CustomEvent('menuItemSelected', { detail });"
      "  window.dispatchEvent(event);"
      "})()"
    );
  }

  std::string resolveToMainProcess(const std::string& seq, const std::string& state, const std::string& value) {
    return std::string("ipc://resolve?seq=" + seq + "&state=" + state + "&value=" + value);
  }

  //
  // Helper functions...
  //
  inline const std::vector<std::string>
  split(const std::string& s, const char& c) {
    std::string buff;
    std::vector<std::string> vec;

    for (auto n : s) {
      if(n != c) {
        buff += n;
      } else if (n == c && buff != "") {
        vec.push_back(buff);
        buff = "";
      }
    }

    if (!buff.empty()) vec.push_back(buff);

    return vec;
  }

  inline std::string
  trim(std::string str) {
    str.erase(0, str.find_first_not_of(" \r\n\t"));
    str.erase(str.find_last_not_of(" \r\n\t") + 1);
    return str;
  }

  inline std::string tmpl(const std::string s, Map pairs) {
    std::string output = s;

    for (auto item : pairs) {
      auto key = std::string("[{]+(" + item.first + ")[}]+");
      auto value = item.second;
      output = std::regex_replace(output, std::regex(key), value);
    }

    return output;
  }

  uint64_t rand64(void) {
    uint64_t r = 0;
    for (int i = 0; i < 64; i += RAND_MAX_WIDTH) {
      r <<= RAND_MAX_WIDTH;
      r ^= (unsigned) rand();
    }
    return r;
  }

  inline std::string getEnv(const char* variableName) {
    #if _WIN32
      char* variableValue = nullptr;
      std::size_t valueSize = 0;
      auto query = _dupenv_s(&variableValue, &valueSize, variableName);

      std::string result;
      if(query == 0 && variableValue != nullptr && valueSize > 0) {
        result.assign(variableValue, valueSize - 1);
        free(variableValue);
      }

      return result;
    #else
      auto v = getenv(variableName);

      if (v != nullptr) {
        return std::string(v);
      }

      return std::string("");
    #endif
  }

  inline auto setEnv(const char* s) {
    #if _WIN32
      return _putenv(s);
    #else

      return putenv((char*) &s[0]);
    #endif
  }

  struct ExecOutput {
    std::string output;
    int exitCode = 0;
  };

  inline ExecOutput exec(std::string command) {
    FILE *pipe;
    char buf[128];

    #ifdef _WIN32
      //
      // https://docs.microsoft.com/en-us/cpp/c-runtime-library/reference/popen-wpopen?view=msvc-160
      // _popen works fine in a console application... ok fine that's all we need it for... thanks.
      //
      pipe = _popen((const char*) command.c_str(), "rt");
    #else
      pipe = popen((const char*) command.c_str(), "r");
    #endif

    if (pipe == NULL) {
      std::cout << "error: unable to open the command" << std::endl;
      exit(1);
    }

    std::stringstream ss;

    while (fgets(buf, 128, pipe)) {
      ss << buf;
    }

    #ifdef _WIN32
      int exitCode = _pclose(pipe);
    #else
      int exitCode = pclose(pipe);
    #endif

    ExecOutput output {
      .output = ss.str(),
      .exitCode = exitCode
    };

    return output;
  }

  inline void writeToStdout(const std::string &str) {
    #ifdef _WIN32
      std::stringstream ss;
      ss << str << std::endl;
      auto lineStr = ss.str();

      WriteConsoleA(GetStdHandle(STD_OUTPUT_HANDLE), lineStr.c_str(), lineStr.size(), NULL, NULL);
    #else
      std::cout << str << std::endl;
    #endif
  }

  #if _IOS == 0
    inline std::string pathToString(const fs::path &path) {
      auto s = path.u8string();
      return std::string(s.begin(), s.end());
    }

    inline String readFile(fs::path path) {
      std::ifstream stream(path.c_str());
      String content;
      auto buffer = std::istreambuf_iterator<char>(stream);
      auto end = std::istreambuf_iterator<char>();
      content.assign(buffer, end);
      stream.close();
      return content;
    }

    inline void writeFile (fs::path path, std::string s) {
      std::ofstream stream(pathToString(path));
      stream << s;
      stream.close();
    }
  #endif

  inline std::string prefixFile(std::string s) {
    if (platform.mac || platform.linux) {
      return std::string("/usr/local/lib/op/" + s + " ");
    }

    std::string local = getEnv("LOCALAPPDATA");
    return std::string(local + "\\Programs\\socketsupply\\" + s + " ");
  }

  inline std::string prefixFile() {
    if (platform.mac || platform.linux) {
      return "/usr/local/lib/op";
    }

    std::string local = getEnv("LOCALAPPDATA");
    return std::string(local + "\\Programs\\socketsupply");
  }

  inline Map parseConfig(std::string source) {
    auto entries = split(source, '\n');
    Map settings;

    for (auto entry : entries) {
      auto index = entry.find_first_of(':');

      if (index >= 0 && index <= entry.size()) {
        auto key = entry.substr(0, index);
        auto value = entry.substr(index + 1);

        settings[trim(key)] = trim(value);
      }
    }

    return settings;
  }

  //
  // IPC Message parser for the middle end
  // TODO possibly harden data validation.
  //
  class Parse {
    Map args;
    public:
      Parse(const std::string&);
      int index = 0;
      std::string value = "";
      std::string name = "";
      std::string get(const std::string&);
  };

  struct ScreenSize {
    int height = 0;
    int width = 0;
  };

  //
  // cmd: `ipc://id?p1=v1&p2=v2&...\0`
  //
  inline Parse::Parse(const std::string& s) {
    std::string str = s;

    if (str.find("ipc://") == -1) return;

    std::string query;
    std::string path;

    auto raw = split(str, '?');
    path = raw[0];
    if (raw.size() > 1) query = raw[1];

    auto parts = split(path, '/');
    if (parts.size() >= 1) name = parts[1];

    if (raw.size() != 2) return;
    auto pairs = split(raw[1], '&');

    for (auto& rawPair : pairs) {
      auto pair = split(rawPair, '=');
      if (pair.size() <= 1) continue;

      if (pair[0].compare("index") == 0) {
        try {
          index = std::stoi(pair[1].size() > 0 ? pair[1] : "0");
        } catch (...) {
          std::cout << "Warning: received non-integer index" << std::endl;
        }
      }

      args[pair[0]] = pair[1];
    }
  }

  std::string Parse::get(const std::string& s) {
    return args.count(s) ? args[s] : "";
  }

  //
  // All ipc uses a URI schema, so all ipc data needs to be
  // encoded as a URI component. This prevents escaping the
  // protocol.
  //
  const char HEX2DEC[256] = {
    /*       0  1  2  3   4  5  6  7   8  9  A  B   C  D  E  F */
    /* 0 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 1 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 2 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 3 */  0, 1, 2, 3,  4, 5, 6, 7,  8, 9,-1,-1, -1,-1,-1,-1,

    /* 4 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 5 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 6 */ -1,10,11,12, 13,14,15,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 7 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

    /* 8 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* 9 */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* A */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* B */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,

    /* C */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* D */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* E */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1,
    /* F */ -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1, -1,-1,-1,-1
  };

  inline std::string decodeURIComponent(const std::string& sSrc) {

    // Note from RFC1630:  "Sequences which start with a percent sign
    // but are not followed by two hexadecimal characters (0-9, A-F) are reserved
    // for future extension"

    auto s = replace(sSrc, "\\+", " ");
    const unsigned char* pSrc = (const unsigned char *) s.c_str();
    const int SRC_LEN = sSrc.length();
    const unsigned char* const SRC_END = pSrc + SRC_LEN;
    const unsigned char* const SRC_LAST_DEC = SRC_END - 2;

    char* const pStart = new char[SRC_LEN];
    char* pEnd = pStart;

    while (pSrc < SRC_LAST_DEC) {
      if (*pSrc == '%') {
        char dec1, dec2;
        if (-1 != (dec1 = HEX2DEC[*(pSrc + 1)])
            && -1 != (dec2 = HEX2DEC[*(pSrc + 2)])) {

            *pEnd++ = (dec1 << 4) + dec2;
            pSrc += 3;
            continue;
        }
      }
      *pEnd++ = *pSrc++;
    }

    // the last 2- chars
    while (pSrc < SRC_END) {
      *pEnd++ = *pSrc++;
    }

    std::string sResult(pStart, pEnd);
    delete [] pStart;
    return sResult;
  }

  const char SAFE[256] = {
      /*      0 1 2 3  4 5 6 7  8 9 A B  C D E F */
      /* 0 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* 1 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* 2 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* 3 */ 1,1,1,1, 1,1,1,1, 1,1,0,0, 0,0,0,0,

      /* 4 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
      /* 5 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,
      /* 6 */ 0,1,1,1, 1,1,1,1, 1,1,1,1, 1,1,1,1,
      /* 7 */ 1,1,1,1, 1,1,1,1, 1,1,1,0, 0,0,0,0,

      /* 8 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* 9 */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* A */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* B */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,

      /* C */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* D */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* E */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0,
      /* F */ 0,0,0,0, 0,0,0,0, 0,0,0,0, 0,0,0,0
  };

  static inline std::string encodeURIComponent(const std::string& sSrc) {
    const char DEC2HEX[16 + 1] = "0123456789ABCDEF";
    const unsigned char* pSrc = (const unsigned char*) sSrc.c_str();
    const int SRC_LEN = sSrc.length();
    unsigned char* const pStart = new unsigned char[SRC_LEN* 3];
    unsigned char* pEnd = pStart;
    const unsigned char* const SRC_END = pSrc + SRC_LEN;

    for (; pSrc < SRC_END; ++pSrc) {
      if (SAFE[*pSrc]) {
        *pEnd++ = *pSrc;
      } else {
        // escape this char
        *pEnd++ = '%';
        *pEnd++ = DEC2HEX[*pSrc >> 4];
        *pEnd++ = DEC2HEX[*pSrc & 0x0F];
      }
    }

    std::string sResult((char*) pStart, (char*) pEnd);
    delete [] pStart;
    return sResult;
  }

  using SCallback = std::function<void(const std::string)>;
  using ExitCallback = std::function<void(int code)>;

  //
  // Interfaces make sure all operating systems implement the same stuff
  //
  class IApp {
    public:
      bool shouldExit = false;
      ExitCallback onExit = nullptr;
      void exit(int code);

      virtual int run() = 0;
      virtual void kill() = 0;
      virtual void restart() = 0;
      virtual void dispatch(std::function<void()> work) = 0;
      virtual std::string getCwd(const std::string&) = 0;
  };

  void IApp::exit (int code) {
    if (onExit != nullptr) onExit(code);
  }

  class IWindow {
    public:
      int index = 0;
      WindowOptions opts;
      SCallback onMessage = [](const std::string) {};
      ExitCallback onExit = nullptr;

      virtual void eval(const std::string&) = 0;
      virtual void show(const std::string&) = 0;
      virtual void hide(const std::string&) = 0;
      virtual void close(int code) = 0;
      virtual void exit(int code) = 0;
      virtual void kill() = 0;
      virtual void navigate(const std::string&, const std::string&) = 0;
      virtual void setSize(const std::string&, int, int, int) = 0;
      virtual void setTitle(const std::string&, const std::string&) = 0;
      virtual void setContextMenu(const std::string&, const std::string&) = 0;
      virtual void setSystemMenu(const std::string&, const std::string&) = 0;
      virtual void openDialog(const std::string&, bool, bool, bool, bool, const std::string&, const std::string&, const std::string&) = 0;
      virtual void setBackgroundColor(int r, int g, int b, float a) = 0;
      virtual void showInspector() = 0;
      virtual ScreenSize getScreenSize() = 0;
  };
}

#endif // OP_H
