/**
 * @file tests/unit/test_steam_library.cpp
 * @brief Tests for the Steam library parsing helpers.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <string>

// local includes
#include <src/steam_library.h>

using namespace std::literals;

TEST(SteamLibraryVdfTest, ParsesSectionsAndValues) {
  const auto document = steam_library::parse_vdf(R"vdf(
    "libraryfolders"
    {
      "0"
      {
        "path"    "C:\\Program Files (x86)\\Steam"
        "label"   ""
        "apps"
        {
          "228980"  "123"
        }
      }
    }
  )vdf");

  ASSERT_TRUE(document.has_value());
  const auto section = document->find("libraryfolders");
  ASSERT_NE(section, document->end());
  ASSERT_TRUE(section->second.section);

  const auto folder = section->second.children.find("0");
  ASSERT_NE(folder, section->second.children.end());
  EXPECT_EQ(folder->second.children.at("path").value, "C:\\Program Files (x86)\\Steam");
  EXPECT_EQ(folder->second.children.at("apps").children.at("228980").value, "123");
}

TEST(SteamLibraryVdfTest, RejectsMalformedDocuments) {
  EXPECT_FALSE(steam_library::parse_vdf("").has_value());
  EXPECT_FALSE(steam_library::parse_vdf("\"libraryfolders\"").has_value());
  EXPECT_FALSE(steam_library::parse_vdf("\"libraryfolders\" { \"0\" ").has_value());
  EXPECT_FALSE(steam_library::parse_vdf("libraryfolders {").has_value());
}

TEST(SteamLibraryPathTest, ReadsCurrentLibraryFormat) {
  const auto document = steam_library::parse_vdf(R"vdf(
    "libraryfolders"
    {
      "0" { "path" "C:\\Program Files (x86)\\Steam" }
      "1" { "path" "D:\\SteamLibrary" }
      "contentstatsid" "987654321"
    }
  )vdf");
  ASSERT_TRUE(document.has_value());

  const auto paths = steam_library::library_paths(*document);

  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], std::filesystem::path {"C:\\Program Files (x86)\\Steam"});
  EXPECT_EQ(paths[1], std::filesystem::path {"D:\\SteamLibrary"});
}

TEST(SteamLibraryPathTest, ReadsLegacyLibraryFormat) {
  const auto document = steam_library::parse_vdf(R"vdf(
    "LibraryFolders"
    {
      "TimeNextStatsReport" "1234567890"
      "ContentStatsID" "-1234"
      "1" "D:\\SteamLibrary"
      "2" "E:\\Games\\Steam"
    }
  )vdf");
  ASSERT_TRUE(document.has_value());

  const auto paths = steam_library::library_paths(*document);

  ASSERT_EQ(paths.size(), 2u);
  EXPECT_EQ(paths[0], std::filesystem::path {"D:\\SteamLibrary"});
  EXPECT_EQ(paths[1], std::filesystem::path {"E:\\Games\\Steam"});
}

TEST(SteamLibraryPathTest, ReturnsNothingWithoutLibraries) {
  const auto document = steam_library::parse_vdf(R"vdf("libraryfolders" { })vdf");
  ASSERT_TRUE(document.has_value());
  EXPECT_TRUE(steam_library::library_paths(*document).empty());
}

TEST(SteamLibraryManifestTest, ReadsInstalledApplications) {
  const auto app = steam_library::parse_app_manifest(R"vdf(
    "AppState"
    {
      "appid"     "1145360"
      "Universe"  "1"
      "name"      "Hades"
      "StateFlags" "4"
      "installdir" "Hades"
    }
  )vdf");

  ASSERT_TRUE(app.has_value());
  EXPECT_EQ(app->app_id, "1145360");
  EXPECT_EQ(app->name, "Hades");
  EXPECT_EQ(app->install_dir, "Hades");
}

TEST(SteamLibraryManifestTest, SkipsApplicationsThatAreNotInstalled) {
  EXPECT_FALSE(steam_library::parse_app_manifest(R"vdf(
    "AppState" { "appid" "1" "name" "Broken" "StateFlags" "2" }
  )vdf").has_value());

  EXPECT_FALSE(steam_library::parse_app_manifest(R"vdf(
    "AppState" { "appid" "1" "StateFlags" "4" }
  )vdf").has_value());

  EXPECT_FALSE(steam_library::parse_app_manifest(R"vdf(
    "AppState" { "name" "No id" "StateFlags" "4" }
  )vdf").has_value());
}

TEST(SteamLibraryManifestTest, AcceptsRelatedStateFlagsAndEscapedNames) {
  const auto app = steam_library::parse_app_manifest(R"vdf(
    "AppState"
    {
      "appid"      "620"
      "name"       "Portal 2 \"Testing\""
      "StateFlags" "6"
    }
  )vdf");

  ASSERT_TRUE(app.has_value());
  EXPECT_EQ(app->name, "Portal 2 \"Testing\"");
  EXPECT_TRUE(app->install_dir.empty());
}

TEST(SteamLibraryManifestTest, RejectsUnrelatedDocuments) {
  EXPECT_FALSE(steam_library::parse_app_manifest("").has_value());
  EXPECT_FALSE(steam_library::parse_app_manifest(R"vdf("Other" { "appid" "1" "name" "x" })vdf").has_value());
}

TEST(SteamLibraryCommandTest, QuotesTheSteamExecutable) {
  EXPECT_EQ(
    steam_library::launch_command("C:\\Program Files (x86)\\Steam\\steam.exe", "1145360"),
    "\"C:\\Program Files (x86)\\Steam\\steam.exe\" -applaunch 1145360");
}
