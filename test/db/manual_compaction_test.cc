//  Copyright (c) 2011-present, Facebook, Inc.  All rights reserved.
//  This source code is licensed under the BSD-style license found in the
//  LICENSE file in the root directory of this source tree. An additional grant
//  of patent rights can be found in the PATENTS file in the same directory.
//
// Test for issue 178: a manual compaction causes deleted data to reappear.
#include <iostream>
#include <sstream>
#include <cstdlib>

#include "vidardb/db.h"
#include "vidardb/slice.h"
#include "vidardb/write_batch.h"
#include "util/testharness.h"
#include "port/port.h"

using namespace vidardb;

namespace {

const int kNumKeys = 1100000;

std::string Key1(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "my_key_%d", i);
  return buf;
}

std::string Key2(int i) {
  return Key1(i) + "_xxx";
}

class ManualCompactionTest : public testing::Test {
 public:
  ManualCompactionTest() {
    // Get rid of any state from an old run.
    dbname_ = vidardb::test::TmpDir() + "/vidardb_cbug_test";
    DestroyDB(dbname_, vidardb::Options());
  }

  std::string dbname_;
};

TEST_F(ManualCompactionTest, CompactTouchesAllKeys) {
  for (int iter = 0; iter < 2; ++iter) {
    DB* db;
    Options options;
    if (iter == 0) { // level compaction
      options.num_levels = 3;
      options.compaction_style = kCompactionStyleLevel;
    }
    options.create_if_missing = true;
    options.compression = vidardb::kNoCompression;
    ASSERT_OK(DB::Open(options, dbname_, &db));

    db->Put(WriteOptions(), Slice("key1"), Slice("destroy"));
    db->Put(WriteOptions(), Slice("key2"), Slice("destroy"));
    db->Put(WriteOptions(), Slice("key3"), Slice("value3"));
    db->Put(WriteOptions(), Slice("key4"), Slice("destroy"));

    Slice key4("key4");
    db->CompactRange(CompactRangeOptions(), nullptr, &key4);
    Iterator* itr = db->NewIterator(ReadOptions());
    itr->SeekToFirst();
    ASSERT_TRUE(itr->Valid());
    ASSERT_EQ("key3", itr->key().ToString());
    itr->Next();
    ASSERT_TRUE(!itr->Valid());
    delete itr;

    delete db;
    DestroyDB(dbname_, options);
  }
}

TEST_F(ManualCompactionTest, Test) {
  // Open database.  Disable compression since it affects the creation
  // of layers and the code below is trying to test against a very
  // specific scenario.
  vidardb::DB* db;
  vidardb::Options db_options;
  db_options.create_if_missing = true;
  db_options.compression = vidardb::kNoCompression;
  ASSERT_OK(vidardb::DB::Open(db_options, dbname_, &db));

  // create first key range
  vidardb::WriteBatch batch;
  for (int i = 0; i < kNumKeys; i++) {
    batch.Put(Key1(i), "value for range 1 key");
  }
  ASSERT_OK(db->Write(vidardb::WriteOptions(), &batch));

  // create second key range
  batch.Clear();
  for (int i = 0; i < kNumKeys; i++) {
    batch.Put(Key2(i), "value for range 2 key");
  }
  ASSERT_OK(db->Write(vidardb::WriteOptions(), &batch));

  // delete second key range
  batch.Clear();
  for (int i = 0; i < kNumKeys; i++) {
    batch.Delete(Key2(i));
  }
  ASSERT_OK(db->Write(vidardb::WriteOptions(), &batch));

  // compact database
  std::string start_key = Key1(0);
  std::string end_key = Key1(kNumKeys - 1);
  vidardb::Slice least(start_key.data(), start_key.size());
  vidardb::Slice greatest(end_key.data(), end_key.size());

  // commenting out the line below causes the example to work correctly
  db->CompactRange(CompactRangeOptions(), &least, &greatest);

  // count the keys
  vidardb::Iterator* iter = db->NewIterator(vidardb::ReadOptions());
  int num_keys = 0;
  for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
    num_keys++;
  }
  delete iter;
  ASSERT_EQ(kNumKeys, num_keys) << "Bad number of keys";

  // close database
  delete db;
  DestroyDB(dbname_, vidardb::Options());
}

}  // anonymous namespace

int main(int argc, char** argv) {
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}
