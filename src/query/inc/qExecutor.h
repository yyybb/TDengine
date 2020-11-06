/*
 * Copyright (c) 2019 TAOS Data, Inc. <jhtao@taosdata.com>
 *
 * This program is free software: you can use, redistribute, and/or modify
 * it under the terms of the GNU Affero General Public License, version 3
 * or later ("AGPL"), as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.
 *
 * You should have received a copy of the GNU Affero General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */
#ifndef TDENGINE_QUERYEXECUTOR_H
#define TDENGINE_QUERYEXECUTOR_H

#include "os.h"

#include "hash.h"
#include "qFill.h"
#include "qResultbuf.h"
#include "qSqlparser.h"
#include "qTsbuf.h"
#include "query.h"
#include "taosdef.h"
#include "tarray.h"
#include "tlockfree.h"
#include "tsdb.h"
#include "tsqlfunction.h"

struct SColumnFilterElem;
typedef bool (*__filter_func_t)(struct SColumnFilterElem* pFilter, char* val1, char* val2);
typedef int32_t (*__block_search_fn_t)(char* data, int32_t num, int64_t key, int32_t order);

typedef struct SPosInfo {
  int32_t pageId:20;
  int32_t rowId:12;
} SPosInfo;

typedef struct SGroupResInfo {
  int32_t  groupId;
  int32_t  numOfDataPages;
  SPosInfo pos;
} SGroupResInfo;

typedef struct SSqlGroupbyExpr {
  int16_t tableIndex;
  SArray* columnInfo;  // SArray<SColIndex>, group by columns information
  int16_t numOfGroupCols;
  int16_t orderIndex;  // order by column index
  int16_t orderType;   // order by type: asc/desc
} SSqlGroupbyExpr;

typedef struct SWindowResult {
  SPosInfo      pos;         // Position of current result in disk-based output buffer
  uint16_t      numOfRows;   // number of rows of current time window
  bool          closed;      // this result status: closed or opened
  SResultInfo*  resultInfo;  // For each result column, there is a resultInfo
  union {STimeWindow win; char* key;};  // start key of current time window
} SWindowResult;

/**
 * If the number of generated results is greater than this value,
 * query query will be halt and return results to client immediate.
 */
typedef struct SResultRec {
  int64_t total;      // total generated result size in rows
  int64_t rows;       // current result set size in rows
  int64_t capacity;   // capacity of current result output buffer
  int32_t threshold;  // result size threshold in rows.
} SResultRec;

typedef struct SWindowResInfo {
  SWindowResult* pResult;    // result list
  SHashObj*      hashList;   // hash list for quick access
  int16_t        type;       // data type for hash key
  int32_t        capacity;   // max capacity
  int32_t        curIndex;   // current start active index
  int32_t        size;       // number of result set
  int64_t        startTime;  // start time of the first time window for sliding query
  int64_t        prevSKey;   // previous (not completed) sliding window start key
  int64_t        threshold;  // threshold to halt query and return the generated results.
  int64_t        interval;   // time window interval
} SWindowResInfo;

typedef struct SColumnFilterElem {
  int16_t           bytes;  // column length
  __filter_func_t   fp;
  SColumnFilterInfo filterInfo;
} SColumnFilterElem;

typedef struct SSingleColumnFilterInfo {
  void*              pData;
  int32_t            numOfFilters;
  SColumnInfo        info;
  SColumnFilterElem* pFilters;
} SSingleColumnFilterInfo;

typedef struct STableQueryInfo {  // todo merge with the STableQueryInfo struct
  TSKEY       lastKey;
  int32_t     groupIndex;     // group id in table list
  int16_t     queryRangeSet;  // denote if the query range is set, only available for interval query
  tVariant    tag;
  STimeWindow win;
  STSCursor   cur;
  void*       pTable;         // for retrieve the page id list
  SWindowResInfo windowResInfo;
} STableQueryInfo;

typedef struct SQueryCostInfo {
  uint64_t loadStatisTime;
  uint64_t loadFileBlockTime;
  uint64_t loadDataInCacheTime;
  uint64_t loadStatisSize;
  uint64_t loadFileBlockSize;
  uint64_t loadDataInCacheSize;
  
  uint64_t loadDataTime;
  uint64_t totalRows;
  uint64_t totalCheckedRows;
  uint32_t totalBlocks;
  uint32_t loadBlocks;
  uint32_t loadBlockStatis;
  uint32_t discardBlocks;
  uint64_t elapsedTime;
  uint64_t firstStageMergeTime;
  uint64_t internalSupSize;
  uint64_t numOfTimeWindows;
} SQueryCostInfo;

typedef struct SQuery {
  int16_t          numOfCols;
  int16_t          numOfTags;
  SOrderVal        order;
  STimeWindow      window;
  SInterval        interval;
  int16_t          precision;
  int16_t          numOfOutput;
  int16_t          fillType;
  int16_t          checkBuffer;  // check if the buffer is full during scan each block
  SLimitVal        limit;
  int32_t          rowSize;
  SSqlGroupbyExpr* pGroupbyExpr;
  SExprInfo*       pSelectExpr;
  SColumnInfo*     colList;
  SColumnInfo*     tagColList;
  int32_t          numOfFilterCols;
  int64_t*         fillVal;
  uint32_t         status;  // query status
  SResultRec       rec;
  int32_t          pos;
  tFilePage**      sdata;
  STableQueryInfo* current;

  SSingleColumnFilterInfo* pFilterInfo;
} SQuery;

typedef struct SQueryRuntimeEnv {
  jmp_buf              env;
  SResultInfo*         resultInfo;       // todo refactor to merge with SWindowResInfo
  SQuery*              pQuery;
  SQLFunctionCtx*      pCtx;
  int32_t              numOfRowsPerPage;
  int16_t              offset[TSDB_MAX_COLUMNS];
  uint16_t             scanFlag;         // denotes reversed scan of data or not
  SFillInfo*           pFillInfo;
  SWindowResInfo       windowResInfo;
  STSBuf*              pTSBuf;
  STSCursor            cur;
  SQueryCostInfo       summary;
  void*                pQueryHandle;
  void*                pSecQueryHandle;  // another thread for
  bool                 stableQuery;      // super table query or not
  bool                 topBotQuery;      // false
  bool                 groupbyNormalCol; // denote if this is a groupby normal column query
  bool                 hasTagResults;    // if there are tag values in final result or not
  int32_t              interBufSize;     // intermediate buffer sizse
  int32_t              prevGroupId;      // previous executed group id
  SDiskbasedResultBuf* pResultBuf;       // query result buffer based on blocked-wised disk file
} SQueryRuntimeEnv;

enum {
  QUERY_RESULT_NOT_READY = 1,
  QUERY_RESULT_READY     = 2,
};

typedef struct SMemRef {
  int32_t ref; 
  void *mem;
  void *imem;
} SMemRef;

typedef struct SQInfo {
  void*            signature;
  int32_t          code;   // error code to returned to client
  int64_t          owner; // if it is in execution
  void*            tsdb;
  SMemRef          memRef; 
  int32_t          vgId;
  STableGroupInfo  tableGroupInfo;       // table <tid, last_key> list  SArray<STableKeyInfo>
  STableGroupInfo  tableqinfoGroupInfo;  // this is a group array list, including SArray<STableQueryInfo*> structure
  SQueryRuntimeEnv runtimeEnv;
  SArray*          arrTableIdInfo;
  int32_t          groupIndex;

  /*
   * the query is executed position on which meter of the whole list.
   * when the index reaches the last one of the list, it means the query is completed.
   */
  int32_t          tableIndex;
  SGroupResInfo    groupResInfo;
  void*            pBuf;        // allocated buffer for STableQueryInfo, sizeof(STableQueryInfo)*numOfTables;

  pthread_mutex_t  lock;        // used to synchronize the rsp/query threads
  tsem_t           ready;
  int32_t          dataReady;   // denote if query result is ready or not
  void*            rspContext;  // response context
} SQInfo;

#endif  // TDENGINE_QUERYEXECUTOR_H
