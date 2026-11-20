/**
 * @file tests/unit/test_config_migration.cpp
 * @brief Tests for migrating a pre-rename configuration directory.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>

// local includes
#include <src/config.h>
#include <src/platform/common.h>

namespace {
  namespace fs = std::filesystem;

  /**
   * @brief Temporary directory tree used to exercise the migration.
   */
  class migration_dirs_t {
  public:
    /**
     * @brief Create empty legacy and target configuration directories.
     */
    migration_dirs_t() {
      const auto base = fs::temp_directory_path() / fs::path {std::format("solstice_migration_{}", reinterpret_cast<std::uintptr_t>(this))};
      root = base;
      legacy = base / "legacy";
      target = base / "target";

      std::error_code ec;
      fs::remove_all(root, ec);
      fs::create_directories(legacy, ec);
    }

    /**
     * @brief Remove the temporary directory tree.
     */
    ~migration_dirs_t() {
      std::error_code ec;
      fs::remove_all(root, ec);
    }

    migration_dirs_t(const migration_dirs_t &) = delete;  ///< Temporary directories are not copyable.
    migration_dirs_t &operator=(const migration_dirs_t &) = delete;  ///< Temporary directories are not copy-assignable.

    fs::path root;  ///< Root of the temporary directory tree.
    fs::path legacy;  ///< Directory laid out like a pre-rename installation.
    fs::path target;  ///< Directory used by the current installation.
  };

  /**
   * @brief Write a file, creating parent directories as needed.
   *
   * @param path File to write.
   * @param content Text written to the file.
   */
  void write_file(const fs::path &path, std::string_view content) {
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);

    std::ofstream output {path};
    output << content;
  }

  /**
   * @brief Read a file as text.
   *
   * @param path File to read.
   * @return File content, or an empty string when the file cannot be opened.
   */
  std::string read_file(const fs::path &path) {
    std::ifstream input {path};
    return {std::istreambuf_iterator<char> {input}, std::istreambuf_iterator<char> {}};
  }
}  // namespace

TEST(ConfigMigration, CopiesConfigurationAndPairingState) {
  migration_dirs_t dirs;

  write_file(dirs.legacy / "sunshine.conf", "port = 47989\n");
  write_file(dirs.legacy / "sunshine_state.json", R"({"root":{}})");
  write_file(dirs.legacy / "apps.json", "{}");
  write_file(dirs.legacy / "credentials" / "cakey.pem", "key");
  write_file(dirs.legacy / "covers" / "game.png", "image");

  EXPECT_TRUE(config::migrate_legacy_config(dirs.target, dirs.legacy));

  EXPECT_EQ(read_file(dirs.target / "solstice.conf"), "port = 47989\n");
  EXPECT_EQ(read_file(dirs.target / "solstice_state.json"), R"({"root":{}})");
  EXPECT_EQ(read_file(dirs.target / "apps.json"), "{}");
  EXPECT_EQ(read_file(dirs.target / "credentials" / "cakey.pem"), "key");
  EXPECT_EQ(read_file(dirs.target / "covers" / "game.png"), "image");

  // The legacy files are copied, not moved, so an uninstall of the old application cannot lose data.
  EXPECT_TRUE(fs::exists(dirs.legacy / "sunshine.conf"));
  EXPECT_FALSE(fs::exists(dirs.target / "sunshine.conf"));
}

TEST(ConfigMigration, SkipsWhenCurrentConfigurationExists) {
  migration_dirs_t dirs;

  write_file(dirs.legacy / "sunshine.conf", "port = 47989\n");
  write_file(dirs.legacy / "sunshine_state.json", "{}");
  write_file(dirs.target / "solstice.conf", "port = 48000\n");

  EXPECT_FALSE(config::migrate_legacy_config(dirs.target, dirs.legacy));
  EXPECT_EQ(read_file(dirs.target / "solstice.conf"), "port = 48000\n");
  EXPECT_FALSE(fs::exists(dirs.target / "solstice_state.json"));
}

TEST(ConfigMigration, DoesNothingWithoutLegacyDirectory) {
  migration_dirs_t dirs;

  write_file(dirs.legacy / "sunshine.conf", "port = 47989\n");
  std::error_code ec;
  fs::remove_all(dirs.legacy, ec);

  EXPECT_FALSE(config::migrate_legacy_config(dirs.target, dirs.legacy));
  EXPECT_FALSE(fs::exists(dirs.target));
}

TEST(ConfigMigration, IgnoresEmptyLegacyPath) {
  migration_dirs_t dirs;

  EXPECT_FALSE(config::migrate_legacy_config(dirs.target, {}));
  EXPECT_FALSE(fs::exists(dirs.target));
}

TEST(ConfigMigration, KeepsEntriesThatAlreadyExist) {
  migration_dirs_t dirs;

  write_file(dirs.legacy / "sunshine.conf", "port = 47989\n");
  write_file(dirs.legacy / "covers" / "game.png", "legacy image");
  write_file(dirs.target / "covers" / "game.png", "current image");

  EXPECT_TRUE(config::migrate_legacy_config(dirs.target, dirs.legacy));
  EXPECT_EQ(read_file(dirs.target / "covers" / "game.png"), "current image");
  EXPECT_TRUE(fs::exists(dirs.target / "solstice.conf"));
}

TEST(ConfigMigration, LegacyAppDataPointsAtTheProgramFilesInstall) {
  const auto legacy = platf::legacy_appdata();
  const auto *program_files = _wgetenv(L"ProgramFiles");
  if (program_files == nullptr || *program_files == L'\0') {
    GTEST_SKIP() << "ProgramFiles is not defined in this environment";
  }

  EXPECT_EQ(legacy, fs::path {program_files} / "Sunshine" / "config");
}
