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
