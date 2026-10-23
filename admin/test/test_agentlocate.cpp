#include <catch2/catch_test_macros.hpp>

#include <windows.h>

#include <fstream>
#include <string>

#include "agentlocate.h"

using namespace pvnc;

namespace {
std::wstring makeTempDir() {
  wchar_t base[MAX_PATH];
  GetTempPathW(MAX_PATH, base);
  std::wstring dir = std::wstring(base) + L"pvnc_stat_" + std::to_wstring(GetTickCount64());
  CreateDirectoryW(dir.c_str(), nullptr);
  return dir;
}
void writeFile(const std::wstring& path, const std::string& text) {
  std::ofstream f(path, std::ios::binary | std::ios::trunc);
  f.write(text.data(), (std::streamsize)text.size());
}
}  // namespace

TEST_CASE("aggregateStatus merges two status files", "[agentlocate]") {
  std::wstring dir = makeTempDir();
  writeFile(dir + L"\\status-7654.json",
            R"({"agentVersion":"0.1.9","port":7654,"startedAt":1,
                "sessions":[{"user":"CORP\\jdoe","ip":"10.0.0.5","mode":"screen","since":100}]})");
  writeFile(dir + L"\\status-7655.json",
            R"({"agentVersion":"0.1.9","port":7655,"startedAt":2,
                "sessions":[{"user":"iNB","ip":"127.0.0.1","mode":"control","since":200}]})");

  LiveStatus s = aggregateStatus(dir);
  CHECK(s.agentVersion == L"0.1.9");
  CHECK(s.sessions.size() == 2);
  REQUIRE(s.ports.size() == 2);
  CHECK(s.ports[0] == 7654);
  CHECK(s.ports[1] == 7655);

  RemoveDirectoryW((dir).c_str());  // best-effort; leftover temp files are harmless
}

TEST_CASE("aggregateStatus on a missing dir is empty, no throw", "[agentlocate]") {
  LiveStatus s = aggregateStatus(L"C:\\no\\such\\pvnc\\dir\\x");
  CHECK(s.sessions.empty());
  CHECK(s.ports.empty());
}

TEST_CASE("aggregateStatus skips a malformed status file", "[agentlocate]") {
  std::wstring dir = makeTempDir();
  writeFile(dir + L"\\status-7654.json", "{not valid json");
  writeFile(dir + L"\\status-7655.json",
            R"({"port":7655,"sessions":[{"user":"a","ip":"1.2.3.4","mode":"screen","since":1}]})");
  LiveStatus s = aggregateStatus(dir);
  CHECK(s.sessions.size() == 1);
  CHECK(s.ports == std::vector<uint16_t>{7655});
}

TEST_CASE("locateAgent reports not installed cleanly when the service is absent",
          "[agentlocate]") {
  AgentInstall info = locateAgent();
  // On a dev box without the service, installed is false and nothing throws.
  // configDir always falls back to %ProgramData%\PortalVNC.
  CHECK(info.configDir.find(L"PortalVNC") != std::wstring::npos);
  if (!info.installed) CHECK(info.state == SvcState::NotInstalled);
}
