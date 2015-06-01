#include <iostream>
#include <iomanip>

#include <cmath>

#include "gcp/program/Program.h"
#include "gcp/util/Exception.h"
#include "gcp/util/Timer.h"
#include "gcp/util/LevelDbTest.h"

#include "leveldb/db.h"

#include <sys/shm.h>

using namespace gcp::util;
using namespace gcp::program;

KeyTabEntry Program::keywords[] = {
  { "n",        "10000", "i", "Iterations of gets"},
  { END_OF_KEYWORDS}
};

void Program::initializeUsage() {};

int Program::main()
{
  struct shmid_ds info;

  if(shmctl(41207, IPC_RMID, &info) < 0)
    ReportSysError("shmctl");

  return 0;
}
