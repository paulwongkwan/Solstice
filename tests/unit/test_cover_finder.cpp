/**
 * @file tests/unit/test_cover_finder.cpp
 * @brief Tests for the IGDB and Steam cover finder helpers.
 */

// test includes
#include "../tests_common.h"

// local includes
#include <src/cover_finder.h>

TEST(CoverFinderHostTest, AllowsIgdbAndSteamImageHosts) {
  EXPECT_TRUE(cover_finder::is_allowed_cover_host("images.igdb.com"));
  EXPECT_TRUE(cover_finder::is_allowed_cover_host("Images.IGDB.com"));
  EXPECT_TRUE(cover_finder::is_allowed_cover_host("cdn.cloudflare.steamstatic.com"));
  EXPECT_TRUE(cover_finder::is_allowed_cover_host("shared.akamai.steamstatic.com"));
  EXPECT_FALSE(cover_finder::is_allowed_cover_host("example.com"));
  EXPECT_FALSE(cover_finder::is_allowed_cover_host(""));
}

TEST(CoverFinderUrlTest, BuildsSteamCoverAndThumbnailUrls) {
  EXPECT_EQ(
    cover_finder::steam_cover_url(1145360),
    "https://cdn.cloudflare.steamstatic.com/steam/apps/1145360/library_600x900_2x.jpg");
  EXPECT_EQ(
    cover_finder::steam_thumbnail_url(1145360),
    "https://cdn.cloudflare.steamstatic.com/steam/apps/1145360/library_600x900.jpg");
}

TEST(CoverFinderUrlTest, UpgradesIgdbImageUrls) {
  EXPECT_EQ(
    cover_finder::igdb_cover_url("//images.igdb.com/igdb/image/upload/t_thumb/abc123.jpg"),
    "https://images.igdb.com/igdb/image/upload/t_cover_big_2x/abc123.png");
  EXPECT_EQ(
    cover_finder::igdb_cover_url("https://images.igdb.com/igdb/image/upload/t_cover_small/abc123.jpg"),
    "https://images.igdb.com/igdb/image/upload/t_cover_big_2x/abc123.png");
}

TEST(CoverFinderUrlTest, RejectsForeignIgdbUrls) {
  EXPECT_TRUE(cover_finder::igdb_cover_url("").empty());
  EXPECT_TRUE(cover_finder::igdb_cover_url("https://example.com/igdb/image/upload/t_thumb/abc.jpg").empty());
  EXPECT_TRUE(cover_finder::igdb_cover_url("https://images.igdb.com/no-upload-segment").empty());
}

TEST(CoverFinderSteamTest, ParsesApplicationsFromTheStoreSearch) {
  const auto candidates = cover_finder::parse_steam_results(R"({
    "total": 2,
    "items": [
      {"type": "app", "name": "Hades", "id": 1145360, "tiny_image": "https://example/tiny.jpg"},
      {"type": "bundle", "name": "Hades Bundle", "id": 999}
    ]
  })");

  ASSERT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0].source, "Steam");
  EXPECT_EQ(candidates[0].title, "Hades");
  EXPECT_EQ(candidates[0].key, "steam_1145360");
  EXPECT_EQ(candidates[0].image_url, cover_finder::steam_cover_url(1145360));
}

TEST(CoverFinderSteamTest, IgnoresEmptyAndMalformedResponses) {
  EXPECT_TRUE(cover_finder::parse_steam_results("").empty());
  EXPECT_TRUE(cover_finder::parse_steam_results("not json").empty());
  EXPECT_TRUE(cover_finder::parse_steam_results("{\"total\":0}").empty());
  EXPECT_TRUE(cover_finder::parse_steam_results("{\"items\":[{\"type\":\"app\"}]}").empty());
}

TEST(CoverFinderIgdbTest, ParsesGamesWithCovers) {
  const auto candidates = cover_finder::parse_igdb_results(R"([
    {"id": 1027, "name": "Hades", "cover": {"id": 1, "url": "//images.igdb.com/igdb/image/upload/t_thumb/abc.jpg"}},
    {"id": 1028, "name": "Hades II"},
    {"id": 0, "name": "Missing id", "cover": {"url": "//images.igdb.com/igdb/image/upload/t_thumb/def.jpg"}}
  ])");

  ASSERT_EQ(candidates.size(), 1u);
  EXPECT_EQ(candidates[0].source, "IGDB");
  EXPECT_EQ(candidates[0].title, "Hades");
  EXPECT_EQ(candidates[0].key, "igdb_1027");
  EXPECT_EQ(candidates[0].image_url, "https://images.igdb.com/igdb/image/upload/t_cover_big_2x/abc.png");
}

TEST(CoverFinderIgdbTest, IgnoresEmptyAndMalformedResponses) {
  EXPECT_TRUE(cover_finder::parse_igdb_results("").empty());
  EXPECT_TRUE(cover_finder::parse_igdb_results("not json").empty());
  EXPECT_TRUE(cover_finder::parse_igdb_results("{}").empty());
  EXPECT_TRUE(cover_finder::parse_igdb_results(R"([{"id": 1, "name": "No cover"}])").empty());
}
