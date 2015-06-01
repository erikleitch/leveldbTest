// Copyright (c) 2015 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef LEVELDB_TIMER_H
#define LEVELDB_TIMER_H

/**
 * @file Timer.h
 * 
 * Tagged: Fri May 29 15:40:04 PDT 2015
 * 
 * @version: $Revision: $, $Date: $
 * 
 * @author eleitch@basho.com
 */
#include <stdint.h>

namespace leveldb {

  class Timer {
  public:

    /**
     * Constructor.
     */
    Timer();

    /**
     * Destructor.
     */
    virtual ~Timer();

    void start();
    void stop();
    uint64_t deltaMicroSeconds();

    uint64_t currentMs();

  private:

    uint64_t startMs_;
    uint64_t stopMs_;
    uint64_t diffMs_;
    uint64_t integratedMs_;

  }; // End class Timer

} // End namespace leveldb


#endif // End #ifndef LEVELDB_TIMER_H
