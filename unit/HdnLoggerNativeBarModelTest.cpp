#include <catch2/catch_all.hpp>

#include "../skyrim-platform/src/tilted/ui/HdnLoggerNativeBarModel.h"

TEST_CASE("Logger native bar follows the CEF ping-pong contract",
          "[HdnLoggerNativeBar]")
{
  constexpr double travelMs = 1388.8889;

  REQUIRE(CEFUtils::HdnLoggerBarValue(0.04, 0.0, travelMs) ==
          Catch::Approx(0.04));
  REQUIRE(CEFUtils::HdnLoggerBarValue(0.04, travelMs * 0.96, travelMs) ==
          Catch::Approx(1.0));
  REQUIRE(CEFUtils::HdnLoggerBarValue(1.04, 0.0, travelMs) ==
          Catch::Approx(0.96));
  REQUIRE(CEFUtils::HdnLoggerBarValue(1.04, travelMs * 0.96, travelMs) ==
          Catch::Approx(0.0));
  REQUIRE(CEFUtils::HdnLoggerBarValue(0.04, travelMs * 2.0, travelMs) ==
          Catch::Approx(0.04));
}

TEST_CASE("Logger native bar rejects unsafe geometry", "[HdnLoggerNativeBar]")
{
  REQUIRE(CEFUtils::IsHdnLoggerBarGeometryValid(
    1920.0, 1080.0, 430.0, 330.0, 620.0, 190.0, 6.0, 1388.8889, 0.04));
  REQUIRE_FALSE(CEFUtils::IsHdnLoggerBarGeometryValid(
    0.0, 1080.0, 430.0, 330.0, 620.0, 190.0, 6.0, 1388.8889, 0.04));
  REQUIRE_FALSE(CEFUtils::IsHdnLoggerBarGeometryValid(
    1920.0, 1080.0, 430.0, 330.0, 620.0, 190.0, 6.0, 0.0, 0.04));
  REQUIRE_FALSE(CEFUtils::IsHdnLoggerBarGeometryValid(
    1920.0, 1080.0, 430.0, 330.0, 2000.0, 190.0, 6.0, 1388.8889, 0.04));
}
