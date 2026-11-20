/**
 * @file tests/integration/test_boost_version_consistency.cpp
 * @brief Tests for the Boost system-version and fallback-version contracts.
 */

// test includes
#include "../tests_common.h"

// standard includes
#include <format>
#include <regex>
#include <string>
#include <string_view>

// local includes
#include "src/file_handler.h"

namespace {
  /**
   * @brief Read a project file copied into the test runtime directory.
   *
   * @param path Project-relative file path.
   * @return File contents.
   */
  std::string read_project_file(const std::string_view path) {
    const auto path_string = std::format("{}/{}", SOLSTICE_TEST_BIN_DIR, path);
    auto content = file_handler::read_file(path_string.c_str());
    EXPECT_FALSE(content.empty()) << "Unable to read " << path;
    return content;
  }

  /**
   * @brief Extract the first capture group from text.
   *
   * @param content Text to search.
   * @param pattern Regular expression containing a capture group.
   * @return The first captured value, or an empty string when no match exists.
   */
  std::string extract_value(const std::string &content, const std::regex &pattern) {
    std::smatch match;
    EXPECT_TRUE(std::regex_search(content, match, pattern));
    return match.size() > 1 ? match[1].str() : std::string {};
  }
}  // namespace

TEST(BoostVersionConsistencyTest, SystemPackageAllowsCompatibleVersions) {
  const auto dependency = read_project_file("cmake/dependencies/Boost_Solstice.cmake");
  const auto documentation = read_project_file("docs/building.md");
  const auto common_macros = read_project_file("cmake/macros/common.cmake");
  const auto options = read_project_file("cmake/prep/options.cmake");
  const auto minimum_version = extract_value(
    dependency,
    std::regex {R"regex(set\(BOOST_MINIMUM_VERSION "([0-9]+\.[0-9]+\.[0-9]+)"\))regex"}
  );

  EXPECT_NE(
    dependency.find("find_package(Boost ${BOOST_MINIMUM_VERSION} CONFIG COMPONENTS ${BOOST_COMPONENTS})"),
    std::string::npos
  );
  EXPECT_EQ(dependency.find("EXACT"), std::string::npos);
  EXPECT_EQ(dependency.find("FetchContent"), std::string::npos);
  EXPECT_EQ(dependency.find("\n        system"), std::string::npos);
  EXPECT_NE(dependency.find("CPMGetPackage(Boost)"), std::string::npos);
  EXPECT_NE(documentation.find(std::format("Boost `{}` or newer", minimum_version)), std::string::npos);
  EXPECT_EQ(common_macros.find("FETCH_CONTENT_BOOST_USED"), std::string::npos);
  EXPECT_NE(common_macros.find("CPM_BOOST_USED"), std::string::npos);
  EXPECT_NE(options.find("option(BOOST_USE_STATIC \"Use static boost libraries.\" ON)"), std::string::npos);
  EXPECT_EQ(options.find("option(BOOST_USE_STATIC \"Use static boost libraries.\" OFF)"), std::string::npos);
  EXPECT_NE(documentation.find("Static Boost libraries are preferred on every platform"), std::string::npos);
  EXPECT_NE(documentation.find("-DBOOST_USE_STATIC=OFF"), std::string::npos);
  EXPECT_NE(documentation.find("packaged static Boost libraries"), std::string::npos);
}
