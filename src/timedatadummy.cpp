#include <stdint.h>

// UPGRADE NOTE: GetAdjustedTime() stub for build configuration
// CRITICAL: GetAdjustedTime() is REMOVED in Bitcoin 28.x - MUST preserve in Blackcoin More
// This stub exists for build configurations that don't link timedata.cpp
// The real implementation is in src/timedata.cpp

// needed when linking transaction.cpp, since we are not going to pull real GetAdjustedTimeSeconds() from timedata.cpp
int64_t GetAdjustedTimeSeconds()
{
    return 0;
}
