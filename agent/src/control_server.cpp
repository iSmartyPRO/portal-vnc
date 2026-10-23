#include "control_server.h"

#include <windows.h>
#include <shlobj.h>

#include <cstdint>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "pvnc/control.h"
#include "sysinfo.h"

namespace pvnc {

namespace {
using nlohmann::json;

std::string toUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}
std::wstring fromUtf8(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

void putU32(std::vector<uint8_t>& b, uint32_t v) {
  b.push_back(uint8_t(v >> 24)); b.push_back(uint8_t(v >> 16));
  b.push_back(uint8_t(v >> 8)); b.push_back(uint8_t(v));
}
void putU64(std::vector<uint8_t>& b, uint64_t v) {
  for (int i = 7; i >= 0; --i) b.push_back(uint8_t(v >> (i * 8)));
}

bool sendResponse(TlsConn& c, uint8_t status, const std::string& payload) {
  std::vector<uint8_t> out;
  out.reserve(5 + payload.size());
  out.push_back(status);
  putU32(out, (uint32_t)payload.size());
  out.insert(out.end(), payload.begin(), payload.end());
  return c.writeFull(out.data(), out.size());
}

// Reads a u16-length-prefixed UTF-8 path.
bool readPath(TlsConn& c, std::wstring& out) {
  uint8_t len[2];
  if (!c.readFull(len, 2)) return false;
  size_t n = (size_t(len[0]) << 8) | len[1];
  std::string s(n, '\0');
  if (n && !c.readFull(s.data(), n)) return false;
  out = fromUtf8(s);
  return true;
}
bool readU64(TlsConn& c, uint64_t& out) {
  uint8_t b[8];
  if (!c.readFull(b, 8)) return false;
  out = 0;
  for (int i = 0; i < 8; ++i) out = (out << 8) | b[i];
  return true;
}

std::string listDirJson(const std::wstring& path) {
  json arr = json::array();
  if (path.empty()) {
    // Drives.
    DWORD mask = GetLogicalDrives();
    for (int i = 0; i < 26; ++i) {
      if (!(mask & (1u << i))) continue;
      wchar_t root[4] = {wchar_t(L'A' + i), L':', L'\\', 0};
      json e; char d[4] = {char('A' + i), ':', '\\', 0};
      e["name"] = d; e["dir"] = true; e["size"] = 0;
      arr.push_back(e);
    }
    return json{{"path", ""}, {"entries", arr}}.dump();
  }
  std::wstring base = path;
  if (base.back() != L'\\' && base.back() != L'/') base += L'\\';
  WIN32_FIND_DATAW fd;
  HANDLE h = FindFirstFileW((base + L"*").c_str(), &fd);
  if (h == INVALID_HANDLE_VALUE)
    return json{{"error", "cannot open path"}}.dump();
  do {
    std::wstring name = fd.cFileName;
    if (name == L"." || name == L"..") continue;
    json e;
    e["name"] = toUtf8(name);
    bool dir = (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    e["dir"] = dir;
    e["size"] = dir ? 0.0 : (double)((uint64_t(fd.nFileSizeHigh) << 32) | fd.nFileSizeLow);
    arr.push_back(e);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return json{{"path", toUtf8(path)}, {"entries", arr}}.dump();
}

// kFileGet response: status(1); if ok: u64 size + raw bytes.
bool handleGet(TlsConn& c, const std::wstring& path) {
  HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    uint8_t st = ctl::kNotFound;
    return c.writeFull(&st, 1);
  }
  LARGE_INTEGER sz{}; GetFileSizeEx(f, &sz);
  std::vector<uint8_t> hdr; hdr.push_back(ctl::kOk); putU64(hdr, (uint64_t)sz.QuadPart);
  if (!c.writeFull(hdr.data(), hdr.size())) { CloseHandle(f); return false; }
  std::vector<uint8_t> buf(64 * 1024);
  uint64_t remaining = (uint64_t)sz.QuadPart;
  bool ok = true;
  while (remaining > 0) {
    DWORD want = (DWORD)(remaining < buf.size() ? remaining : buf.size());
    DWORD got = 0;
    if (!ReadFile(f, buf.data(), want, &got, nullptr) || got == 0) { ok = false; break; }
    if (!c.writeFull(buf.data(), got)) { ok = false; break; }
    remaining -= got;
  }
  CloseHandle(f);
  return ok;
}

// kFilePut request (already read path): u64 size + raw bytes. Response: generic.
bool handlePut(TlsConn& c, const std::wstring& path) {
  uint64_t size = 0;
  if (!readU64(c, size)) return false;
  HANDLE f = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                         FILE_ATTRIBUTE_NORMAL, nullptr);
  if (f == INVALID_HANDLE_VALUE) {
    // Still consume the stream to keep the channel in sync, then report error.
    std::vector<uint8_t> sink(64 * 1024);
    uint64_t rem = size;
    while (rem > 0) {
      size_t want = (size_t)(rem < sink.size() ? rem : sink.size());
      if (!c.readFull(sink.data(), want)) break;
      rem -= want;
    }
    return sendResponse(c, ctl::kDenied, json{{"error", "cannot create file"}}.dump());
  }
  std::vector<uint8_t> buf(64 * 1024);
  uint64_t remaining = size;
  bool ok = true;
  while (remaining > 0) {
    size_t want = (size_t)(remaining < buf.size() ? remaining : buf.size());
    if (!c.readFull(buf.data(), want)) { ok = false; break; }
    DWORD wrote = 0;
    if (!WriteFile(f, buf.data(), (DWORD)want, &wrote, nullptr) || wrote != want) { ok = false; break; }
    remaining -= want;
  }
  CloseHandle(f);
  if (!ok) return false;
  return sendResponse(c, ctl::kOk, json{{"ok", true}}.dump());
}

}  // namespace

void serveControl(TlsConn& conn) {
  for (;;) {
    uint8_t op = 0;
    auto st = conn.readFullOrIdle(&op, 1);
    if (st == TlsConn::ReadState::Closed || st == TlsConn::ReadState::Error) break;
    if (st == TlsConn::ReadState::WouldBlock) continue;

    switch (op) {
      case ctl::kSysInfo:
        if (!sendResponse(conn, ctl::kOk, collectSystemInfoJson())) return;
        break;
      case ctl::kFileList: {
        std::wstring path;
        if (!readPath(conn, path)) return;
        if (!sendResponse(conn, ctl::kOk, listDirJson(path))) return;
        break;
      }
      case ctl::kFileGet: {
        std::wstring path;
        if (!readPath(conn, path)) return;
        if (!handleGet(conn, path)) return;
        break;
      }
      case ctl::kFilePut: {
        std::wstring path;
        if (!readPath(conn, path)) return;
        if (!handlePut(conn, path)) return;
        break;
      }
      case ctl::kMkDir: {
        std::wstring path;
        if (!readPath(conn, path)) return;
        int rc = SHCreateDirectoryExW(nullptr, path.c_str(), nullptr);
        bool ok = (rc == ERROR_SUCCESS || rc == ERROR_ALREADY_EXISTS || rc == ERROR_FILE_EXISTS);
        if (!sendResponse(conn, ok ? ctl::kOk : ctl::kDenied,
                          json{{"ok", ok}}.dump())) return;
        break;
      }
      default:
        sendResponse(conn, ctl::kError, "unsupported opcode");
        return;
    }
  }
}

}  // namespace pvnc
