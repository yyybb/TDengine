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

#ifndef TDENGINE_TSCLIENT_H
#define TDENGINE_TSCLIENT_H

#ifdef __cplusplus
extern "C" {
#endif

#include "os.h"

#include "taos.h"
#include "taosdef.h"
#include "taosmsg.h"
#include "tarray.h"
#include "tglobal.h"
#include "tsqlfunction.h"
#include "tutil.h"
#include "tcache.h"

#include "qExecutor.h"
#include "qSqlparser.h"
#include "qTsbuf.h"
#include "tcmdtype.h"

#if 0
static UNUSED_FUNC void *u_malloc (size_t __size) {
  uint32_t v = rand();

  if (v % 5000 <= 0) {
    return NULL;
  } else {
    return malloc(__size);
  }
}

static UNUSED_FUNC void* u_calloc(size_t num, size_t __size) {
  uint32_t v = rand();
  if (v % 5000 <= 0) {
    return NULL;
  } else {
    return calloc(num, __size);
  }
}

static UNUSED_FUNC void* u_realloc(void* p, size_t __size) {
  uint32_t v = rand();
  if (v % 5000 <= 0) {
    return NULL;
  } else {
    return realloc(p, __size);
  }
}

#define calloc  u_calloc
#define malloc  u_malloc
#define realloc u_realloc
#endif

// forward declaration
struct SSqlInfo;
struct SLocalReducer;

// data source from sql string or from file
enum {
  DATA_FROM_SQL_STRING = 1,
  DATA_FROM_DATA_FILE = 2,
};

typedef void (*__async_cb_func_t)(void *param, TAOS_RES *tres, int32_t numOfRows);

typedef struct STableComInfo {
  uint8_t numOfTags;
  uint8_t precision;
  int16_t numOfColumns;
  int32_t rowSize;
} STableComInfo;

typedef struct SCMCorVgroupInfo {
  int32_t    version;
  int8_t     inUse;
  int8_t     numOfEps;
  SEpAddr1   epAddr[TSDB_MAX_REPLICA];
} SCMCorVgroupInfo;

typedef struct STableMeta {
  STableComInfo  tableInfo;
  uint8_t        tableType;
  int16_t        sversion;
  int16_t        tversion;
  char           sTableId[TSDB_TABLE_FNAME_LEN];
  SCMVgroupInfo  vgroupInfo;
  SCMCorVgroupInfo  corVgroupInfo;
  STableId       id;
  SSchema        schema[];  // if the table is TSDB_CHILD_TABLE, schema is acquired by super table meta info
} STableMeta;

typedef struct STableMetaInfo {
  STableMeta   *pTableMeta;      // table meta, cached in client side and acquired by name
  SVgroupsInfo *vgroupList;
  SArray       *pVgroupTables;   // SArray<SVgroupTableInfo>
  
  /*
   * 1. keep the vgroup index during the multi-vnode super table projection query
   * 2. keep the vgroup index for multi-vnode insertion
   */
  int32_t vgroupIndex;
  char    name[TSDB_TABLE_FNAME_LEN];        // (super) table name
  char    aliasName[TSDB_TABLE_NAME_LEN];    // alias name of table specified in query sql
  SArray* tagColList;                        // SArray<SColumn*>, involved tag columns
} STableMetaInfo;

/* the structure for sql function in select clause */
typedef struct SSqlExpr {
  char      aliasName[TSDB_COL_NAME_LEN];  // as aliasName
  SColIndex colInfo;
  int64_t   uid;            // refactor use the pointer
  int16_t   functionId;     // function id in aAgg array
  int16_t   resType;        // return value type
  int16_t   resBytes;       // length of return value
  int32_t   interBytes;     // inter result buffer size
  int16_t   numOfParams;    // argument value of each function
  tVariant  param[3];       // parameters are not more than 3
  int32_t   offset;         // sub result column value of arithmetic expression.
} SSqlExpr;

typedef struct SColumnIndex {
  int16_t tableIndex;
  int16_t columnIndex;
} SColumnIndex;

typedef struct SInternalField {
  TAOS_FIELD      field;
  bool            visible;
  SExprInfo      *pArithExprInfo;
  SSqlExpr       *pSqlExpr;
} SInternalField;

typedef struct SFieldInfo {
  int16_t      numOfOutput;   // number of column in result
  TAOS_FIELD*  final;
  SArray      *internalField; // SArray<SInternalField>
} SFieldInfo;

typedef struct SColumn {
  SColumnIndex       colIndex;
  int32_t            numOfFilters;
  SColumnFilterInfo *filterInfo;
} SColumn;

typedef struct SCond {
  uint64_t uid;
  int32_t  len;  // length of tag query condition data
  char *   cond;
} SCond;

typedef struct SJoinNode {
  char     tableId[TSDB_TABLE_FNAME_LEN];
  uint64_t uid;
  int16_t  tagColId;
} SJoinNode;

typedef struct SJoinInfo {
  bool      hasJoin;
  SJoinNode left;
  SJoinNode right;
} SJoinInfo;

typedef struct STagCond {
  // relation between tbname list and query condition, including : TK_AND or TK_OR
  int16_t relType;

  // tbname query condition, only support tbname query condition on one table
  SCond tbnameCond;

  // join condition, only support two tables join currently
  SJoinInfo joinInfo;

  // for different table, the query condition must be seperated
  SArray *pCond;
} STagCond;

typedef struct SParamInfo {
  int32_t  idx;
  char     type;
  uint8_t  timePrec;
  int16_t  bytes;
  uint32_t offset;
} SParamInfo;

typedef struct STableDataBlocks {
  char     tableId[TSDB_TABLE_FNAME_LEN];
  int8_t   tsSource;     // where does the UNIX timestamp come from, server or client
  bool     ordered;      // if current rows are ordered or not
  int64_t  vgId;         // virtual group id
  int64_t  prevTS;       // previous timestamp, recorded to decide if the records array is ts ascending
  int32_t  numOfTables;  // number of tables in current submit block
  int32_t  rowSize;      // row size for current table
  uint32_t nAllocSize;
  uint32_t headerSize;   // header for table info (uid, tid, submit metadata)
  uint32_t size;

  /*
   * the table meta of table, the table meta will be used during submit, keep a ref
   * to avoid it to be removed from cache
   */
  STableMeta *pTableMeta;
  char       *pData;

  // for parameter ('?') binding
  uint32_t    numOfAllocedParams;
  uint32_t    numOfParams;
  SParamInfo *params;
} STableDataBlocks;

typedef struct SQueryInfo {
  int16_t          command;       // the command may be different for each subclause, so keep it seperately.
  uint32_t         type;          // query/insert type

  STimeWindow      window;        // query time window
  SInterval        interval;

  SSqlGroupbyExpr  groupbyExpr;   // group by tags info
  SArray *         colList;       // SArray<SColumn*>
  SFieldInfo       fieldsInfo;
  SArray *         exprList;      // SArray<SSqlExpr*>
  SLimitVal        limit;
  SLimitVal        slimit;
  STagCond         tagCond;
  SOrderVal        order;
  int16_t          fillType;      // final result fill type
  int16_t          numOfTables;
  STableMetaInfo **pTableMetaInfo;
  struct STSBuf *  tsBuf;
  int64_t *        fillVal;       // default value for fill
  char *           msg;           // pointer to the pCmd->payload to keep error message temporarily
  int64_t          clauseLimit;   // limit for current sub clause
  int64_t          prjOffset;     // offset value in the original sql expression, only applied at client side
  int32_t          udColumnId;    // current user-defined constant output field column id, monotonically decreases from TSDB_UD_COLUMN_INDEX
} SQueryInfo;

typedef struct {
  int     command;
  uint8_t msgType;
  bool    autoCreated;        // create table if it is not existed during retrieve table meta in mnode

  union {
    int32_t count;
    int32_t numOfTablesInSubmit;
  };

  int32_t      insertType;
  int32_t      clauseIndex;  // index of multiple subclause query

  char *       curSql;       // current sql, resume position of sql after parsing paused
  int8_t       parseFinished;

  int16_t      numOfCols;
  uint32_t     allocSize;
  char *       payload;
  int32_t      payloadLen;
  SQueryInfo **pQueryInfo;
  int32_t      numOfClause;
  int32_t      batchSize;    // for parameter ('?') binding and batch processing
  int32_t      numOfParams;

  int8_t       dataSourceType;     // load data from file or not
  int8_t       submitSchema; // submit block is built with table schema
  STagData     tagData;
  SHashObj    *pTableList;   // referred table involved in sql
  SArray      *pDataBlocks;  // SArray<STableDataBlocks*> submit data blocks after parsing sql
} SSqlCmd;

typedef struct SResRec {
  int numOfRows;
  int numOfTotal;
} SResRec;

typedef struct {
  int64_t               numOfRows;                  // num of results in current retrieved
  int64_t               numOfRowsGroup;             // num of results of current group
  int64_t               numOfTotal;                 // num of total results
  int64_t               numOfClauseTotal;           // num of total result in current subclause
  char *                pRsp;
  int32_t               rspType;
  int32_t               rspLen;
  uint64_t              qhandle;
  int64_t               uid;
  int64_t               useconds;
  int64_t               offset;  // offset value from vnode during projection query of stable
  int32_t               row;
  int16_t               numOfCols;
  int16_t               precision;
  bool                  completed;
  int32_t               code;
  int32_t               numOfGroups;
  SResRec *             pGroupRec;
  char *                data;
  TAOS_ROW              tsrow;
  int32_t*              length;  // length for each field for current row
  char **               buffer;  // Buffer used to put multibytes encoded using unicode (wchar_t)
  SColumnIndex *        pColumnIndex;
  SArithmeticSupport*   pArithSup;   // support the arithmetic expression calculation on agg functions
  
  struct SLocalReducer *pLocalReducer;
} SSqlRes;

typedef struct STscObj {
  void *             signature;
  void *             pTimer;
  char               user[TSDB_USER_LEN];
  char               pass[TSDB_KEY_LEN];
  char               acctId[TSDB_ACCT_LEN];
  char               db[TSDB_ACCT_LEN + TSDB_DB_NAME_LEN];
  char               sversion[TSDB_VERSION_LEN];
  char               writeAuth : 1;
  char               superAuth : 1;
  uint32_t           connId;
  struct SSqlObj *   pHb;
  struct SSqlObj *   sqlList;
  struct SSqlStream *streamList;
  void*              pDnodeConn;
  pthread_mutex_t    mutex;
  T_REF_DECLARE()
} STscObj;

typedef struct SSubqueryState {
  int32_t          numOfRemain;         // the number of remain unfinished subquery
  int32_t          numOfSub;            // the number of total sub-queries
  uint64_t         numOfRetrievedRows;  // total number of points in this query
} SSubqueryState;

typedef struct SSqlObj {
  void            *signature;
  pthread_t        owner;        // owner of sql object, by which it is executed
  STscObj         *pTscObj;
  void            *pRpcCtx;
  void            (*fp)();
  void            (*fetchFp)();
  void            *param;
  int64_t          stime;
  uint32_t         queryId;
  void *           pStream;
  void *           pSubscription;
  char *           sqlstr;
  char             parseRetry;
  char             retry;
  char             maxRetry;
  SRpcEpSet        epSet;
  char             listed;
  tsem_t           rspSem;
  SSqlCmd          cmd;
  SSqlRes          res;

  SSubqueryState   subState;
  struct SSqlObj **pSubs;

  struct SSqlObj  *prev, *next;
  struct SSqlObj **self;
} SSqlObj;

typedef struct SSqlStream {
  SSqlObj *pSql;
  uint32_t streamId;
  char     listed;
  bool     isProject;
  int16_t  precision;
  int64_t  num;  // number of computing count

  /*
   * keep the number of current result in computing,
   * the value will be set to 0 before set timer for next computing
   */
  int64_t numOfRes;

  int64_t useconds;  // total  elapsed time
  int64_t ctime;     // stream created time
  int64_t stime;     // stream next executed time
  int64_t etime;     // stream end query time, when time is larger then etime, the stream will be closed
  SInterval interval;
  void *  pTimer;

  void (*fp)();
  void *param;

  void (*callback)(void *);  // Callback function when stream is stopped from client level
  struct SSqlStream *prev, *next;
} SSqlStream;

int32_t tscInitRpc(const char *user, const char *secret, void** pDnodeConn);
void    tscInitMsgsFp();

int tsParseSql(SSqlObj *pSql, bool initial);

void tscProcessMsgFromServer(SRpcMsg *rpcMsg, SRpcEpSet *pEpSet);
int  tscProcessSql(SSqlObj *pSql);

int  tscRenewTableMeta(SSqlObj *pSql, int32_t tableIndex);
void tscQueueAsyncRes(SSqlObj *pSql);

void tscQueueAsyncError(void(*fp), void *param, int32_t code);

int tscProcessLocalCmd(SSqlObj *pSql);
int tscCfgDynamicOptions(char *msg);
int taos_retrieve(TAOS_RES *res);

int32_t tscTansformSQLFuncForSTableQuery(SQueryInfo *pQueryInfo);
void    tscRestoreSQLFuncForSTableQuery(SQueryInfo *pQueryInfo);

int32_t tscCreateResPointerInfo(SSqlRes *pRes, SQueryInfo *pQueryInfo);

void tscResetSqlCmdObj(SSqlCmd *pCmd, bool removeFromCache);

/**
 * free query result of the sql object
 * @param pObj
 */
void tscFreeSqlResult(SSqlObj *pSql);

/**
 * only free part of resources allocated during query.
 * TODO remove it later
 * Note: this function is multi-thread safe.
 * @param pObj
 */
void tscPartiallyFreeSqlObj(SSqlObj *pSql);

/**
 * free sql object, release allocated resource
 * @param pObj
 */
void tscFreeSqlObj(SSqlObj *pSql);
void tscFreeRegisteredSqlObj(void *pSql);
void tscFreeTableMetaHelper(void *pTableMeta);

void tscCloseTscObj(STscObj *pObj);

// todo move to taos? or create a new file: taos_internal.h
TAOS *taos_connect_a(char *ip, char *user, char *pass, char *db, uint16_t port, void (*fp)(void *, TAOS_RES *, int),
                     void *param, TAOS **taos);
TAOS_RES* taos_query_h(TAOS* taos, const char *sqlstr, TAOS_RES** res);
void waitForQueryRsp(void *param, TAOS_RES *tres, int code);

void doAsyncQuery(STscObj *pObj, SSqlObj *pSql, __async_cb_func_t fp, void *param, const char *sqlstr, size_t sqlLen);

void tscProcessMultiVnodesImportFromFile(SSqlObj *pSql);
void tscInitResObjForLocalQuery(SSqlObj *pObj, int32_t numOfRes, int32_t rowLen);
bool tscIsUpdateQuery(SSqlObj* pSql);
bool tscHasReachLimitation(SQueryInfo *pQueryInfo, SSqlRes *pRes);

char *tscGetErrorMsgPayload(SSqlCmd *pCmd);

int32_t tscInvalidSQLErrMsg(char *msg, const char *additionalInfo, const char *sql);
int32_t tscSQLSyntaxErrMsg(char* msg, const char* additionalInfo,  const char* sql);

int32_t tscToSQLCmd(SSqlObj *pSql, struct SSqlInfo *pInfo);

static FORCE_INLINE void tscGetResultColumnChr(SSqlRes* pRes, SFieldInfo* pFieldInfo, int32_t columnIndex) {
  SInternalField* pInfo = (SInternalField*) TARRAY_GET_ELEM(pFieldInfo->internalField, columnIndex);
  assert(pInfo->pSqlExpr != NULL);

  int32_t type = pInfo->pSqlExpr->resType;
  int32_t bytes = pInfo->pSqlExpr->resBytes;

  char* pData = pRes->data + (int32_t)(pInfo->pSqlExpr->offset * pRes->numOfRows + bytes * pRes->row);

  // user defined constant value output columns
  if (TSDB_COL_IS_UD_COL(pInfo->pSqlExpr->colInfo.flag)) {
    if (type == TSDB_DATA_TYPE_NCHAR || type == TSDB_DATA_TYPE_BINARY) {
      pData = pInfo->pSqlExpr->param[1].pz;
      pRes->length[columnIndex] = pInfo->pSqlExpr->param[1].nLen;
      pRes->tsrow[columnIndex] = (pInfo->pSqlExpr->param[1].nType == TSDB_DATA_TYPE_NULL) ? NULL : (unsigned char*)pData;
    } else {
      assert(bytes == tDataTypeDesc[type].nSize);

      pRes->tsrow[columnIndex] = isNull(pData, type) ? NULL : (unsigned char*)&pInfo->pSqlExpr->param[1].i64Key;
      pRes->length[columnIndex] = bytes;
    }
  } else {
    if (type == TSDB_DATA_TYPE_NCHAR || type == TSDB_DATA_TYPE_BINARY) {
      int32_t realLen = varDataLen(pData);
      assert(realLen <= bytes - VARSTR_HEADER_SIZE);

      pRes->tsrow[columnIndex] = (isNull(pData, type)) ? NULL : (unsigned char*)((tstr *)pData)->data;
      if (realLen < pInfo->pSqlExpr->resBytes - VARSTR_HEADER_SIZE) {  // todo refactor
        *(pData + realLen + VARSTR_HEADER_SIZE) = 0;
      }

      pRes->length[columnIndex] = realLen;
    } else {
      assert(bytes == tDataTypeDesc[type].nSize);

      pRes->tsrow[columnIndex] = isNull(pData, type) ? NULL : (unsigned char*)pData;
      pRes->length[columnIndex] = bytes;
    }
  }
}

extern SCacheObj*    tscMetaCache;
extern SCacheObj*    tscObjCache;
extern void *    tscTmr;
extern void *    tscQhandle;
extern int       tscKeepConn[];
extern int       tsInsertHeadSize;
extern int       tscNumOfThreads;
  
extern SRpcCorEpSet tscMgmtEpSet;

extern int (*tscBuildMsg[TSDB_SQL_MAX])(SSqlObj *pSql, SSqlInfo *pInfo);

int32_t tscCompareTidTags(const void* p1, const void* p2);
void tscBuildVgroupTableInfo(SSqlObj* pSql, STableMetaInfo* pTableMetaInfo, SArray* tables);

#ifdef __cplusplus
}
#endif

#endif
