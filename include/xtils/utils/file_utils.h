#pragma once

#include <string>
#include <vector>

namespace file_utils {
// Default file size limits to prevent memory exhaustion
constexpr size_t DEFAULT_MAX_FILE_SIZE = 100 * 1024 * 1024;  // 100MB
constexpr size_t DEFAULT_MAX_LINES = 1000000;                // 1 million lines
// Basic file checks
bool readable(const std::string& path);
bool writeable(const std::string& path);
bool exists(const std::string& path);
bool is_file(const std::string& path);
bool is_directory(const std::string& path);

// File I/O operations
bool read(const std::string& path, std::string* out);
bool read(const std::string& path, std::string* out, size_t max_size);
bool read_lines(const std::string& path, std::vector<std::string>* out);
bool read_lines(const std::string& path, std::vector<std::string>* out,
                size_t max_lines);
bool write(const std::string& path, const std::string& content);
bool write_lines(const std::string& path,
                 const std::vector<std::string>& lines);
bool append(const std::string& path, const std::string& content);

// Directory operations
bool mkdir(const std::string& path);
std::vector<std::string> list_directory(const std::string& path);
std::vector<std::string> list_files(const std::string& path);
std::vector<std::string> list_directories(const std::string& path);

// File system operations
bool copy(const std::string& src, const std::string& dst);
bool move(const std::string& src, const std::string& dst);
bool rename(const std::string& src, const std::string& dst);
bool remove(const std::string& path);
bool remove_all(const std::string& path);

// File information
size_t file_size(const std::string& path);

// Path operations
std::string dirname(const std::string& path);
std::string bsname(const std::string& path);
std::string extension(const std::string& path);
std::string stem(const std::string& path);
std::string join_path(const std::string& path1, const std::string& path2);
std::string absolute_path(const std::string& path);
std::string canonical_path(const std::string& path);

// Working directory operations
std::string current_path();
bool change_directory(const std::string& path);
}  // namespace file_utils
