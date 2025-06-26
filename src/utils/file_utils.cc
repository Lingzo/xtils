#include "xtils/utils/file_utils.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <string.h>
#include <sys/sendfile.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

#include <string>
#include <vector>

// For copy_file_range system call
#ifndef __NR_copy_file_range
#if defined(__x86_64__)
#define __NR_copy_file_range 326
#elif defined(__i386__)
#define __NR_copy_file_range 377
#elif defined(__aarch64__)
#define __NR_copy_file_range 285
#elif defined(__arm__)
#define __NR_copy_file_range 391
#endif
#endif

#include <cstdlib>
#include <fstream>
#include <iostream>
#include <sstream>

namespace file_utils {

// Use the constants from the header
using file_utils::DEFAULT_MAX_FILE_SIZE;
using file_utils::DEFAULT_MAX_LINES;

bool readable(const std::string& path) {
  return access(path.c_str(), R_OK) == 0;
}

bool writeable(const std::string& path) {
  return access(path.c_str(), W_OK) == 0;
}

bool read(const std::string& path, std::string* out) {
  return read(path, out, DEFAULT_MAX_FILE_SIZE);
}

bool read(const std::string& path, std::string* out, size_t max_size) {
  // Check if file exists first
  if (!exists(path)) {
    return false;
  }

  // Get file size using stat
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }

  if (static_cast<size_t>(st.st_size) > max_size) {
    return false;  // File too large
  }

  std::ifstream file(path, std::ios::binary);
  if (!file.is_open()) {
    return false;
  }

  std::ostringstream ss;
  ss << file.rdbuf();
  *out = ss.str();

  return !file.bad();
}

bool read_lines(const std::string& path, std::vector<std::string>* out) {
  return read_lines(path, out, DEFAULT_MAX_LINES);
}

bool read_lines(const std::string& path, std::vector<std::string>* out,
                size_t max_lines) {
  auto& lines = *out;
  lines.clear();
  std::ifstream file(path);

  if (!file.is_open()) {
    return false;
  }

  std::string line;
  size_t line_count = 0;
  while (std::getline(file, line) && line_count < max_lines) {
    lines.push_back(line);
    ++line_count;
  }

  return true;
}

bool mkdir(const std::string& path) {
  // Create directories recursively
  std::string current_path;
  for (size_t i = 0; i < path.length(); ++i) {
    if (path[i] == '/' || i == path.length() - 1) {
      if (i == path.length() - 1 && path[i] != '/') {
        current_path += path[i];
      }

      if (!current_path.empty() && current_path != "/") {
        struct stat st;
        if (stat(current_path.c_str(), &st) != 0) {
          if (::mkdir(current_path.c_str(), 0755) != 0 && errno != EEXIST) {
            return false;
          }
        } else if (!S_ISDIR(st.st_mode)) {
          return false;  // Path exists but is not a directory
        }
      }
    }

    if (i < path.length()) {
      current_path += path[i];
    }
  }

  return true;
}

bool exists(const std::string& path) {
  struct stat st;
  return stat(path.c_str(), &st) == 0;
}

bool is_file(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }
  return S_ISREG(st.st_mode);
}

bool is_directory(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }
  return S_ISDIR(st.st_mode);
}

bool write(const std::string& path, const std::string& content) {
  std::ofstream file(path, std::ios::binary | std::ios::trunc);
  if (!file.is_open()) {
    return false;
  }

  file << content;
  return !file.bad();
}

bool write_lines(const std::string& path,
                 const std::vector<std::string>& lines) {
  std::ofstream file(path);
  if (!file.is_open()) {
    return false;
  }

  for (const auto& line : lines) {
    file << line << "\n";
  }

  return !file.bad();
}

bool append(const std::string& path, const std::string& content) {
  std::ofstream file(path, std::ios::app);
  if (!file.is_open()) {
    return false;
  }

  file << content;
  return !file.bad();
}

bool copy(const std::string& src, const std::string& dst) {
  int src_fd = open(src.c_str(), O_RDONLY);
  if (src_fd == -1) {
    return false;
  }

  // Get source file permissions and size
  struct stat src_stat;
  if (fstat(src_fd, &src_stat) != 0) {
    close(src_fd);
    return false;
  }

  int dst_fd =
      open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, src_stat.st_mode);
  if (dst_fd == -1) {
    close(src_fd);
    return false;
  }

  bool success = false;
  off_t file_size = src_stat.st_size;

  // Method 1: Try copy_file_range() (Linux 4.5+)
#ifdef __NR_copy_file_range
  off_t offset = 0;
  while (offset < file_size) {
    ssize_t bytes_copied = syscall(__NR_copy_file_range, src_fd, &offset,
                                   dst_fd, nullptr, file_size - offset, 0);
    if (bytes_copied == -1) {
      if (errno == ENOSYS || errno == EXDEV) {
        // copy_file_range not supported or cross-filesystem copy
        break;
      }
      goto cleanup;
    }
    if (bytes_copied == 0) {
      // EOF reached
      break;
    }
  }

  if (offset >= file_size) {
    success = true;
    goto cleanup;
  }
#endif

  // Method 2: Try sendfile() as fallback
  {
    off_t sendfile_offset = 0;
    while (sendfile_offset < file_size) {
      ssize_t bytes_sent = sendfile(dst_fd, src_fd, &sendfile_offset,
                                    file_size - sendfile_offset);
      if (bytes_sent == -1) {
        if (errno == ENOSYS || errno == EINVAL) {
          // sendfile not supported for this file type
          break;
        }
        goto cleanup;
      }
      if (bytes_sent == 0) {
        // EOF reached
        break;
      }
    }

    if (sendfile_offset >= file_size) {
      success = true;
      goto cleanup;
    }
  }

  // Method 3: Traditional read/write fallback
  {
    // Reset file position for traditional copy
    if (lseek(src_fd, 0, SEEK_SET) == -1) {
      goto cleanup;
    }

    char buffer[65536];  // Use larger buffer for better performance
    ssize_t bytes_read, bytes_written;
    success = true;

    while ((bytes_read = ::read(src_fd, buffer, sizeof(buffer))) > 0) {
      char* write_ptr = buffer;
      ssize_t remaining = bytes_read;

      while (remaining > 0) {
        bytes_written = ::write(dst_fd, write_ptr, remaining);
        if (bytes_written == -1) {
          success = false;
          goto cleanup;
        }
        write_ptr += bytes_written;
        remaining -= bytes_written;
      }
    }

    if (bytes_read == -1) {
      success = false;
    }
  }

cleanup:
  close(src_fd);
  close(dst_fd);

  return success;
}

bool rename(const std::string& src, const std::string& dst) {
  return ::rename(src.c_str(), dst.c_str()) == 0;
}

bool move(const std::string& src, const std::string& dst) {
  return ::rename(src.c_str(), dst.c_str()) == 0;
}

bool remove(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }

  if (S_ISDIR(st.st_mode)) {
    return rmdir(path.c_str()) == 0;
  } else {
    return unlink(path.c_str()) == 0;
  }
}

bool remove_all(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return false;
  }

  if (!S_ISDIR(st.st_mode)) {
    return unlink(path.c_str()) == 0;
  }

  // Remove directory contents recursively
  DIR* dir = opendir(path.c_str());
  if (!dir) {
    return false;
  }

  struct dirent* entry;
  bool success = true;

  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string entry_path = path + "/" + entry->d_name;
    if (!remove_all(entry_path)) {
      success = false;
      break;
    }
  }

  closedir(dir);

  if (success) {
    success = rmdir(path.c_str()) == 0;
  }

  return success;
}

size_t file_size(const std::string& path) {
  struct stat st;
  if (stat(path.c_str(), &st) != 0) {
    return 0;
  }
  return static_cast<size_t>(st.st_size);
}

std::string dirname(const std::string& path) {
  if (path.empty()) {
    return ".";
  }
  auto idx = path.find_last_of("/");
  if (idx != std::string::npos) {
    return path.substr(0, idx);
  }
  return ".";
}

std::string bsname(const std::string& path) {
  if (path.empty()) {
    return ".";
  }
  auto idx = path.find_last_of("/");
  if (idx != std::string::npos) {
    return path.substr(idx + 1);
  }
  return path;
}

std::string extension(const std::string& path) {
  std::string base = file_utils::bsname(path);
  size_t dot_pos = base.find_last_of('.');
  if (dot_pos == std::string::npos || dot_pos == 0) {
    return "";
  }
  return base.substr(dot_pos);
}

std::string stem(const std::string& path) {
  std::string base = file_utils::bsname(path);
  size_t dot_pos = base.find_last_of('.');
  if (dot_pos == std::string::npos || dot_pos == 0) {
    return base;
  }
  return base.substr(0, dot_pos);
}

std::string join_path(const std::string& path1, const std::string& path2) {
  if (path1.empty()) {
    return path2;
  }
  if (path2.empty()) {
    return path1;
  }

  if (path2[0] == '/') {
    return path2;  // path2 is absolute
  }

  std::string result = path1;
  if (result.back() != '/') {
    result += '/';
  }
  result += path2;

  return result;
}

std::string absolute_path(const std::string& path) {
  if (path.empty()) {
    return current_path();
  }

  if (path[0] == '/') {
    return path;  // Already absolute
  }

  char* resolved = realpath(path.c_str(), nullptr);
  if (!resolved) {
    // If realpath fails, construct manually
    return join_path(current_path(), path);
  }

  std::string result(resolved);
  free(resolved);
  return result;
}

std::string canonical_path(const std::string& path) {
  char* resolved = realpath(path.c_str(), nullptr);
  if (!resolved) {
    return path;  // Return original path if canonicalization fails
  }

  std::string result(resolved);
  free(resolved);
  return result;
}

std::vector<std::string> list_directory(const std::string& path) {
  std::vector<std::string> entries;

  DIR* dir = opendir(path.c_str());
  if (!dir) {
    return entries;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") != 0 && strcmp(entry->d_name, "..") != 0) {
      entries.push_back(entry->d_name);
    }
  }

  closedir(dir);
  return entries;
}

std::vector<std::string> list_files(const std::string& path) {
  std::vector<std::string> files;

  DIR* dir = opendir(path.c_str());
  if (!dir) {
    return files;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string entry_path = join_path(path, entry->d_name);
    if (is_file(entry_path)) {
      files.push_back(entry->d_name);
    }
  }

  closedir(dir);
  return files;
}

std::vector<std::string> list_directories(const std::string& path) {
  std::vector<std::string> dirs;

  DIR* dir = opendir(path.c_str());
  if (!dir) {
    return dirs;
  }

  struct dirent* entry;
  while ((entry = readdir(dir)) != nullptr) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
      continue;
    }

    std::string entry_path = join_path(path, entry->d_name);
    if (is_directory(entry_path)) {
      dirs.push_back(entry->d_name);
    }
  }

  closedir(dir);
  return dirs;
}

std::string current_path() {
  char* cwd = getcwd(nullptr, 0);
  if (!cwd) {
    return "./";
  }

  std::string result(cwd);
  free(cwd);
  return result;
}

bool change_directory(const std::string& path) {
  return chdir(path.c_str()) == 0;
}

}  // namespace file_utils
