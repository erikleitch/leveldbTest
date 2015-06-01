#include "leveldb/util/ExceptionUtils.h"
#include "leveldb/util/StatManager.h"

using namespace std;

using namespace leveldb::util;

/**.......................................................................
 * Constructors
 */
StatManager::StatManager() : strobeIntervalSec_(60), queueLen_(1) {};
StatManager::StatManager() : strobeIntervalSec_(strobeIntervalSec), queueLen_(queueLen) {};

/**.......................................................................
 * Destructor.
 */
StatManager::~StatManager() {}

void StatManager::lock() 
{
  mutex_.lock();
}

void StatManager::unlock() 
{
  mutex_.unlock();
}
