/*
 * This file is part of Percona Server for MongoDB.
 * Licensed under the GNU Affero General Public License, version 3 or later.
 */

#include "mongo/platform/basic.h"

#include <rocksdb/version.h>

#include <string>
#include <vector>

#include "mongo/unittest/unittest.h"
#include "rocks_stats_parser.h"

namespace mongo {
namespace {

#define ROCKSDB_VERSION_NUM (((ROCKSDB_MAJOR * 100) + ROCKSDB_MINOR) * 100 + ROCKSDB_PATCH)

// Match the linked RocksDB layout and vary values to check field mapping and units.
BSONObj parseStats(const std::string& intervalWAL) {
    const std::string header =
#if ROCKSDB_VERSION_NUM < 50001
        "Level    Files   Size(MB) Score Read(GB)  Rn(GB) Rnp1(GB) Write(GB) Wnew(GB) Moved(GB) W-Amp Rd(MB/s) Wr(MB/s) Comp(sec) Comp(cnt) Avg(sec) KeyIn KeyDrop";
#elif ROCKSDB_VERSION_NUM < 50405
        "Level    Files   Size(MB} Score Read(GB}  Rn(GB} Rnp1(GB} Write(GB} Wnew(GB} Moved(GB} W-Amp Rd(MB/s} Wr(MB/s} Comp(sec} Comp(cnt} Avg(sec} KeyIn KeyDrop";
#elif ROCKSDB_VERSION_NUM < 62800
        "Level    Files   Size     Score Read(GB)  Rn(GB) Rnp1(GB) Write(GB) Wnew(GB) Moved(GB) W-Amp Rd(MB/s) Wr(MB/s) Comp(sec) Comp(cnt) Avg(sec) KeyIn KeyDrop";
#else
        "Level    Files   Size     Score Read(GB)  Rn(GB) Rnp1(GB) Write(GB) Wnew(GB) Moved(GB) W-Amp Rd(MB/s) Wr(MB/s) Comp(sec) CompMergeCPU(sec) Comp(cnt) Avg(sec) KeyIn KeyDrop Rblob(GB) Wblob(GB)";
#endif
    const std::string level =
        " Int 1/0 1.00"
#if ROCKSDB_VERSION_NUM >= 50405
        " MB"
#endif
        " 0.2 2.0 3.0 4.0 5.0 -0.5 6.0 7.0 8.0 9.0 10.25"
#if ROCKSDB_VERSION_NUM >= 62800
        " 11.50"
#endif
        " 12 13.125 14 15"
#if ROCKSDB_VERSION_NUM >= 62800
        " 16.0 17.0"
#endif
        ;
    const std::vector<std::string> lines = {
        header,
        "------------------------",
        level,
        "Uptime(secs): 1.0 total, 1.0 interval",
        "Flush(GB): cumulative 0.000, interval 0.000",
        "AddFile(GB): cumulative 0.000, interval 0.000",
        "AddFile(Total Files): cumulative 0, interval 0",
        "AddFile(L0 Files): cumulative 0, interval 0",
        "AddFile(Keys): cumulative 0, interval 0",
        "Cumulative compaction: 0.00 GB write, 0.00 MB/s write, 0.00 GB read, 0.00 MB/s read, 0.0 seconds",
        "Interval compaction: 0.00 GB write, 0.00 MB/s write, 0.00 GB read, 0.00 MB/s read, 0.0 seconds",
        "Stalls(count): 0 level0_slowdown, 0 level0_slowdown_with_compaction, 0 level0_numfiles, 0 level0_numfiles_with_compaction, 0 stop for pending_compaction_bytes, 0 slowdown for pending_compaction_bytes, 0 memtable_compaction, 0 memtable_slowdown, interval 0 total count",
        "** DB Stats **",
        "Uptime(secs): 1.0 total, 1.0 interval",
        "Cumulative writes: 1 writes, 1 keys, 1 commit groups, 1.0 writes per commit group, ingest: 0.00 GB, 0.00 MB/s",
        "Cumulative WAL: 1 writes, 0 syncs, 1.00 writes per sync, written: 0.00 GB, 0.00 MB/s",
        "Cumulative stall: 00:00:0.000 H:M:S, 0.0 percent",
        "Interval writes: 1 writes, 1 keys, 1 commit groups, 1.0 writes per commit group, ingest: 0.00 MB, 0.00 MB/s",
        intervalWAL,
        "Interval stall: 00:00:0.000 H:M:S, 0.0 percent",
    };
    RocksStatsParser parser;
    for (const auto& line : lines) {
        parser.parse(line);
    }
    BSONObjBuilder builder;
    parser.done(builder);
    return builder.obj();
}

TEST(RocksStatsParserTest, NegativeNewBytesAndAdditionalColumns) {
    const auto stats = parseStats("Interval WAL: 1 writes, 0 syncs, 1.00 writes per sync, written: 1.25 GB, 2.50 MB/s");
    const auto compaction = stats.getObjectField("compaction-stats");
    ASSERT_FALSE(compaction.hasField("error"));
    ASSERT_EQUALS(32, compaction.nFields());
    const auto level = compaction.getObjectField("level-stats").getObjectField("Int");
    ASSERT_EQUALS(-0.5, level["bytes-new-GB"].numberDouble());
    ASSERT_EQUALS(10.25, level["compactions-sec"].numberDouble());
    ASSERT_EQUALS(12, level["compactions-cnt"].numberInt());
    ASSERT_EQUALS(13.125, level["compaction-avg-len-sec"].numberDouble());
}

TEST(RocksStatsParserTest, IntervalWALPreservesMegabytesForGigabyteInput) {
    const auto stats = parseStats("Interval WAL: 1 writes, 0 syncs, 1.00 writes per sync, written: 1.25 GB, 2.50 MB/s");
    const auto db = stats.getObjectField("db-stats");
    ASSERT_FALSE(db.hasField("error"));
    ASSERT_EQUALS(28, db.nFields());
    ASSERT_EQUALS(1280.0, db["interval-WAL-written-MB"].numberDouble());
    ASSERT_EQUALS(2.5, db["interval-WAL-written-MB-s"].numberDouble());
}

TEST(RocksStatsParserTest, IntervalWALAcceptsLegacyMegabytes) {
    const auto stats = parseStats("Interval WAL: 1 writes, 0 syncs, 1.00 writes per sync, written: 2.50 MB, 3.25 MB/s");
    const auto db = stats.getObjectField("db-stats");
    ASSERT_FALSE(db.hasField("error"));
    ASSERT_EQUALS(2.5, db["interval-WAL-written-MB"].numberDouble());
    ASSERT_EQUALS(3.25, db["interval-WAL-written-MB-s"].numberDouble());
}

}  // namespace
}  // namespace mongo
