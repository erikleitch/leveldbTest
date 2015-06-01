// Copyright (c) 2015 The LevelDB Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file. See the AUTHORS file for names of contributors.

#ifndef LEVELDB_FILESTATS_H
#define LEVELDB_FILESTATS_H

/**
 * @file FileStats.h
 * 
 * Tagged: Fri May 29 17:08:14 PDT 2015
 * 
 * @version: $Revision: $, $Date: $
 * 
 * @author eleitch@basho.com
 */
namespace leveldb {

  class FileStats {
  public:

    /**
     * Constructor.
     */
    FileStats();

    /**
     * Destructor.
     */
    virtual ~FileStats();

  private:
  }; // End class FileStats

} // End namespace leveldb


#endif // End #ifndef LEVELDB_FILESTATS_H
