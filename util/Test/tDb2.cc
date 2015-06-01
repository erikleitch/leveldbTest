#include <iostream>
#include <iomanip>

#include <cmath>

#include "gcp/program/Program.h"
#include "gcp/util/Exception.h"
#include "gcp/util/Timer.h"
#include "gcp/util/LevelDbTest.h"

#include "leveldb/db.h"
#include "leveldb/filter_policy.h"

using namespace gcp::util;
using namespace gcp::program;

KeyTabEntry Program::keywords[] = {
  { "n",        "10000", "i", "Iterations of gets"},
  { END_OF_KEYWORDS}
};

void Program::initializeUsage() {};

using namespace leveldb;

class TestDb {
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
  DB* db_;

  Options last_options_;

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
  
  Status TryReopen(Options* options=NULL) {
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
  };

  void Close() {
    delete db_;
    db_ = NULL;
  };

};

int Program::main()
{
  TestDb db;
  db.dbname_ = "/tmp/testdb";
  db.TryReopen();
  db.Close();
  return 0;
}
