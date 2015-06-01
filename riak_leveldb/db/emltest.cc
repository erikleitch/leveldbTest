// Copyright (c) 2011 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#include "leveldb/db.h"
#include "leveldb/filter_policy.h"
#include "db/db_impl.h"
#include "db/filename.h"
#include "db/version_set.h"
#include "db/write_batch_internal.h"
#include "leveldb/cache.h"
#include "leveldb/env.h"
#include "leveldb/table.h"
#include "util/hash.h"
#include "util/logging.h"
#include "util/mutexlock.h"
#include "util/testharness.h"
#include "util/testutil.h"

namespace leveldb {

static std::string RandomString(Random* rnd, int len) {
  std::string r;
  test::RandomString(rnd, len, &r);
  return r;
}

namespace {
class AtomicCounter {
 private:
  port::Mutex mu_;
  int count_;
 public:
  AtomicCounter() : count_(0) { }
  void Increment() {
    MutexLock l(&mu_);
    count_++;
  }
  int Read() {
    MutexLock l(&mu_);
    return count_;
  }
  void Reset() {
    MutexLock l(&mu_);
    count_ = 0;
  }
};
}

// Special Env used to delay background operations
class SpecialEnv : public EnvWrapper {
 public:
  // sstable Sync() calls are blocked while this pointer is non-NULL.
  port::AtomicPointer delay_sstable_sync_;

  // Simulate no-space errors while this pointer is non-NULL.
  port::AtomicPointer no_space_;

  // Simulate non-writable file system while this pointer is non-NULL
  port::AtomicPointer non_writable_;

  bool count_random_reads_;
  AtomicCounter random_read_counter_;

  AtomicCounter sleep_counter_;

  explicit SpecialEnv(Env* base) : EnvWrapper(base) {
    delay_sstable_sync_.Release_Store(NULL);
    no_space_.Release_Store(NULL);
    non_writable_.Release_Store(NULL);
    count_random_reads_ = false;
  }

  Status NewWritableFile(const std::string& f, WritableFile** r, size_t map_size) {
    class SSTableFile : public WritableFile {
     private:
      SpecialEnv* env_;
      WritableFile* base_;

     public:
      SSTableFile(SpecialEnv* env, WritableFile* base)
          : env_(env),
            base_(base) {
      }
      ~SSTableFile() { delete base_; }
      Status Append(const Slice& data) {
        if (env_->no_space_.Acquire_Load() != NULL) {
          // Drop writes on the floor
          return Status::OK();
        } else {
          return base_->Append(data);
        }
      }
      Status Close() { return base_->Close(); }
      Status Flush() { return base_->Flush(); }
      Status Sync() {
        while (env_->delay_sstable_sync_.Acquire_Load() != NULL) {
          env_->SleepForMicroseconds(100000);
        }
        return base_->Sync();
      }
    };

    if (non_writable_.Acquire_Load() != NULL) {
      return Status::IOError("simulated write error");
    }

    Status s = target()->NewWritableFile(f, r, 2<<20);
    if (s.ok()) {
      if (strstr(f.c_str(), ".sst") != NULL) {
        *r = new SSTableFile(this, *r);
      }
    }
    return s;
  }

  Status NewRandomAccessFile(const std::string& f, RandomAccessFile** r) {
    class CountingFile : public RandomAccessFile {
     private:
      RandomAccessFile* target_;
      AtomicCounter* counter_;
     public:
      CountingFile(RandomAccessFile* target, AtomicCounter* counter)
          : target_(target), counter_(counter) {
      }
      virtual ~CountingFile() { delete target_; }
      virtual Status Read(uint64_t offset, size_t n, Slice* result,
                          char* scratch) const {
        counter_->Increment();
        return target_->Read(offset, n, result, scratch);
      }
    };

    Status s = target()->NewRandomAccessFile(f, r);
    if (s.ok() && count_random_reads_) {
      *r = new CountingFile(*r, &random_read_counter_);
    }
    return s;
  }

  virtual void SleepForMicroseconds(int micros) {
    sleep_counter_.Increment();
    target()->SleepForMicroseconds(micros);
  }
};

class DBTest {
 private:
  const FilterPolicy* filter_policy_;

  // Sequence of option configurations to try
  enum OptionConfig {
    kDefault,
    kFilter,
    kUncompressed,
    kEnd
  };
  int option_config_;

 public:
  std::string dbname_;
  SpecialEnv* env_;
  DB* db_;

  Options last_options_;

  DBTest() : option_config_(kDefault),
             env_(new SpecialEnv(Env::Default())) {
    filter_policy_ = NewBloomFilterPolicy2(16);
    dbname_ = test::TmpDir() + "/db_test";
    DestroyDB(dbname_, Options());
    db_ = NULL;
    Reopen();
  }

  ~DBTest() {
    delete db_;
    DestroyDB(dbname_, Options());
    delete env_;
    delete filter_policy_;
  }

  // Switch to a fresh database with the next option configuration to
  // test.  Return false if there are no more configurations to test.
  bool ChangeOptions() {
    option_config_++;
    if (option_config_ >= kEnd) {
      return false;
    } else {
      DestroyAndReopen();
      return true;
    }
  }

  // Return the current option configuration.
  Options CurrentOptions() {
    Options options;
    switch (option_config_) {
      case kFilter:
        options.filter_policy = filter_policy_;
        break;
      case kUncompressed:
        options.compression = kNoCompression;
        break;
      default:
        break;
    }
    return options;
  }

  DBImpl* dbfull() {
    return reinterpret_cast<DBImpl*>(db_);
  }

  void Reopen(Options* options = NULL) {
    ASSERT_OK(TryReopen(options));
  }

  void Close() {
    delete db_;
    db_ = NULL;
  }

  void DestroyAndReopen(Options* options = NULL) {
    delete db_;
    db_ = NULL;
    DestroyDB(dbname_, Options());
    ASSERT_OK(TryReopen(options));
  }

  Status TryReopen(Options* options) {
    delete db_;
    db_ = NULL;
    Options opts;
    if (options != NULL) {
      opts = *options;
    } else {
      opts = CurrentOptions();
      opts.create_if_missing = true;
    }
    last_options_ = opts;

    return DB::Open(opts, dbname_, &db_);
  }

  Status DoubleOpen(Options* options = NULL) {
    DB * db_fail;
    delete db_;
    db_ = NULL;
    Options opts, opts2;
    if (options != NULL) {
      opts = *options;
    } else {
      opts = CurrentOptions();
      opts.create_if_missing = true;
    }
    last_options_ = opts;

    DB::Open(opts, dbname_, &db_);
    return DB::Open(opts2, dbname_, &db_fail);
  }

  Status Put(const std::string& k, const std::string& v) {
    return db_->Put(WriteOptions(), k, v);
  }

  Status Delete(const std::string& k) {
    return db_->Delete(WriteOptions(), k);
  }

  std::string Get(const std::string& k, const Snapshot* snapshot = NULL) {
    ReadOptions options;
    options.snapshot = snapshot;
    std::string result;
    Status s = db_->Get(options, k, &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  std::string GetNoCache(const std::string& k, const Snapshot* snapshot = NULL) {
    ReadOptions options;
    options.snapshot = snapshot;
    options.fill_cache=false;
    std::string result;
    Status s = db_->Get(options, k, &result);
    if (s.IsNotFound()) {
      result = "NOT_FOUND";
    } else if (!s.ok()) {
      result = s.ToString();
    }
    return result;
  }

  // Return a string that contains all key,value pairs in order,
  // formatted like "(k1->v1)(k2->v2)".
  std::string Contents() {
    std::vector<std::string> forward;
    std::string result;
    Iterator* iter = db_->NewIterator(ReadOptions());
    for (iter->SeekToFirst(); iter->Valid(); iter->Next()) {
      std::string s = IterStatus(iter);
      result.push_back('(');
      result.append(s);
      result.push_back(')');
      forward.push_back(s);
    }

    // Check reverse iteration results are the reverse of forward results
    int matched = 0;
    for (iter->SeekToLast(); iter->Valid(); iter->Prev()) {
      ASSERT_LT(matched, forward.size());
      ASSERT_EQ(IterStatus(iter), forward[forward.size() - matched - 1]);
      matched++;
    }
    ASSERT_EQ(matched, forward.size());

    delete iter;
    return result;
  }

  std::string AllEntriesFor(const Slice& user_key) {
    Iterator* iter = dbfull()->TEST_NewInternalIterator();
    InternalKey target(user_key, kMaxSequenceNumber, kTypeValue);
    iter->Seek(target.Encode());
    std::string result;
    if (!iter->status().ok()) {
      result = iter->status().ToString();
    } else {
      result = "[ ";
      bool first = true;
      while (iter->Valid()) {
        ParsedInternalKey ikey;
        if (!ParseInternalKey(iter->key(), &ikey)) {
          result += "CORRUPTED";
        } else {
          if (last_options_.comparator->Compare(ikey.user_key, user_key) != 0) {
            break;
          }
          if (!first) {
            result += ", ";
          }
          first = false;
          switch (ikey.type) {
            case kTypeValue:
              result += iter->value().ToString();
              break;
            case kTypeDeletion:
              result += "DEL";
              break;
          }
        }
        iter->Next();
      }
      if (!first) {
        result += " ";
      }
      result += "]";
    }
    delete iter;
    return result;
  }

  int NumTableFilesAtLevel(int level) {
    std::string property;
    ASSERT_TRUE(
        db_->GetProperty("leveldb.num-files-at-level" + NumberToString(level),
                         &property));
    return atoi(property.c_str());
  }

  int TotalTableFiles() {
    int result = 0;
    for (int level = 0; level < config::kNumLevels; level++) {
      result += NumTableFilesAtLevel(level);
    }
    return result;
  }

  // Return spread of files per level
  std::string FilesPerLevel() {
    std::string result;
    int last_non_zero_offset = 0;
    for (int level = 0; level < config::kNumLevels; level++) {
      int f = NumTableFilesAtLevel(level);
      char buf[100];
      snprintf(buf, sizeof(buf), "%s%d", (level ? "," : ""), f);
      result += buf;
      if (f > 0) {
        last_non_zero_offset = result.size();
      }
    }
    result.resize(last_non_zero_offset);
    return result;
  }

  int CountFiles() {
    std::vector<std::string> files;
    env_->GetChildren(dbname_, &files);
    return static_cast<int>(files.size());
  }

  uint64_t Size(const Slice& start, const Slice& limit) {
    Range r(start, limit);
    uint64_t size;
    db_->GetApproximateSizes(&r, 1, &size);
    return size;
  }

  void Compact(const Slice& start, const Slice& limit) {
    db_->CompactRange(&start, &limit);
  }

  // Do n memtable compactions, each of which produces an sstable
  // covering the range [small,large].
  void MakeTables(int n, const std::string& small, const std::string& large) {
    for (int i = 0; i < n; i++) {
      Put(small, "begin");
      Put(large, "end");
      dbfull()->TEST_CompactMemTable();
    }
  }

  // Prevent pushing of new sstables into deeper levels by adding
  // tables that cover a specified range to all levels.
  void FillLevels(const std::string& smallest, const std::string& largest) {
    MakeTables(config::kNumLevels, smallest, largest);
  }

  void DumpFileCounts(const char* label) {
    fprintf(stderr, "---\n%s:\n", label);
    fprintf(stderr, "maxoverlap: %lld\n",
            static_cast<long long>(
                dbfull()->TEST_MaxNextLevelOverlappingBytes()));
    for (int level = 0; level < config::kNumLevels; level++) {
      int num = NumTableFilesAtLevel(level);
      if (num > 0) {
        fprintf(stderr, "  level %3d : %d files\n", level, num);
      }
    }
  }

  std::string DumpSSTableList() {
    std::string property;
    db_->GetProperty("leveldb.sstables", &property);
    return property;
  }

  std::string IterStatus(Iterator* iter) {
    std::string result;
    if (iter->Valid()) {
      result = iter->key().ToString() + "->" + iter->value().ToString();
    } else {
      result = "(invalid)";
    }
    return result;
  }
};

TEST(DBTest, Empty) {
  do {
    ASSERT_TRUE(db_ != NULL);
    ASSERT_EQ("NOT_FOUND", Get("foo"));
  } while (ChangeOptions());
}


#if 0
// riak does not execute compaction due to reads

TEST(DBTest, GetEncountersEmptyLevel) {
  do {
    // Arrange for the following to happen:
    //   * sstable A in level 0
    //   * nothing in level 1
    //   * sstable B in level 2
    // Then do enough Get() calls to arrange for an automatic compaction
    // of sstable A.  A bug would cause the compaction to be marked as
    // occuring at level 1 (instead of the correct level 0).

    // Step 1: First place sstables in levels 0 and 2
    int compaction_count = 0;
    while (NumTableFilesAtLevel(0) == 0 ||
           NumTableFilesAtLevel(2) == 0) {
      ASSERT_LE(compaction_count, 100) << "could not fill levels 0 and 2";
      compaction_count++;
      Put("a", "begin");
      Put("z", "end");
      dbfull()->TEST_CompactMemTable();
    }

    // Step 2: clear level 1 if necessary.
    dbfull()->TEST_CompactRange(1, NULL, NULL);
    ASSERT_EQ(NumTableFilesAtLevel(0), 1);
    ASSERT_EQ(NumTableFilesAtLevel(1), 0);
    ASSERT_EQ(NumTableFilesAtLevel(2), 1);

    // Step 3: read a bunch of times
    for (int i = 0; i < 1000; i++) {
      ASSERT_EQ("NOT_FOUND", Get("missing"));
    }

    // Step 4: Wait for compaction to finish
    env_->SleepForMicroseconds(1000000);

    ASSERT_EQ(NumTableFilesAtLevel(0), 0);
  } while (ChangeOptions());
}
#endif

static std::string Key(int i) {
  char buf[100];
  snprintf(buf, sizeof(buf), "key%06d", i);
  return std::string(buf);
}

static bool Between(uint64_t val, uint64_t low, uint64_t high) {
  bool result = (val >= low) && (val <= high);
  if (!result) {
    fprintf(stderr, "Value %llu is not in range [%llu, %llu]\n",
            (unsigned long long)(val),
            (unsigned long long)(low),
            (unsigned long long)(high));
  }
  return result;
}

// Multi-threaded test:
namespace {

static const int kNumThreads = 4;
static const int kTestSeconds = 10;
static const int kNumKeys = 1000;

struct MTState {
  DBTest* test;
  port::AtomicPointer stop;
  port::AtomicPointer counter[kNumThreads];
  port::AtomicPointer thread_done[kNumThreads];
};

struct MTThread {
  MTState* state;
  int id;
};

static void MTThreadBody(void* arg) {
  MTThread* t = reinterpret_cast<MTThread*>(arg);
  int id = t->id;
  DB* db = t->state->test->db_;
  uintptr_t counter = 0;
  fprintf(stderr, "... starting thread %d\n", id);
  Random rnd(1000 + id);
  std::string value;
  char valbuf[1500];
  while (t->state->stop.Acquire_Load() == NULL) {
    t->state->counter[id].Release_Store(reinterpret_cast<void*>(counter));

    int key = rnd.Uniform(kNumKeys);
    char keybuf[20];
    snprintf(keybuf, sizeof(keybuf), "%016d", key);

    if (rnd.OneIn(2)) {
      // Write values of the form <key, my id, counter>.
      // We add some padding for force compactions.
      snprintf(valbuf, sizeof(valbuf), "%d.%d.%-1000d",
               key, id, static_cast<int>(counter));
      ASSERT_OK(db->Put(WriteOptions(), Slice(keybuf), Slice(valbuf)));
    } else {
      // Read a value and verify that it matches the pattern written above.
      Status s = db->Get(ReadOptions(), Slice(keybuf), &value);
      if (s.IsNotFound()) {
        // Key has not yet been written
      } else {
        // Check that the writer thread counter is >= the counter in the value
        ASSERT_OK(s);
        int k, w, c;
        ASSERT_EQ(3, sscanf(value.c_str(), "%d.%d.%d", &k, &w, &c)) << value;
        ASSERT_EQ(k, key);
        ASSERT_GE(w, 0);
        ASSERT_LT(w, kNumThreads);
        ASSERT_LE(c, reinterpret_cast<uintptr_t>(
            t->state->counter[w].Acquire_Load()));
      }
    }
    counter++;
  }
  t->state->thread_done[id].Release_Store(t);
  fprintf(stderr, "... stopping thread %d after %d ops\n", id, int(counter));
}

}  // namespace


namespace {
typedef std::map<std::string, std::string> KVMap;
}

class ModelDB: public DB {
 public:
  class ModelSnapshot : public Snapshot {
   public:
    KVMap map_;
  };

  explicit ModelDB(const Options& options): options_(options) { }
  ~ModelDB() { }
  virtual Status Put(const WriteOptions& o, const Slice& k, const Slice& v) {
    return DB::Put(o, k, v);
  }
  virtual Status Delete(const WriteOptions& o, const Slice& key) {
    return DB::Delete(o, key);
  }
  virtual Status Get(const ReadOptions& options,
                     const Slice& key, std::string* value) {
    assert(false);      // Not implemented
    return Status::NotFound(key);
  }
  virtual Status Get(const ReadOptions& options,
                     const Slice& key, Value* value) {
    assert(false);      // Not implemented
    return Status::NotFound(key);
  }
  virtual Iterator* NewIterator(const ReadOptions& options) {
    if (options.snapshot == NULL) {
      KVMap* saved = new KVMap;
      *saved = map_;
      return new ModelIter(saved, true);
    } else {
      const KVMap* snapshot_state =
          &(reinterpret_cast<const ModelSnapshot*>(options.snapshot)->map_);
      return new ModelIter(snapshot_state, false);
    }
  }
  virtual const Snapshot* GetSnapshot() {
    ModelSnapshot* snapshot = new ModelSnapshot;
    snapshot->map_ = map_;
    return snapshot;
  }

  virtual void ReleaseSnapshot(const Snapshot* snapshot) {
    delete reinterpret_cast<const ModelSnapshot*>(snapshot);
  }
  virtual Status Write(const WriteOptions& options, WriteBatch* batch) {
    class Handler : public WriteBatch::Handler {
     public:
      KVMap* map_;
      virtual void Put(const Slice& key, const Slice& value) {
        (*map_)[key.ToString()] = value.ToString();
      }
      virtual void Delete(const Slice& key) {
        map_->erase(key.ToString());
      }
    };
    Handler handler;
    handler.map_ = &map_;
    return batch->Iterate(&handler);
  }

  virtual bool GetProperty(const Slice& property, std::string* value) {
    return false;
  }
  virtual void GetApproximateSizes(const Range* r, int n, uint64_t* sizes) {
    for (int i = 0; i < n; i++) {
      sizes[i] = 0;
    }
  }
  virtual void CompactRange(const Slice* start, const Slice* end) {
  }

 private:
  class ModelIter: public Iterator {
   public:
    ModelIter(const KVMap* map, bool owned)
        : map_(map), owned_(owned), iter_(map_->end()) {
    }
    ~ModelIter() {
      if (owned_) delete map_;
    }
    virtual bool Valid() const { return iter_ != map_->end(); }
    virtual void SeekToFirst() { iter_ = map_->begin(); }
    virtual void SeekToLast() {
      if (map_->empty()) {
        iter_ = map_->end();
      } else {
        iter_ = map_->find(map_->rbegin()->first);
      }
    }
    virtual void Seek(const Slice& k) {
      iter_ = map_->lower_bound(k.ToString());
    }
    virtual void Next() { ++iter_; }
    virtual void Prev() { --iter_; }
    virtual Slice key() const { return iter_->first; }
    virtual Slice value() const { return iter_->second; }
    virtual Status status() const { return Status::OK(); }
   private:
    const KVMap* const map_;
    const bool owned_;  // Do we own map_
    KVMap::const_iterator iter_;
  };
  const Options options_;
  KVMap map_;
};

static std::string RandomKey(Random* rnd) {
  int len = (rnd->OneIn(3)
             ? 1                // Short sometimes to encourage collisions
             : (rnd->OneIn(100) ? rnd->Skewed(10) : rnd->Uniform(10)));
  return test::RandomKey(rnd, len);
}

static bool CompareIterators(int step,
                             DB* model,
                             DB* db,
                             const Snapshot* model_snap,
                             const Snapshot* db_snap) {
  ReadOptions options;
  options.snapshot = model_snap;
  Iterator* miter = model->NewIterator(options);
  options.snapshot = db_snap;
  Iterator* dbiter = db->NewIterator(options);
  bool ok = true;
  int count = 0;
  for (miter->SeekToFirst(), dbiter->SeekToFirst();
       ok && miter->Valid() && dbiter->Valid();
       miter->Next(), dbiter->Next()) {
    count++;
    if (miter->key().compare(dbiter->key()) != 0) {
      fprintf(stderr, "step %d: Key mismatch: '%s' vs. '%s'\n",
              step,
              EscapeString(miter->key()).c_str(),
              EscapeString(dbiter->key()).c_str());
      ok = false;
      break;
    }

    if (miter->value().compare(dbiter->value()) != 0) {
      fprintf(stderr, "step %d: Value mismatch for key '%s': '%s' vs. '%s'\n",
              step,
              EscapeString(miter->key()).c_str(),
              EscapeString(miter->value()).c_str(),
              EscapeString(miter->value()).c_str());
      ok = false;
    }
  }

  if (ok) {
    if (miter->Valid() != dbiter->Valid()) {
      fprintf(stderr, "step %d: Mismatch at end of iterators: %d vs. %d\n",
              step, miter->Valid(), dbiter->Valid());
      ok = false;
    }
  }
  fprintf(stderr, "%d entries compared: ok=%d\n", count, ok);
  delete miter;
  delete dbiter;
  return ok;
}

std::string MakeKey(unsigned int num) {
  char buf[30];
  snprintf(buf, sizeof(buf), "%016u", num);
  return std::string(buf);
}

void BM_LogAndApply(int iters, int num_base_files) {
  std::string dbname = test::TmpDir() + "/leveldb_test_benchmark";
  DestroyDB(dbname, Options());

  DB* db = NULL;
  Options opts;
  opts.create_if_missing = true;
  Status s = DB::Open(opts, dbname, &db);
  ASSERT_OK(s);
  ASSERT_TRUE(db != NULL);

  delete db;
  db = NULL;

  Env* env = Env::Default();

  port::Mutex mu;
  MutexLock l(&mu);

  InternalKeyComparator cmp(BytewiseComparator());
  Options options;
  VersionSet vset(dbname, &options, NULL, &cmp);
  ASSERT_OK(vset.Recover());
  VersionEdit vbase;
  uint64_t fnum = 1;
  for (int i = 0; i < num_base_files; i++) {
    InternalKey start(MakeKey(2*fnum), 1, kTypeValue);
    InternalKey limit(MakeKey(2*fnum+1), 1, kTypeDeletion);
    vbase.AddFile(2, fnum++, 1 /* file size */, start, limit);
  }
  ASSERT_OK(vset.LogAndApply(&vbase, &mu));

  uint64_t start_micros = env->NowMicros();

  for (int i = 0; i < iters; i++) {
    VersionEdit vedit;
    vedit.DeleteFile(2, fnum);
    InternalKey start(MakeKey(2*fnum), 1, kTypeValue);
    InternalKey limit(MakeKey(2*fnum+1), 1, kTypeDeletion);
    vedit.AddFile(2, fnum++, 1 /* file size */, start, limit);
    vset.LogAndApply(&vedit, &mu);
  }
  uint64_t stop_micros = env->NowMicros();
  unsigned int us = stop_micros - start_micros;
  char buf[16];
  snprintf(buf, sizeof(buf), "%d", num_base_files);
  fprintf(stderr,
          "BM_LogAndApply/%-6s   %8d iters : %9u us (%7.0f us / iter)\n",
          buf, iters, us, ((float)us) / iters);
}

TEST(DBTest, GetFromVersions) {
  do {
    ASSERT_OK(Put("foo", "v1"));
    dbfull()->TEST_CompactMemTable();
    ASSERT_EQ("v1", Get("foo"));
  } while (ChangeOptions());
}

#if 0
TEST(LogTest, ReadWrite) {
  Write("foo");
  Write("bar");
  Write("");
  Write("xxxx");
  ASSERT_EQ("foo", Read());
  ASSERT_EQ("bar", Read());
  ASSERT_EQ("", Read());
  ASSERT_EQ("xxxx", Read());
  ASSERT_EQ("EOF", Read());
  ASSERT_EQ("EOF", Read());  // Make sure reads at eof work
}
#endif

}  // namespace leveldb

int main(int argc, char** argv) {
  if (argc > 1 && std::string(argv[1]) == "--benchmark") {
    leveldb::BM_LogAndApply(1000, 1);
    leveldb::BM_LogAndApply(1000, 100);
    leveldb::BM_LogAndApply(1000, 10000);
    leveldb::BM_LogAndApply(100, 100000);
    return 0;
  }

  return leveldb::test::RunAllTests();
}
