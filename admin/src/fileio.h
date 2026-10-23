#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace pvnc {

// Reads an entire file. Returns false if it cannot be opened.
bool readTextFile(const std::wstring& path, std::string& out);
bool readBinFile(const std::wstring& path, std::vector<uint8_t>& out);

// Writes a whole file (truncating). Returns false on error.
bool writeTextFile(const std::wstring& path, const std::string& data);
bool writeBinFile(const std::wstring& path, const std::vector<uint8_t>& data);

// Joins a directory and a leaf name with a backslash.
std::wstring joinPath(const std::wstring& dir, const std::wstring& leaf);

// UTF-8 <-> UTF-16 helpers for passing file paths / config values around.
std::string wToUtf8(const std::wstring& w);
std::wstring utf8ToW(const std::string& s);

}  // namespace pvnc
