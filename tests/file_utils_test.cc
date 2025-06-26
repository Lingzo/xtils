#include "xtils/utils/file_utils.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest/doctest.h"

TEST_CASE("file_utils basic file operations") {
  const std::string test_file = "test_file.txt";
  const std::string test_content = "Hello, World!\nThis is a test file.";

  // Clean up before test
  file_utils::remove(test_file);

  SUBCASE("write and read file") {
    CHECK(file_utils::write(test_file, test_content));
    CHECK(file_utils::exists(test_file));
    CHECK(file_utils::is_file(test_file));

    std::string content;
    CHECK(file_utils::read(test_file, &content));
    CHECK(content == test_content);
  }

  SUBCASE("readable and writeable checks") {
    file_utils::write(test_file, test_content);
    CHECK(file_utils::readable(test_file));
    CHECK(file_utils::writeable(test_file));
  }

  SUBCASE("file size") {
    file_utils::write(test_file, test_content);
    CHECK(file_utils::file_size(test_file) == test_content.length());
  }

  SUBCASE("append to file") {
    const std::string append_content = "\nAppended line";
    file_utils::write(test_file, test_content);
    CHECK(file_utils::append(test_file, append_content));

    std::string final_content;
    file_utils::read(test_file, &final_content);
    CHECK(final_content == test_content + append_content);
  }

  // Clean up after test
  file_utils::remove(test_file);
}

TEST_CASE("file_utils line operations") {
  const std::string test_file = "test_lines.txt";
  const std::vector<std::string> test_lines = {"Line 1", "Line 2", "Line 3"};

  // Clean up before test
  file_utils::remove(test_file);

  SUBCASE("write and read lines") {
    CHECK(file_utils::write_lines(test_file, test_lines));
    std::vector<std::string> read_lines;
    file_utils::read_lines(test_file, &read_lines);
    CHECK(read_lines.size() == test_lines.size());

    for (size_t i = 0; i < test_lines.size(); ++i) {
      CHECK(read_lines[i] == test_lines[i]);
    }
  }

  // Clean up after test
  file_utils::remove(test_file);
}

TEST_CASE("file_utils directory operations") {
  const std::string test_dir = "test_directory";
  const std::string nested_dir = "test_directory/nested";

  // Clean up before test
  file_utils::remove_all(test_dir);

  SUBCASE("create directory") {
    CHECK(file_utils::mkdir(test_dir));
    CHECK(file_utils::exists(test_dir));
    CHECK(file_utils::is_directory(test_dir));
  }

  SUBCASE("create nested directories") {
    CHECK(file_utils::mkdir(nested_dir));
    CHECK(file_utils::exists(nested_dir));
    CHECK(file_utils::is_directory(nested_dir));
  }

  SUBCASE("list directory contents") {
    file_utils::mkdir(test_dir);

    // Create some test files and directories
    const std::string test_file1 = file_utils::join_path(test_dir, "file1.txt");
    const std::string test_file2 = file_utils::join_path(test_dir, "file2.txt");
    const std::string test_subdir = file_utils::join_path(test_dir, "subdir");

    file_utils::write(test_file1, "content1");
    file_utils::write(test_file2, "content2");
    file_utils::mkdir(test_subdir);

    auto all_entries = file_utils::list_directory(test_dir);
    auto files = file_utils::list_files(test_dir);
    auto dirs = file_utils::list_directories(test_dir);

    CHECK(all_entries.size() == 3);
    CHECK(files.size() == 2);
    CHECK(dirs.size() == 1);

    // Check if expected entries are present
    bool found_file1 = false, found_file2 = false, found_subdir = false;
    for (const auto& entry : all_entries) {
      if (entry == "file1.txt") found_file1 = true;
      if (entry == "file2.txt") found_file2 = true;
      if (entry == "subdir") found_subdir = true;
    }
    CHECK(found_file1);
    CHECK(found_file2);
    CHECK(found_subdir);
  }

  // Clean up after test
  file_utils::remove_all(test_dir);
}

TEST_CASE("file_utils file system operations") {
  const std::string src_file = "source.txt";
  const std::string dst_file = "destination.txt";
  const std::string moved_file = "moved.txt";
  const std::string test_content = "Test content for file operations";

  // Clean up before test
  file_utils::remove(src_file);
  file_utils::remove(dst_file);
  file_utils::remove(moved_file);

  SUBCASE("copy file") {
    file_utils::write(src_file, test_content);
    CHECK(file_utils::copy(src_file, dst_file));
    CHECK(file_utils::exists(dst_file));

    std::string dst_content;
    file_utils::read(dst_file, &dst_content);
    CHECK(dst_content == test_content);
  }

  SUBCASE("move file") {
    file_utils::write(src_file, test_content);
    CHECK(file_utils::move(src_file, moved_file));
    CHECK(!file_utils::exists(src_file));
    CHECK(file_utils::exists(moved_file));

    std::string moved_content;
    file_utils::read(moved_file, &moved_content);
    CHECK(moved_content == test_content);
  }

  SUBCASE("remove file") {
    file_utils::write(src_file, test_content);
    CHECK(file_utils::exists(src_file));
    CHECK(file_utils::remove(src_file));
    CHECK(!file_utils::exists(src_file));
  }

  // Clean up after test
  file_utils::remove(src_file);
  file_utils::remove(dst_file);
  file_utils::remove(moved_file);
}

TEST_CASE("file_utils path operations") {
  SUBCASE("path manipulation") {
    const std::string path = "/home/user/documents/file.txt";

    CHECK(file_utils::dirname(path) == "/home/user/documents");
    CHECK(file_utils::bsname(path) == "file.txt");
    CHECK(file_utils::extension(path) == ".txt");
    CHECK(file_utils::stem(path) == "file");
  }

  SUBCASE("join paths") {
    CHECK(file_utils::join_path("home", "user") == "home/user");
    CHECK(file_utils::join_path("/home", "user") == "/home/user");
    CHECK(file_utils::join_path("home/", "user") == "home/user");
  }

  SUBCASE("absolute path") {
    const std::string relative_path = "test.txt";
    std::string abs_path = file_utils::absolute_path(relative_path);
    CHECK(!abs_path.empty());
    CHECK(abs_path.front() == '/');  // Should start with / on Unix systems
  }
}

TEST_CASE("file_utils working directory operations") {
  SUBCASE("current path") {
    std::string current = file_utils::current_path();
    CHECK(!current.empty());
  }

  SUBCASE("change directory") {
    std::string original = file_utils::current_path();
    const std::string test_dir = "temp_test_dir";

    // Create test directory
    file_utils::mkdir(test_dir);

    // Change to test directory
    CHECK(file_utils::change_directory(test_dir));

    std::string new_current = file_utils::current_path();
    CHECK(new_current != original);

    // Change back to original directory
    CHECK(file_utils::change_directory(original));

    // Clean up
    file_utils::remove_all(test_dir);
  }
}

TEST_CASE("file_utils file size limits") {
  const std::string test_file = "size_limit_test.txt";

  // Clean up before test
  file_utils::remove(test_file);

  SUBCASE("read with custom size limit") {
    const std::string large_content = std::string(1000, 'A');  // 1KB content
    file_utils::write(test_file, large_content);

    std::string content;
    // Should succeed with large limit
    CHECK(file_utils::read(test_file, &content, 2000));
    CHECK(content == large_content);

    // Should fail with small limit
    CHECK(!file_utils::read(test_file, &content, 500));
  }

  SUBCASE("read_lines with custom line limit") {
    std::vector<std::string> many_lines;
    for (int i = 0; i < 100; ++i) {
      many_lines.push_back("Line " + std::to_string(i));
    }
    file_utils::write_lines(test_file, many_lines);

    // Should read all lines with large limit
    std::vector<std::string> all_lines;
    file_utils::read_lines(test_file, &all_lines, 200);
    CHECK(all_lines.size() == 100);

    // Should read only limited lines with small limit
    std::vector<std::string> limited_lines;
    file_utils::read_lines(test_file, &limited_lines, 50);
    CHECK(limited_lines.size() == 50);
    CHECK(limited_lines[0] == "Line 0");
    CHECK(limited_lines[49] == "Line 49");
  }

  SUBCASE("default limits") {
    // Test that default functions still work
    const std::string normal_content = "Normal sized content";
    file_utils::write(test_file, normal_content);

    std::string content;
    CHECK(file_utils::read(test_file, &content));
    CHECK(content == normal_content);

    std::vector<std::string> lines;
    file_utils::read_lines(test_file, &lines);
    CHECK(lines.size() == 1);
    CHECK(lines[0] == normal_content);
  }

  // Clean up after test
  file_utils::remove(test_file);
}

TEST_CASE("file_utils error handling") {
  SUBCASE("read non-existent file") {
    std::string content;
    CHECK(!file_utils::read("non_existent_file.txt", &content));
    CHECK(content.empty());
  }

  SUBCASE("readlines non-existent file") {
    std::vector<std::string> lines;
    file_utils::read_lines("non_existent_file.txt", &lines);
    CHECK(lines.empty());
  }

  SUBCASE("check non-existent file") {
    CHECK(!file_utils::exists("non_existent_file.txt"));
    CHECK(!file_utils::readable("non_existent_file.txt"));
    CHECK(!file_utils::writeable("non_existent_file.txt"));
    CHECK(!file_utils::is_file("non_existent_file.txt"));
    CHECK(!file_utils::is_directory("non_existent_file.txt"));
  }

  SUBCASE("file size of non-existent file") {
    CHECK(file_utils::file_size("non_existent_file.txt") == 0);
  }

  SUBCASE("list non-existent directory") {
    auto entries = file_utils::list_directory("non_existent_directory");
    CHECK(entries.empty());
  }
}
