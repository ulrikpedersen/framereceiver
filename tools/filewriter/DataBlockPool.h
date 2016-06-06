/*
 * DataBlockPool.h
 *
 *  Created on: 24 May 2016
 *      Author: gnx91527
 */

#ifndef TOOLS_FILEWRITER_DATABLOCKPOOL_H_
#define TOOLS_FILEWRITER_DATABLOCKPOOL_H_

#include <boost/shared_ptr.hpp>
#include <list>

#include "DataBlock.h"

namespace filewriter
{

  /**
   * The DataBlock and DataBlockPool classes provide memory management for
   * data within Frames.  Memory is allocated by a data block on construction,
   * and then the data block can be re-used without continually freeing and re-
   * allocating the memory.
   * The DataBlockPool provides a singleton class that can be used to access
   * data blocks through shared memory pointers and manages the data blocks to
   * avoid continuous allocating and freeing of memory.  The DataBlockPool also
   * contains details of how many blocks are available, in use and the total
   * memory used.
   */
  class DataBlockPool
  {
  public:
    virtual ~DataBlockPool();

    static void allocate(size_t nBlocks, size_t nBytes);
    static boost::shared_ptr<DataBlock> take(size_t nBytes);
    static void release(boost::shared_ptr<DataBlock> block);
    static size_t getFreeBlocks();
    static size_t getUsedBlocks();
    static size_t getTotalBlocks();
    static size_t getMemoryAllocated();

  private:
    static DataBlockPool *instance();
    DataBlockPool();
    void internalAllocate(size_t nBlocks, size_t nBytes);
    boost::shared_ptr<DataBlock> internalTake(size_t nBytes);
    void internalRelease(boost::shared_ptr<DataBlock> block);
    size_t internalGetFreeBlocks();
    size_t internalGetUsedBlocks();
    size_t internalGetTotalBlocks();
    size_t internalGetMemoryAllocated();

    std::list<boost::shared_ptr<DataBlock> > freeList_;
    std::map<int, boost::shared_ptr<DataBlock> > usedMap_;
    size_t freeBlocks_;
    size_t usedBlocks_;
    size_t totalBlocks_;
    size_t memoryAllocated_;

    static DataBlockPool *instance_;

  };

} /* namespace filewriter */

#endif /* TOOLS_FILEWRITER_DATABLOCKPOOL_H_ */