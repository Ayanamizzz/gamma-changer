#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

namespace gamma_changer::file_io {

// Configuration files are UTF-8 on disk. std::wifstream/std::wofstream depend on
// the process locale, so conversion is explicit. Both return an empty string on
// invalid input; callers distinguish "empty" from "invalid" by checking the
// input size.
std::string to_utf8(const std::wstring& value);
std::wstring from_utf8(const std::string& value);

// True when the file exists and is larger than `limit`. Parsers check this
// before reading so a hostile or damaged file cannot exhaust memory.
bool file_exceeds(const std::filesystem::path& path, std::uintmax_t limit);

// Reads the whole file. Returns false when the file cannot be opened, cannot be
// read, or is not valid UTF-8. An empty file is a successful empty read.
bool read_bytes(const std::filesystem::path& path, std::string& bytes);
bool read_utf8_text(const std::filesystem::path& path, std::wstring& text);

// The single write path for every persisted file:
//   take the cross-process store lock -> write <target>.tmp -> flush to disk ->
//   MoveFileEx(REPLACE_EXISTING | WRITE_THROUGH) -> remove the temp on failure.
// A crash at any point leaves either the old or the new file, never a torn one.
// `description` names the file in error messages ("profiles", "base ramp", ...).
bool write_atomically(const std::filesystem::path& target, const std::string& bytes,
                      const wchar_t* description, std::wstring& error);
bool write_utf8_atomically(const std::filesystem::path& target, const std::wstring& text,
                           const wchar_t* description, std::wstring& error);

}  // namespace gamma_changer::file_io
