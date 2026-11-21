/**
 * @file tests/unit/test_app_json.cpp
 * @brief Tests for the application list helpers.
 */

// test includes
#include "../tests_common.h"

// local includes
#include <src/app_json.h>

TEST(AppJsonNameTest, RequiresANonEmptyStringName) {
  EXPECT_TRUE(app_json::has_valid_name({{"name", "Hades"}}));
  EXPECT_TRUE(app_json::has_valid_name({{"name", "  Hades  "}}));
  EXPECT_FALSE(app_json::has_valid_name({{"name", ""}}));
  EXPECT_FALSE(app_json::has_valid_name({{"name", "   "}}));
  EXPECT_FALSE(app_json::has_valid_name({{"name", 42}}));
  EXPECT_FALSE(app_json::has_valid_name({{"cmd", "Hades.exe"}}));
  EXPECT_FALSE(app_json::has_valid_name("Hades"));
}

TEST(AppJsonAppendTest, AppendsAndSortsApplications) {
  nlohmann::json file_tree = {{"apps", nlohmann::json::array({{{"name", "Zelda"}}})}};

  const auto added = app_json::append(file_tree, nlohmann::json::array({{{"name", "Celeste"}}, {{"name", "Hades"}}}));

  EXPECT_EQ(added, 2u);
  ASSERT_EQ(file_tree["apps"].size(), 3u);
  EXPECT_EQ(file_tree["apps"][0]["name"], "Celeste");
  EXPECT_EQ(file_tree["apps"][1]["name"], "Hades");
  EXPECT_EQ(file_tree["apps"][2]["name"], "Zelda");
}

TEST(AppJsonAppendTest, SkipsEntriesWithoutAUsableName) {
  nlohmann::json file_tree = {{"apps", nlohmann::json::array()}};

  const auto added = app_json::append(
    file_tree,
    nlohmann::json::array({{{"name", "Hades"}}, {{"name", ""}}, {{"cmd", "setup.exe"}}, "Hades", 5}));

  EXPECT_EQ(added, 1u);
  ASSERT_EQ(file_tree["apps"].size(), 1u);
  EXPECT_EQ(file_tree["apps"][0]["name"], "Hades");
}

TEST(AppJsonAppendTest, CreatesTheApplicationArrayWhenMissing) {
  nlohmann::json file_tree = nlohmann::json::object();

  const auto added = app_json::append(file_tree, nlohmann::json::array({{{"name", "Hades"}}}));

  EXPECT_EQ(added, 1u);
  ASSERT_TRUE(file_tree["apps"].is_array());
  EXPECT_EQ(file_tree["apps"].size(), 1u);
}

TEST(AppJsonAppendTest, ReplacesAnInvalidApplicationArray) {
  nlohmann::json file_tree = {{"apps", "not an array"}};

  const auto added = app_json::append(file_tree, nlohmann::json::array({{{"name", "Hades"}}}));

  EXPECT_EQ(added, 1u);
  ASSERT_TRUE(file_tree["apps"].is_array());
  EXPECT_EQ(file_tree["apps"].size(), 1u);
}

TEST(AppJsonAppendTest, RejectsPayloadsThatAreNotArrays) {
  nlohmann::json file_tree = {{"apps", nlohmann::json::array()}};
  EXPECT_EQ(app_json::append(file_tree, {{"name", "Hades"}}), 0u);

  nlohmann::json not_a_document = "text";
  EXPECT_EQ(app_json::append(not_a_document, nlohmann::json::array()), 0u);
}

TEST(AppJsonAppendTest, SortingKeepsExistingEntriesWithMissingNames) {
  nlohmann::json file_tree = {{"apps", nlohmann::json::array({{
    {"cmd", "orphan"}
  }})}};

  app_json::append(file_tree, nlohmann::json::array({{
    {"name", "Hades"}
  }}));

  ASSERT_EQ(file_tree["apps"].size(), 2u);
  EXPECT_EQ(file_tree["apps"][0]["cmd"], "orphan");
  EXPECT_EQ(file_tree["apps"][1]["name"], "Hades");
}

TEST(AppJsonWorkingDirTest, ReadsTheDirectoryOfAQuotedExecutable) {
  EXPECT_EQ(app_json::executable_directory(R"("C:\Games\Hades\Hades.exe")"), R"(C:\Games\Hades)");
  EXPECT_EQ(app_json::executable_directory(R"(  "D:/SteamLibrary/steamapps/common/Hades/Hades.exe"  )"), "D:/SteamLibrary/steamapps/common/Hades");
  EXPECT_EQ(app_json::executable_directory(R"("C:\Games\Hades.EXE")"), R"(C:\Games)");
}

TEST(AppJsonWorkingDirTest, RejectsCommandsThatAreNotASingleExecutable) {
  EXPECT_FALSE(app_json::executable_directory(R"("C:\Games\Hades\Hades.exe" -windowed)").has_value());
  EXPECT_FALSE(app_json::executable_directory(R"(C:\Games\Hades\Hades.exe)").has_value());
  EXPECT_FALSE(app_json::executable_directory(R"("Hades.exe")").has_value());
  EXPECT_FALSE(app_json::executable_directory(R"("C:\Games\Hades\readme.txt")").has_value());
  EXPECT_FALSE(app_json::executable_directory("").has_value());
  EXPECT_FALSE(app_json::executable_directory(R"("C:\Games\Hades\Hades.exe)").has_value());
}

TEST(AppJsonWorkingDirTest, FillsAMissingWorkingDirectory) {
  nlohmann::json entry = {{"name", "Hades"}, {"cmd", R"("C:\Games\Hades\Hades.exe")"}};

  EXPECT_TRUE(app_json::fill_working_directory(entry));
  EXPECT_EQ(entry["working-dir"], R"(C:\Games\Hades)");
}

TEST(AppJsonWorkingDirTest, KeepsAnExistingWorkingDirectory) {
  nlohmann::json entry = {{"name", "Hades"}, {"cmd", R"("C:\Games\Hades\Hades.exe")"}, {"working-dir", R"(D:\Elsewhere)"}};

  EXPECT_FALSE(app_json::fill_working_directory(entry));
  EXPECT_EQ(entry["working-dir"], R"(D:\Elsewhere)");
}

TEST(AppJsonWorkingDirTest, FillsAWorkingDirectoryThatIsEmptyOrNull) {
  nlohmann::json empty = {{"cmd", R"("C:\Games\Hades\Hades.exe")"}, {"working-dir", "   "}};
  EXPECT_TRUE(app_json::fill_working_directory(empty));
  EXPECT_EQ(empty["working-dir"], R"(C:\Games\Hades)");

  nlohmann::json missing = {{"cmd", R"("C:\Games\Hades\Hades.exe")"}};
  EXPECT_TRUE(app_json::fill_working_directory(missing));
}

TEST(AppJsonWorkingDirTest, IgnoresEntriesWithoutAUsableCommand) {
  nlohmann::json no_cmd = {{"name", "Hades"}};
  EXPECT_FALSE(app_json::fill_working_directory(no_cmd));
  EXPECT_FALSE(no_cmd.contains("working-dir"));

  nlohmann::json steam = {{"cmd", R"("C:\Program Files (x86)\Steam\steam.exe" -applaunch 1145360)"}};
  EXPECT_FALSE(app_json::fill_working_directory(steam));
  EXPECT_FALSE(steam.contains("working-dir"));

  nlohmann::json not_an_object = "Hades";
  EXPECT_FALSE(app_json::fill_working_directory(not_an_object));
}
