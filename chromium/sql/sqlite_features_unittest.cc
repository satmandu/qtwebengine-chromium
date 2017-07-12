// Copyright (c) 2012 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <stddef.h>
#include <stdint.h>

#include <string>

#include "base/bind.h"
#include "base/files/file_util.h"
#include "base/files/memory_mapped_file.h"
#include "base/files/scoped_temp_dir.h"
#include "sql/connection.h"
#include "sql/statement.h"
#include "sql/test/sql_test_base.h"
#include "sql/test/test_helpers.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "third_party/sqlite/sqlite3.h"

#if defined(OS_IOS)
#include "base/ios/ios_util.h"
#endif

#if defined(OS_MACOSX) && !defined(OS_IOS)
#include <CoreFoundation/CoreFoundation.h>
#include <CoreServices/CoreServices.h>

#include "base/mac/mac_util.h"
#include "base/mac/scoped_cftyperef.h"
#endif

// Test that certain features are/are-not enabled in our SQLite.

namespace {

void CaptureErrorCallback(int* error_pointer, std::string* sql_text,
                          int error, sql::Statement* stmt) {
  *error_pointer = error;
  const char* text = stmt ? stmt->GetSQLStatement() : NULL;
  *sql_text = text ? text : "no statement available";
}

class SQLiteFeaturesTest : public sql::SQLTestBase {
 public:
  SQLiteFeaturesTest() : error_(SQLITE_OK) {}

  void SetUp() override {
    SQLTestBase::SetUp();

    // The error delegate will set |error_| and |sql_text_| when any sqlite
    // statement operation returns an error code.
    db().set_error_callback(
        base::Bind(&CaptureErrorCallback, &error_, &sql_text_));
  }

  void TearDown() override {
    // If any error happened the original sql statement can be found in
    // |sql_text_|.
    EXPECT_EQ(SQLITE_OK, error_) << sql_text_;

    SQLTestBase::TearDown();
  }

  int error() { return error_; }

 private:
  // The error code of the most recent error.
  int error_;
  // Original statement which has caused the error.
  std::string sql_text_;
};

// Do not include fts1 support, it is not useful, and nobody is
// looking at it.
TEST_F(SQLiteFeaturesTest, NoFTS1) {
  ASSERT_EQ(SQLITE_ERROR, db().ExecuteAndReturnErrorCode(
      "CREATE VIRTUAL TABLE foo USING fts1(x)"));
}

// Do not include fts2 support, it is not useful, and nobody is
// looking at it.
TEST_F(SQLiteFeaturesTest, NoFTS2) {
  ASSERT_EQ(SQLITE_ERROR, db().ExecuteAndReturnErrorCode(
      "CREATE VIRTUAL TABLE foo USING fts2(x)"));
}

// fts3 used to be used for history files, and may also be used by WebDatabase
// clients.
TEST_F(SQLiteFeaturesTest, FTS3) {
  ASSERT_TRUE(db().Execute("CREATE VIRTUAL TABLE foo USING fts3(x)"));
}

#if !defined(USE_SYSTEM_SQLITE)
// Originally history used fts2, which Chromium patched to treat "foo*" as a
// prefix search, though the icu tokenizer would return it as two tokens {"foo",
// "*"}.  Test that fts3 works correctly.
TEST_F(SQLiteFeaturesTest, FTS3_Prefix) {
  const char kCreateSql[] =
      "CREATE VIRTUAL TABLE foo USING fts3(x, tokenize icu)";
  ASSERT_TRUE(db().Execute(kCreateSql));

  ASSERT_TRUE(db().Execute("INSERT INTO foo (x) VALUES ('test')"));

  sql::Statement s(db().GetUniqueStatement(
      "SELECT x FROM foo WHERE x MATCH 'te*'"));
  ASSERT_TRUE(s.Step());
  EXPECT_EQ("test", s.ColumnString(0));
}
#endif

#if !defined(USE_SYSTEM_SQLITE)
// Verify that Chromium's SQLite is compiled with HAVE_USLEEP defined.  With
// HAVE_USLEEP, SQLite uses usleep() with millisecond granularity.  Otherwise it
// uses sleep() with second granularity.
TEST_F(SQLiteFeaturesTest, UsesUsleep) {
  base::TimeTicks before = base::TimeTicks::Now();
  sqlite3_sleep(1);
  base::TimeDelta delta = base::TimeTicks::Now() - before;

  // It is not impossible for this to be over 1000 if things are compiled the
  // right way.  But it is very unlikely, most platforms seem to be around
  // <TBD>.
  LOG(ERROR) << "Milliseconds: " << delta.InMilliseconds();
  EXPECT_LT(delta.InMilliseconds(), 1000);
}
#endif

// Ensure that our SQLite version has working foreign key support with cascade
// delete support.
TEST_F(SQLiteFeaturesTest, ForeignKeySupport) {
  ASSERT_TRUE(db().Execute("PRAGMA foreign_keys=1"));
  ASSERT_TRUE(db().Execute("CREATE TABLE parents (id INTEGER PRIMARY KEY)"));
  ASSERT_TRUE(db().Execute(
      "CREATE TABLE children ("
      "    id INTEGER PRIMARY KEY,"
      "    pid INTEGER NOT NULL REFERENCES parents(id) ON DELETE CASCADE)"));

  // Inserting without a matching parent should fail with constraint violation.
  // Mask off any extended error codes for USE_SYSTEM_SQLITE.
  int insertErr = db().ExecuteAndReturnErrorCode(
      "INSERT INTO children VALUES (10, 1)");
  EXPECT_EQ(SQLITE_CONSTRAINT, (insertErr&0xff));

  size_t rows;
  EXPECT_TRUE(sql::test::CountTableRows(&db(), "children", &rows));
  EXPECT_EQ(0u, rows);

  // Inserting with a matching parent should work.
  ASSERT_TRUE(db().Execute("INSERT INTO parents VALUES (1)"));
  EXPECT_TRUE(db().Execute("INSERT INTO children VALUES (11, 1)"));
  EXPECT_TRUE(db().Execute("INSERT INTO children VALUES (12, 1)"));
  EXPECT_TRUE(sql::test::CountTableRows(&db(), "children", &rows));
  EXPECT_EQ(2u, rows);

  // Deleting the parent should cascade, i.e., delete the children as well.
  ASSERT_TRUE(db().Execute("DELETE FROM parents"));
  EXPECT_TRUE(sql::test::CountTableRows(&db(), "children", &rows));
  EXPECT_EQ(0u, rows);
}

#if defined(MOJO_APPTEST_IMPL) || defined(OS_IOS)
// If the platform cannot support SQLite mmap'ed I/O, make sure SQLite isn't
// offering to support it.
TEST_F(SQLiteFeaturesTest, NoMmap) {
#if defined(OS_IOS) && defined(USE_SYSTEM_SQLITE)
  if (base::ios::IsRunningOnIOS10OrLater()) {
    // iOS 10 added mmap support for sqlite.
    return;
  }
#endif

  // For recent versions of SQLite, SQLITE_MAX_MMAP_SIZE=0 can be used to
  // disable mmap support.  Alternately, sqlite3_config() could be used.  In
  // that case, the pragma will run successfully, but the size will always be 0.
  //
  // The SQLite embedded in older iOS releases predates the addition of mmap
  // support.  In that case the pragma will run without error, but no results
  // are returned when querying the value.
  //
  // MojoVFS implements a no-op for xFileControl().  PRAGMA mmap_size is
  // implemented in terms of SQLITE_FCNTL_MMAP_SIZE.  In that case, the pragma
  // will succeed but with no effect.
  ignore_result(db().Execute("PRAGMA mmap_size = 1048576"));
  sql::Statement s(db().GetUniqueStatement("PRAGMA mmap_size"));
  ASSERT_TRUE(!s.Step() || !s.ColumnInt64(0));
}
#endif

#if !defined(MOJO_APPTEST_IMPL)
// Verify that OS file writes are reflected in the memory mapping of a
// memory-mapped file.  Normally SQLite writes to memory-mapped files using
// memcpy(), which should stay consistent.  Our SQLite is slightly patched to
// mmap read only, then write using OS file writes.  If the memory-mapped
// version doesn't reflect the OS file writes, SQLite's memory-mapped I/O should
// be disabled on this platform using SQLITE_MAX_MMAP_SIZE=0.
TEST_F(SQLiteFeaturesTest, Mmap) {
#if defined(OS_IOS) && defined(USE_SYSTEM_SQLITE)
  if (!base::ios::IsRunningOnIOS10OrLater()) {
    // iOS9's sqlite does not support mmap, so this test must be skipped.
    return;
  }
#endif

  // Try to turn on mmap'ed I/O.
  ignore_result(db().Execute("PRAGMA mmap_size = 1048576"));
  {
    sql::Statement s(db().GetUniqueStatement("PRAGMA mmap_size"));

#if !defined(USE_SYSTEM_SQLITE)
    // With Chromium's version of SQLite, the setting should always be non-zero.
    ASSERT_TRUE(s.Step());
    ASSERT_GT(s.ColumnInt64(0), 0);
#else
    // With the system SQLite, don't verify underlying mmap functionality if the
    // SQLite is too old to support mmap, or if mmap is disabled (see NoMmap
    // test).  USE_SYSTEM_SQLITE is not bundled into the NoMmap case because
    // whether mmap is enabled or not is outside of Chromium's control.
    if (!s.Step() || !s.ColumnInt64(0))
      return;
#endif
  }
  db().Close();

  const uint32_t kFlags =
      base::File::FLAG_OPEN | base::File::FLAG_READ | base::File::FLAG_WRITE;
  char buf[4096];

  // Create a file with a block of '0', a block of '1', and a block of '2'.
  {
    base::File f(db_path(), kFlags);
    ASSERT_TRUE(f.IsValid());
    memset(buf, '0', sizeof(buf));
    ASSERT_EQ(f.Write(0*sizeof(buf), buf, sizeof(buf)), (int)sizeof(buf));

    memset(buf, '1', sizeof(buf));
    ASSERT_EQ(f.Write(1*sizeof(buf), buf, sizeof(buf)), (int)sizeof(buf));

    memset(buf, '2', sizeof(buf));
    ASSERT_EQ(f.Write(2*sizeof(buf), buf, sizeof(buf)), (int)sizeof(buf));
  }

  // mmap the file and verify that everything looks right.
  {
    base::MemoryMappedFile m;
    ASSERT_TRUE(m.Initialize(db_path()));

    memset(buf, '0', sizeof(buf));
    ASSERT_EQ(0, memcmp(buf, m.data() + 0*sizeof(buf), sizeof(buf)));

    memset(buf, '1', sizeof(buf));
    ASSERT_EQ(0, memcmp(buf, m.data() + 1*sizeof(buf), sizeof(buf)));

    memset(buf, '2', sizeof(buf));
    ASSERT_EQ(0, memcmp(buf, m.data() + 2*sizeof(buf), sizeof(buf)));

    // Scribble some '3' into the first page of the file, and verify that it
    // looks the same in the memory mapping.
    {
      base::File f(db_path(), kFlags);
      ASSERT_TRUE(f.IsValid());
      memset(buf, '3', sizeof(buf));
      ASSERT_EQ(f.Write(0*sizeof(buf), buf, sizeof(buf)), (int)sizeof(buf));
    }
    ASSERT_EQ(0, memcmp(buf, m.data() + 0*sizeof(buf), sizeof(buf)));

    // Repeat with a single '4' in case page-sized blocks are different.
    const size_t kOffset = 1*sizeof(buf) + 123;
    ASSERT_NE('4', m.data()[kOffset]);
    {
      base::File f(db_path(), kFlags);
      ASSERT_TRUE(f.IsValid());
      buf[0] = '4';
      ASSERT_EQ(f.Write(kOffset, buf, 1), 1);
    }
    ASSERT_EQ('4', m.data()[kOffset]);
  }
}
#endif

// Verify that http://crbug.com/248608 is fixed.  In this bug, the
// compiled regular expression is effectively cached with the prepared
// statement, causing errors if the regular expression is rebound.
TEST_F(SQLiteFeaturesTest, CachedRegexp) {
  ASSERT_TRUE(db().Execute("CREATE TABLE r (id INTEGER UNIQUE, x TEXT)"));
  ASSERT_TRUE(db().Execute("INSERT INTO r VALUES (1, 'this is a test')"));
  ASSERT_TRUE(db().Execute("INSERT INTO r VALUES (2, 'that was a test')"));
  ASSERT_TRUE(db().Execute("INSERT INTO r VALUES (3, 'this is a stickup')"));
  ASSERT_TRUE(db().Execute("INSERT INTO r VALUES (4, 'that sucks')"));

  const char* kSimpleSql = "SELECT SUM(id) FROM r WHERE x REGEXP ?";
  sql::Statement s(db().GetCachedStatement(SQL_FROM_HERE, kSimpleSql));

  s.BindString(0, "this.*");
  ASSERT_TRUE(s.Step());
  EXPECT_EQ(4, s.ColumnInt(0));

  s.Reset(true);
  s.BindString(0, "that.*");
  ASSERT_TRUE(s.Step());
  EXPECT_EQ(6, s.ColumnInt(0));

  s.Reset(true);
  s.BindString(0, ".*test");
  ASSERT_TRUE(s.Step());
  EXPECT_EQ(3, s.ColumnInt(0));

  s.Reset(true);
  s.BindString(0, ".* s[a-z]+");
  ASSERT_TRUE(s.Step());
  EXPECT_EQ(7, s.ColumnInt(0));
}

#if defined(OS_MACOSX) && !defined(OS_IOS)
base::ScopedCFTypeRef<CFURLRef> CFURLRefForPath(const base::FilePath& path){
  base::ScopedCFTypeRef<CFStringRef> urlString(
      CFStringCreateWithFileSystemRepresentation(
          kCFAllocatorDefault, path.value().c_str()));
  base::ScopedCFTypeRef<CFURLRef> url(
      CFURLCreateWithFileSystemPath(kCFAllocatorDefault, urlString,
                                    kCFURLPOSIXPathStyle, FALSE));
  return url;
}

// If a database file is marked to be excluded from Time Machine, verify that
// journal files are also excluded.
// TODO(shess): Disabled because CSBackupSetItemExcluded() does not work on the
// bots, though it's fine on dev machines.  See <http://crbug.com/410350>.
TEST_F(SQLiteFeaturesTest, DISABLED_TimeMachine) {
  ASSERT_TRUE(db().Execute("CREATE TABLE t (id INTEGER PRIMARY KEY)"));
  db().Close();

  base::FilePath journal(db_path().value() + FILE_PATH_LITERAL("-journal"));
  ASSERT_TRUE(GetPathExists(db_path()));
  ASSERT_TRUE(GetPathExists(journal));

  base::ScopedCFTypeRef<CFURLRef> dbURL(CFURLRefForPath(db_path()));
  base::ScopedCFTypeRef<CFURLRef> journalURL(CFURLRefForPath(journal));

  // Not excluded to start.
  EXPECT_FALSE(CSBackupIsItemExcluded(dbURL, NULL));
  EXPECT_FALSE(CSBackupIsItemExcluded(journalURL, NULL));

  // Exclude the main database file.
  EXPECT_TRUE(base::mac::SetFileBackupExclusion(db_path()));

  Boolean excluded_by_path = FALSE;
  EXPECT_TRUE(CSBackupIsItemExcluded(dbURL, &excluded_by_path));
  EXPECT_FALSE(excluded_by_path);
  EXPECT_FALSE(CSBackupIsItemExcluded(journalURL, NULL));

  EXPECT_TRUE(db().Open(db_path()));
  ASSERT_TRUE(db().Execute("INSERT INTO t VALUES (1)"));
  EXPECT_TRUE(CSBackupIsItemExcluded(dbURL, &excluded_by_path));
  EXPECT_FALSE(excluded_by_path);
  EXPECT_TRUE(CSBackupIsItemExcluded(journalURL, &excluded_by_path));
  EXPECT_FALSE(excluded_by_path);

  // TODO(shess): In WAL mode this will touch -wal and -shm files.  -shm files
  // could be always excluded.
}
#endif

#if !defined(USE_SYSTEM_SQLITE)
// Test that Chromium's patch to make auto_vacuum integrate with
// SQLITE_FCNTL_CHUNK_SIZE is working.
TEST_F(SQLiteFeaturesTest, SmartAutoVacuum) {
  // Turn on auto_vacuum, and set the page size low to make results obvious.
  // These settings require re-writing the database, which VACUUM does.
  ASSERT_TRUE(db().Execute("PRAGMA auto_vacuum = FULL"));
  ASSERT_TRUE(db().Execute("PRAGMA page_size = 1024"));
  ASSERT_TRUE(db().Execute("VACUUM"));

  // Code-coverage of the PRAGMA set/get implementation.
  const char kPragmaSql[] = "PRAGMA auto_vacuum_slack_pages";
  ASSERT_EQ("0", sql::test::ExecuteWithResult(&db(), kPragmaSql));
  ASSERT_TRUE(db().Execute("PRAGMA auto_vacuum_slack_pages = 4"));
  ASSERT_EQ("4", sql::test::ExecuteWithResult(&db(), kPragmaSql));
  // Max out at 255.
  ASSERT_TRUE(db().Execute("PRAGMA auto_vacuum_slack_pages = 1000"));
  ASSERT_EQ("255", sql::test::ExecuteWithResult(&db(), kPragmaSql));
  ASSERT_TRUE(db().Execute("PRAGMA auto_vacuum_slack_pages = 0"));

  // With page_size=1024, the following will insert rows which take up an
  // overflow page, plus a small header in a b-tree node.  An empty table takes
  // a single page, so for small row counts each insert will add one page, and
  // each delete will remove one page.
  const char kCreateSql[] = "CREATE TABLE t (id INTEGER PRIMARY KEY, value)";
  const char kInsertSql[] = "INSERT INTO t (value) VALUES (randomblob(980))";
#if !defined(OS_WIN)
  const char kDeleteSql[] = "DELETE FROM t WHERE id = (SELECT MIN(id) FROM t)";
#endif

  // This database will be 34 overflow pages plus the table's root page plus the
  // SQLite header page plus the freelist page.
  ASSERT_TRUE(db().Execute(kCreateSql));
  {
    sql::Statement s(db().GetUniqueStatement(kInsertSql));
    for (int i = 0; i < 34; ++i) {
      s.Reset(true);
      ASSERT_TRUE(s.Run());
    }
  }
  ASSERT_EQ("37", sql::test::ExecuteWithResult(&db(), "PRAGMA page_count"));

  // http://sqlite.org/mmap.html indicates that Windows will silently fail when
  // truncating a memory-mapped file.  That pretty much invalidates these tests
  // against the actual file size.
#if !defined(OS_WIN)
  // Each delete will delete a single page, including crossing a
  // multiple-of-four boundary.
  {
    sql::Statement s(db().GetUniqueStatement(kDeleteSql));
    for (int i = 0; i < 5; ++i) {
      int64_t file_size_before, file_size_after;
      ASSERT_TRUE(base::GetFileSize(db_path(), &file_size_before));

      s.Reset(true);
      ASSERT_TRUE(s.Run());

      ASSERT_TRUE(base::GetFileSize(db_path(), &file_size_after));
      ASSERT_EQ(file_size_after, file_size_before - 1024);
    }
  }

  // Turn on "smart" auto-vacuum to remove 4 pages at a time.
  ASSERT_TRUE(db().Execute("PRAGMA auto_vacuum_slack_pages = 4"));

  // No pages removed, then four deleted at once.
  {
    sql::Statement s(db().GetUniqueStatement(kDeleteSql));
    for (int i = 0; i < 3; ++i) {
      int64_t file_size_before, file_size_after;
      ASSERT_TRUE(base::GetFileSize(db_path(), &file_size_before));

      s.Reset(true);
      ASSERT_TRUE(s.Run());

      ASSERT_TRUE(base::GetFileSize(db_path(), &file_size_after));
      ASSERT_EQ(file_size_after, file_size_before);
    }

    int64_t file_size_before, file_size_after;
    ASSERT_TRUE(base::GetFileSize(db_path(), &file_size_before));

    s.Reset(true);
    ASSERT_TRUE(s.Run());

    ASSERT_TRUE(base::GetFileSize(db_path(), &file_size_after));
    ASSERT_EQ(file_size_after, file_size_before - 4096);
  }
#endif
}
#endif  // !defined(USE_SYSTEM_SQLITE)

}  // namespace
