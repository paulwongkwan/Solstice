/**
 * @file tests/unit/test_game_scanner.cpp
 * @brief Tests for the game folder scanner.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

// local includes
#include <src/game_scanner.h>

namespace {
  /**
   * @brief Create an empty placeholder file.
   *
   * @param path File to create.
   */
  void touch(const std::filesystem::path &path) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream {path} << "test";
  }

  /**
   * @brief Temporary directory that removes itself when the test ends.
   */
  struct temporary_directory_t {
    temporary_directory_t() {
      const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
      path = std::filesystem::temp_directory_path() / ("solstice-game-scanner-" + std::to_string(stamp) + "-" + std::to_string(counter++));
      std::filesystem::remove_all(path);
      std::filesystem::create_directories(path);
    }

    ~temporary_directory_t() {
      std::error_code error;
      std::filesystem::remove_all(path, error);
    }

    std::filesystem::path path;  ///< Directory used by the test.
    static inline int counter {};  ///< Distinguishes directories created in the same process.
  };
}  // namespace

TEST(GameScannerExecutableTest, AcceptsGameExecutables) {
  EXPECT_TRUE(game_scanner::is_game_executable("Hades.exe"));
  EXPECT_TRUE(game_scanner::is_game_executable("DOOM.EXE"));
  EXPECT_TRUE(game_scanner::is_game_executable("witcher3.exe"));
  EXPECT_TRUE(game_scanner::is_game_executable("Game-Win64-Shipping.exe"));
}

TEST(GameScannerExecutableTest, RejectsSupportToolingAndOtherFiles) {
  EXPECT_FALSE(game_scanner::is_game_executable("unins000.exe"));
  EXPECT_FALSE(game_scanner::is_game_executable("uninstall.exe"));
  EXPECT_FALSE(game_scanner::is_game_executable("setup.exe"));
  EXPECT_FALSE(game_scanner::is_game_executable("vcredist_x64.exe"));
  EXPECT_FALSE(game_scanner::is_game_executable("dxwebsetup.exe"));
  EXPECT_FALSE(game_scanner::is_game_executable("CrashReportClient.exe"));
  EXPECT_FALSE(game_scanner::is_game_executable("game.bin"));
  EXPECT_FALSE(game_scanner::is_game_executable("readme.txt"));
  EXPECT_FALSE(game_scanner::is_game_executable(".exe"));
}

TEST(GameScannerDisplayNameTest, TurnsFileNamesIntoNames) {
  EXPECT_EQ(game_scanner::display_name("Hades.exe"), "Hades");
  EXPECT_EQ(game_scanner::display_name("half_life_2.exe"), "Half Life 2");
  EXPECT_EQ(game_scanner::display_name("Portal.2.exe"), "Portal 2");
  EXPECT_EQ(game_scanner::display_name("  spaced__name  .exe"), "Spaced Name");
  EXPECT_EQ(game_scanner::display_name("ELDEN RING.exe"), "ELDEN RING");
  EXPECT_EQ(game_scanner::display_name("no_extension"), "No Extension");
}

TEST(GameScannerScanTest, FindsExecutablesRecursively) {
  temporary_directory_t directory;
  touch(directory.path / "Hades.exe");
  touch(directory.path / "Celeste" / "Celeste.exe");
  touch(directory.path / "Celeste" / "content" / "data.bin");

  const auto games = game_scanner::scan(directory.path);

  ASSERT_EQ(games.size(), 2u);
  EXPECT_EQ(games[0].name, "Celeste");
  EXPECT_EQ(games[1].name, "Hades");
  EXPECT_TRUE(std::filesystem::path {games[1].path}.is_absolute());
}

TEST(GameScannerScanTest, SkipsSupportFoldersAndInstallers) {
  temporary_directory_t directory;
  touch(directory.path / "Game.exe");
  touch(directory.path / "setup.exe");
  touch(directory.path / "_CommonRedist" / "vcredist_x64.exe");
  touch(directory.path / "Game" / "redist" / "helper.exe");

  const auto games = game_scanner::scan(directory.path);

  ASSERT_EQ(games.size(), 1u);
  EXPECT_EQ(games[0].name, "Game");
}

TEST(GameScannerScanTest, ReturnsNothingForMissingFolders) {
  temporary_directory_t directory;
  EXPECT_TRUE(game_scanner::scan(directory.path / "missing").empty());
  EXPECT_TRUE(game_scanner::scan({}).empty());
}

TEST(GameScannerScanTest, HonorsTheResultLimit) {
  temporary_directory_t directory;
  for (int i = 0; i < 5; ++i) {
    touch(directory.path / ("game" + std::to_string(i) + ".exe"));
  }

  EXPECT_EQ(game_scanner::scan(directory.path, 4, 3).size(), 3u);
}
