#include "fileio.h"

#include <windows.h>

#include <fstream>
#include <sstream>

namespace pvnc {

bool readTextFile(const std::wstring& path, std::string& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  std::ostringstream ss;
  ss << in.rdbuf();
  out = ss.str();
  return true;
}

bool readBinFile(const std::wstring& path, std::vector<uint8_t>& out) {
  std::ifstream in(path, std::ios::binary);
  if (!in) return false;
  in.seekg(0, std::ios::end);
  std::streamoff n = in.tellg();
  in.seekg(0, std::ios::beg);
  out.resize(n > 0 ? (size_t)n : 0);
  if (!out.empty()) in.read((char*)out.data(), (std::streamsize)out.size());
  return (bool)in || in.eof();
}

bool writeTextFile(const std::wstring& path, const std::string& data) {
  std::ofstream o(path, std::ios::binary | std::ios::trunc);
  if (!o) return false;
  o.write(data.data(), (std::streamsize)data.size());
  return (bool)o;
}

bool writeBinFile(const std::wstring& path, const std::vector<uint8_t>& data) {
  std::ofstream o(path, std::ios::binary | std::ios::trunc);
  if (!o) return false;
  if (!data.empty()) o.write((const char*)data.data(), (std::streamsize)data.size());
  return (bool)o;
}

std::wstring joinPath(const std::wstring& dir, const std::wstring& leaf) {
  if (dir.empty()) return leaf;
  wchar_t last = dir.back();
  if (last == L'\\' || last == L'/') return dir + leaf;
  return dir + L"\\" + leaf;
}

std::string wToUtf8(const std::wstring& w) {
  if (w.empty()) return {};
  int n = WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), nullptr, 0, nullptr, nullptr);
  std::string s(n, '\0');
  WideCharToMultiByte(CP_UTF8, 0, w.data(), (int)w.size(), s.data(), n, nullptr, nullptr);
  return s;
}

std::wstring utf8ToW(const std::string& s) {
  if (s.empty()) return {};
  int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), nullptr, 0);
  std::wstring w(n, L'\0');
  MultiByteToWideChar(CP_UTF8, 0, s.data(), (int)s.size(), w.data(), n);
  return w;
}

}  // namespace pvnc
