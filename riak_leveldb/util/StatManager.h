#ifndef LEVELDB_UTIL_STATMANAGER_H
#define LEVELDB_UTIL_STATMANAGER_H

/**
 * @file StatManager.h
 * 
 * Tagged: Sun May 31 20:16:32 PDT 2015
 * 
 * @version: $Revision: $, $Date: $
 * 
 * @author /bin/bash: username: command not found
 */
#include "port/port.h"

namespace leveldb {
  namespace util {

    class StatManager {
    public:

      //============================================================
      // An object that will manage stats for a single counter
      //============================================================

      struct Counter {
      public:

	Counter(unsigned queueLen);
	void add(uint64_t val);
	void storeDifferential();

	uint64_t integratedCurr_;
	uint64_t integratedLast_;

	std::vector<uint64_t> differentials_;
	std::vector<uint64_t> sampleTime_;

	unsigned headIndex_:
	unsigned nInQueue_;
      };

      /**
       * Constructor.
       */
      StatManager();
      StatManager(unsigned strobeIntervalSec, unsigned queueLen);

      /**
       * Destructor.
       */
      virtual ~StatManager();

      //------------------------------------------------------------
      // Add to the counters maintained by this object
      //------------------------------------------------------------

      void add(std::map<std::string, uint64_t>& counterMap);

      //------------------------------------------------------------
      // Store differential counts
      //------------------------------------------------------------

      void strobe();

      //------------------------------------------------------------
      // Retrieve a map of counter samples from this object
      //------------------------------------------------------------

      void getCounts(std::map<std::string, std::vector<std::pair<uint64_t, uint64_t> >& samples);

    private:

      void lock();
      void unlock();

      std::map<std::string, Counter> counterMap_;

      leveldb::port::Mutex mutex_;
      pthread_
    }; // End class StatManager

  } // End namespace util
} // End namespace leveldb



#endif // End #ifndef LEVELDB_UTIL_STATMANAGER_H
