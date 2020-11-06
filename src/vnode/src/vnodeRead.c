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

#define _DEFAULT_SOURCE
#define _NON_BLOCKING_RETRIEVE  0

#include "os.h"

#include "tglobal.h"
#include "taoserror.h"
#include "taosmsg.h"
#include "tcache.h"
#include "query.h"
#include "trpc.h"
#include "tsdb.h"
#include "vnode.h"
#include "vnodeInt.h"
#include "tqueue.h"

static int32_t (*vnodeProcessReadMsgFp[TSDB_MSG_TYPE_MAX])(SVnodeObj *pVnode, SReadMsg *pReadMsg);
static int32_t  vnodeProcessQueryMsg(SVnodeObj *pVnode, SReadMsg *pReadMsg);
static int32_t  vnodeProcessFetchMsg(SVnodeObj *pVnode, SReadMsg *pReadMsg);
static int32_t  vnodeNotifyCurrentQhandle(void* handle, void* qhandle, int32_t vgId);

void vnodeInitReadFp(void) {
  vnodeProcessReadMsgFp[TSDB_MSG_TYPE_QUERY] = vnodeProcessQueryMsg;
  vnodeProcessReadMsgFp[TSDB_MSG_TYPE_FETCH] = vnodeProcessFetchMsg;
}

//
// After the fetch request enters the vnode queue, if the vnode cannot provide services, the process function are
// still required, or there will be a deadlock, so we don’t do any check here, but put the check codes before the
// request enters the queue
//
int32_t vnodeProcessRead(void *param, SReadMsg *pReadMsg) {
  SVnodeObj *pVnode = (SVnodeObj *)param;
  int msgType = pReadMsg->rpcMsg.msgType;

  if (vnodeProcessReadMsgFp[msgType] == NULL) {
    vDebug("vgId:%d, msgType:%s not processed, no handle", pVnode->vgId, taosMsg[msgType]);
    return TSDB_CODE_VND_MSG_NOT_PROCESSED;
  }

  return (*vnodeProcessReadMsgFp[msgType])(pVnode, pReadMsg);
}

int32_t vnodeCheckRead(void *param) {
  SVnodeObj *pVnode = param;
  if (pVnode->status != TAOS_VN_STATUS_READY) {
    vDebug("vgId:%d, vnode status is %s, recCount:%d pVnode:%p", pVnode->vgId, vnodeStatus[pVnode->status],
           pVnode->refCount, pVnode);
    return TSDB_CODE_APP_NOT_READY;
  }

  // tsdb may be in reset state
  if (pVnode->tsdb == NULL) {
    vDebug("vgId:%d, tsdb is null, recCount:%d pVnode:%p", pVnode->vgId, pVnode->refCount, pVnode);
    return TSDB_CODE_APP_NOT_READY;
  }

  if (pVnode->role != TAOS_SYNC_ROLE_SLAVE && pVnode->role != TAOS_SYNC_ROLE_MASTER) {
    vDebug("vgId:%d, replica:%d role:%s, recCount:%d pVnode:%p", pVnode->vgId, pVnode->syncCfg.replica,
           syncRole[pVnode->role], pVnode->refCount, pVnode);
    return TSDB_CODE_APP_NOT_READY;
  }

  return TSDB_CODE_SUCCESS;
}
static int32_t vnodePutItemIntoReadQueue(SVnodeObj *pVnode, void **qhandle, void *ahandle) {
  int32_t code = vnodeCheckRead(pVnode);
  if (code != TSDB_CODE_SUCCESS) return code;

  SReadMsg *pRead = (SReadMsg *)taosAllocateQitem(sizeof(SReadMsg));
  pRead->rpcMsg.msgType = TSDB_MSG_TYPE_QUERY;
  pRead->pCont = qhandle;
  pRead->contLen = 0;
  pRead->rpcMsg.ahandle = ahandle;

  atomic_add_fetch_32(&pVnode->refCount, 1);

  vDebug("QInfo:%p add to vread queue for exec query, msg:%p", *qhandle, pRead);
  taosWriteQitem(pVnode->rqueue, TAOS_QTYPE_QUERY, pRead);

  return TSDB_CODE_SUCCESS;
}

/**
 *
 * @param pRet         response message object
 * @param pVnode       the vnode object
 * @param handle       qhandle for executing query
 * @param freeHandle   free qhandle or not
 * @param ahandle      sqlObj address at client side
 * @return
 */
static int32_t vnodeDumpQueryResult(SRspRet *pRet, void *pVnode, void **handle, bool *freeHandle, void *ahandle) {
  bool continueExec = false;

  int32_t code = TSDB_CODE_SUCCESS;
  if ((code = qDumpRetrieveResult(*handle, (SRetrieveTableRsp **)&pRet->rsp, &pRet->len, &continueExec)) == TSDB_CODE_SUCCESS) {
    if (continueExec) {
      *freeHandle = false;
      code = vnodePutItemIntoReadQueue(pVnode, handle, ahandle);
      if (code != TSDB_CODE_SUCCESS) {
        *freeHandle = true;
        return code;
      } else {
        pRet->qhandle = *handle;
      }
    } else {
      *freeHandle = true;
      vDebug("QInfo:%p exec completed, free handle:%d", *handle, *freeHandle);
    }
  } else {
    SRetrieveTableRsp *pRsp = (SRetrieveTableRsp *)rpcMallocCont(sizeof(SRetrieveTableRsp));
    memset(pRsp, 0, sizeof(SRetrieveTableRsp));
    pRsp->completed = true;

    pRet->rsp = pRsp;
    pRet->len = sizeof(SRetrieveTableRsp);
    *freeHandle = true;
  }

  return code;
}

static void vnodeBuildNoResultQueryRsp(SRspRet *pRet) {
  pRet->rsp = (SRetrieveTableRsp *)rpcMallocCont(sizeof(SRetrieveTableRsp));
  pRet->len = sizeof(SRetrieveTableRsp);

  memset(pRet->rsp, 0, sizeof(SRetrieveTableRsp));
  SRetrieveTableRsp *pRsp = pRet->rsp;

  pRsp->completed = true;
}

static int32_t vnodeProcessQueryMsg(SVnodeObj *pVnode, SReadMsg *pReadMsg) {
  void *   pCont = pReadMsg->pCont;
  int32_t  contLen = pReadMsg->contLen;
  SRspRet *pRet = &pReadMsg->rspRet;

  SQueryTableMsg *pQueryTableMsg = (SQueryTableMsg *)pCont;
  memset(pRet, 0, sizeof(SRspRet));

  // qHandle needs to be freed correctly
  if (pReadMsg->rpcMsg.code == TSDB_CODE_RPC_NETWORK_UNAVAIL) {
    SRetrieveTableMsg *killQueryMsg = (SRetrieveTableMsg *)pReadMsg->pCont;
    killQueryMsg->free = htons(killQueryMsg->free);
    killQueryMsg->qhandle = htobe64(killQueryMsg->qhandle);

    vWarn("QInfo:%p connection %p broken, kill query", (void *)killQueryMsg->qhandle, pReadMsg->rpcMsg.handle);
    assert(pReadMsg->rpcMsg.contLen > 0 && killQueryMsg->free == 1);

    void **qhandle = qAcquireQInfo(pVnode->qMgmt, (uint64_t)killQueryMsg->qhandle);
    if (qhandle == NULL || *qhandle == NULL) {
      vWarn("QInfo:%p invalid qhandle, no matched query handle, conn:%p", (void *)killQueryMsg->qhandle,
            pReadMsg->rpcMsg.handle);
    } else {
      assert(*qhandle == (void *)killQueryMsg->qhandle);

      qKillQuery(*qhandle);
      qReleaseQInfo(pVnode->qMgmt, (void **)&qhandle, true);
    }

    return TSDB_CODE_TSC_QUERY_CANCELLED;
  }

  int32_t code = TSDB_CODE_SUCCESS;
  void ** handle = NULL;

  if (contLen != 0) {
    qinfo_t pQInfo = NULL;
    code = qCreateQueryInfo(pVnode->tsdb, pVnode->vgId, pQueryTableMsg, &pQInfo);

    SQueryTableRsp *pRsp = (SQueryTableRsp *)rpcMallocCont(sizeof(SQueryTableRsp));
    pRsp->code = code;
    pRsp->qhandle = 0;

    pRet->len = sizeof(SQueryTableRsp);
    pRet->rsp = pRsp;
    int32_t vgId = pVnode->vgId;

    // current connect is broken
    if (code == TSDB_CODE_SUCCESS) {
      handle = qRegisterQInfo(pVnode->qMgmt, (uint64_t)pQInfo);
      if (handle == NULL) {  // failed to register qhandle
        pRsp->code = terrno;
        terrno = 0;
        vError("vgId:%d QInfo:%p register qhandle failed, return to app, code:%s", pVnode->vgId, (void *)pQInfo,
               tstrerror(pRsp->code));
        qDestroyQueryInfo(pQInfo);  // destroy it directly
        return pRsp->code;
      } else {
        assert(*handle == pQInfo);
        pRsp->qhandle = htobe64((uint64_t)pQInfo);
      }

      if (handle != NULL &&
          vnodeNotifyCurrentQhandle(pReadMsg->rpcMsg.handle, *handle, pVnode->vgId) != TSDB_CODE_SUCCESS) {
        vError("vgId:%d, QInfo:%p, query discarded since link is broken, %p", pVnode->vgId, *handle,
               pReadMsg->rpcMsg.handle);
        pRsp->code = TSDB_CODE_RPC_NETWORK_UNAVAIL;
        qReleaseQInfo(pVnode->qMgmt, (void **)&handle, true);
        return pRsp->code;
      }
    } else {
      assert(pQInfo == NULL);
    }

    if (handle != NULL) {
      vDebug("vgId:%d, QInfo:%p, dnode query msg disposed, create qhandle and returns to app", vgId, *handle);
      code = vnodePutItemIntoReadQueue(pVnode, handle, pReadMsg->rpcMsg.ahandle);
      if (code != TSDB_CODE_SUCCESS) {
        pRsp->code = code;
        qReleaseQInfo(pVnode->qMgmt, (void **)&handle, true);
        return pRsp->code;
      }
    }
  } else {
    assert(pCont != NULL);
    void **qhandle = (void **)pCont;

    vDebug("vgId:%d, QInfo:%p, dnode continues to exec query", pVnode->vgId, *qhandle);


#if _NON_BLOCKING_RETRIEVE
    bool freehandle = false;
    bool buildRes = qTableQuery(*qhandle);  // do execute query

    // build query rsp, the retrieve request has reached here already
    if (buildRes) {
      // update the connection info according to the retrieve connection
      pReadMsg->rpcMsg.handle = qGetResultRetrieveMsg(*qhandle);
      assert(pReadMsg->rpcMsg.handle != NULL);

      vDebug("vgId:%d, QInfo:%p, start to build retrieval rsp after query paused, %p", pVnode->vgId, *qhandle,
             pReadMsg->rpcMsg.handle);

      // set the real rsp error code
      pReadMsg->rpcMsg.code = vnodeDumpQueryResult(&pReadMsg->rspRet, pVnode, qhandle, &freehandle, pReadMsg->rpcMsg.ahandle);

      // NOTE: set return code to be TSDB_CODE_QRY_HAS_RSP to notify dnode to return msg to client
      code = TSDB_CODE_QRY_HAS_RSP;
    } else {
      void* h1 = qGetResultRetrieveMsg(*qhandle);
      assert(h1 == NULL);

      freehandle = qQueryCompleted(*qhandle);
    }

    // NOTE: if the qhandle is not put into vread queue or query is completed, free the qhandle.
    // If the building of result is not required, simply free it. Otherwise, mandatorily free the qhandle
    if (freehandle || (!buildRes)) {
      qReleaseQInfo(pVnode->qMgmt, (void **)&qhandle, freehandle);
    }
#else
    qTableQuery(*qhandle);  // do execute query
    qReleaseQInfo(pVnode->qMgmt, (void **)&qhandle, false);
#endif
  }

  return code;
}

static int32_t vnodeProcessFetchMsg(SVnodeObj *pVnode, SReadMsg *pReadMsg) {
  void *   pCont = pReadMsg->pCont;
  SRspRet *pRet = &pReadMsg->rspRet;

  SRetrieveTableMsg *pRetrieve = pCont;
  pRetrieve->free = htons(pRetrieve->free);
  pRetrieve->qhandle = htobe64(pRetrieve->qhandle);

  vDebug("vgId:%d, QInfo:%p, retrieve msg is disposed, free:%d, conn:%p", pVnode->vgId, (void *)pRetrieve->qhandle,
         pRetrieve->free, pReadMsg->rpcMsg.handle);

  memset(pRet, 0, sizeof(SRspRet));

  terrno = TSDB_CODE_SUCCESS;
  int32_t code = TSDB_CODE_SUCCESS;
  void ** handle = qAcquireQInfo(pVnode->qMgmt, pRetrieve->qhandle);
  if (handle == NULL) {
    code = terrno;
    terrno = TSDB_CODE_SUCCESS;
  } else if ((*handle) != (void *)pRetrieve->qhandle) {
    code = TSDB_CODE_QRY_INVALID_QHANDLE;
  }

  if (code != TSDB_CODE_SUCCESS) {
    vDebug("vgId:%d, invalid handle in retrieving result, code:0x%08x, QInfo:%p", pVnode->vgId, code, (void *)pRetrieve->qhandle);
    vnodeBuildNoResultQueryRsp(pRet);
    return code;
  }
  
  if (pRetrieve->free == 1) {
    vWarn("vgId:%d, QInfo:%p, retrieve msg received to kill query and free qhandle", pVnode->vgId, *handle);
    qKillQuery(*handle);
    qReleaseQInfo(pVnode->qMgmt, (void **)&handle, true);

    vnodeBuildNoResultQueryRsp(pRet);
    code = TSDB_CODE_TSC_QUERY_CANCELLED;
    return code;
  }

  // register the qhandle to connect to quit query immediate if connection is broken
  if (vnodeNotifyCurrentQhandle(pReadMsg->rpcMsg.handle, *handle, pVnode->vgId) != TSDB_CODE_SUCCESS) {
    vError("vgId:%d, QInfo:%p, retrieve discarded since link is broken, %p", pVnode->vgId, *handle,
           pReadMsg->rpcMsg.handle);
    code = TSDB_CODE_RPC_NETWORK_UNAVAIL;
    qKillQuery(*handle);
    qReleaseQInfo(pVnode->qMgmt, (void **)&handle, true);
    return code;
  }

  bool freeHandle = true;
  bool buildRes = false;

  code = qRetrieveQueryResultInfo(*handle, &buildRes, pReadMsg->rpcMsg.handle);
  if (code != TSDB_CODE_SUCCESS) {
    // TODO handle malloc failure
    pRet->rsp = (SRetrieveTableRsp *)rpcMallocCont(sizeof(SRetrieveTableRsp));
    pRet->len = sizeof(SRetrieveTableRsp);
    memset(pRet->rsp, 0, sizeof(SRetrieveTableRsp));
    freeHandle = true;
  } else {  // result is not ready, return immediately
    assert(buildRes == true);
#if _NON_BLOCKING_RETRIEVE
    if (!buildRes) {
      assert(pReadMsg->rpcMsg.handle != NULL);

      qReleaseQInfo(pVnode->qMgmt, (void **)&handle, false);
      return TSDB_CODE_QRY_NOT_READY;
    }
#endif

    // ahandle is the sqlObj pointer
    code = vnodeDumpQueryResult(pRet, pVnode, handle, &freeHandle, pReadMsg->rpcMsg.ahandle);
  }

  // If qhandle is not added into vread queue, the query should be completed already or paused with error.
  // Here free qhandle immediately
  if (freeHandle) {
    qReleaseQInfo(pVnode->qMgmt, (void **)&handle, true);
  }

  return code;
}

// notify connection(handle) that current qhandle is created, if current connection from
// client is broken, the query needs to be killed immediately.
int32_t vnodeNotifyCurrentQhandle(void *handle, void *qhandle, int32_t vgId) {
  SRetrieveTableMsg *killQueryMsg = rpcMallocCont(sizeof(SRetrieveTableMsg));
  killQueryMsg->qhandle = htobe64((uint64_t)qhandle);
  killQueryMsg->free = htons(1);
  killQueryMsg->header.vgId = htonl(vgId);
  killQueryMsg->header.contLen = htonl(sizeof(SRetrieveTableMsg));

  vDebug("QInfo:%p register qhandle to connect:%p", qhandle, handle);
  return rpcReportProgress(handle, (char *)killQueryMsg, sizeof(SRetrieveTableMsg));
}
