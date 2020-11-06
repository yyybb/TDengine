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
#include "os.h"
#include "taosmsg.h"
#include "taoserror.h"
#include "tqueue.h"
#include "trpc.h"
#include "tutil.h"
#include "tsdb.h"
#include "twal.h"
#include "tsync.h"
#include "tdataformat.h"
#include "vnode.h"
#include "vnodeInt.h"
#include "syncInt.h"
#include "tcq.h"

static int32_t (*vnodeProcessWriteMsgFp[TSDB_MSG_TYPE_MAX])(SVnodeObj *, void *, SRspRet *);
static int32_t vnodeProcessSubmitMsg(SVnodeObj *pVnode, void *pMsg, SRspRet *);
static int32_t vnodeProcessCreateTableMsg(SVnodeObj *pVnode, void *pMsg, SRspRet *);
static int32_t vnodeProcessDropTableMsg(SVnodeObj *pVnode, void *pMsg, SRspRet *);
static int32_t vnodeProcessAlterTableMsg(SVnodeObj *pVnode, void *pMsg, SRspRet *);
static int32_t vnodeProcessDropStableMsg(SVnodeObj *pVnode, void *pMsg, SRspRet *);
static int32_t vnodeProcessUpdateTagValMsg(SVnodeObj *pVnode, void *pCont, SRspRet *pRet);

void vnodeInitWriteFp(void) {
  vnodeProcessWriteMsgFp[TSDB_MSG_TYPE_SUBMIT]          = vnodeProcessSubmitMsg;
  vnodeProcessWriteMsgFp[TSDB_MSG_TYPE_MD_CREATE_TABLE] = vnodeProcessCreateTableMsg;
  vnodeProcessWriteMsgFp[TSDB_MSG_TYPE_MD_DROP_TABLE]   = vnodeProcessDropTableMsg;
  vnodeProcessWriteMsgFp[TSDB_MSG_TYPE_MD_ALTER_TABLE]  = vnodeProcessAlterTableMsg;
  vnodeProcessWriteMsgFp[TSDB_MSG_TYPE_MD_DROP_STABLE]  = vnodeProcessDropStableMsg;
  vnodeProcessWriteMsgFp[TSDB_MSG_TYPE_UPDATE_TAG_VAL]  = vnodeProcessUpdateTagValMsg;
}

int32_t vnodeProcessWrite(void *param1, int qtype, void *param2, void *item) {
  int32_t    code = 0;
  SVnodeObj *pVnode = (SVnodeObj *)param1;
  SWalHead * pHead = param2;

  if (vnodeProcessWriteMsgFp[pHead->msgType] == NULL) {
    vDebug("vgId:%d, msgType:%s not processed, no handle", pVnode->vgId, taosMsg[pHead->msgType]);
    return TSDB_CODE_VND_MSG_NOT_PROCESSED;
  }

  if (pHead->version == 0) {  // from client or CQ
    if (pVnode->status != TAOS_VN_STATUS_READY) {
      vDebug("vgId:%d, msgType:%s not processed, vnode status is %d", pVnode->vgId, taosMsg[pHead->msgType],
             pVnode->status);
      return TSDB_CODE_APP_NOT_READY;  // it may be in deleting or closing state
    }

    if (pVnode->role != TAOS_SYNC_ROLE_MASTER) {
      vDebug("vgId:%d, msgType:%s not processed, replica:%d role:%s", pVnode->vgId, taosMsg[pHead->msgType],
             pVnode->syncCfg.replica, syncRole[pVnode->role]);
      return TSDB_CODE_APP_NOT_READY;
    }

    // assign version
    pHead->version = pVnode->version + 1;
    if (pVnode->delay) usleep(pVnode->delay * 1000);

  } else {  // from wal or forward
    // for data from WAL or forward, version may be smaller
    if (pHead->version <= pVnode->version) return 0;
  }

  // forward to peers, even it is WAL/FWD, it shall be called to update version in sync
  int32_t syncCode = 0;
  syncCode = syncForwardToPeer(pVnode->sync, pHead, item, qtype);
  if (syncCode < 0) return syncCode;

  // write into WAL
  code = walWrite(pVnode->wal, pHead);
  if (code < 0) return code;

  pVnode->version = pHead->version;

  // write data locally
  code = (*vnodeProcessWriteMsgFp[pHead->msgType])(pVnode, pHead->cont, item);
  if (code < 0) return code;

  return syncCode;
}

int32_t vnodeCheckWrite(void *param) {
  SVnodeObj *pVnode = param;
  if (!(pVnode->accessState & TSDB_VN_WRITE_ACCCESS)) {
    vDebug("vgId:%d, no write auth, recCount:%d pVnode:%p", pVnode->vgId, pVnode->refCount, pVnode);
    return TSDB_CODE_VND_NO_WRITE_AUTH;
  }

  // tsdb may be in reset state
  if (pVnode->tsdb == NULL) {
    vDebug("vgId:%d, tsdb is null, recCount:%d pVnode:%p", pVnode->vgId, pVnode->refCount, pVnode);
    return TSDB_CODE_APP_NOT_READY;
  }

  if (pVnode->status == TAOS_VN_STATUS_CLOSING) {
    vDebug("vgId:%d, vnode status is %s, recCount:%d pVnode:%p", pVnode->vgId, vnodeStatus[pVnode->status],
           pVnode->refCount, pVnode);
    return TSDB_CODE_APP_NOT_READY;
  }

  return TSDB_CODE_SUCCESS;
}

void vnodeConfirmForward(void *param, uint64_t version, int32_t code) {
  SVnodeObj *pVnode = (SVnodeObj *)param;
  syncConfirmForward(pVnode->sync, version, code);
}

static int32_t vnodeProcessSubmitMsg(SVnodeObj *pVnode, void *pCont, SRspRet *pRet) {
  int32_t code = TSDB_CODE_SUCCESS;

  vTrace("vgId:%d, submit msg is processed", pVnode->vgId);

  // save insert result into item
  SShellSubmitRspMsg *pRsp = NULL;
  if (pRet) {
    pRet->len = sizeof(SShellSubmitRspMsg);
    pRet->rsp = rpcMallocCont(pRet->len);
    pRsp = pRet->rsp;
  }

  if (tsdbInsertData(pVnode->tsdb, pCont, pRsp) < 0) code = terrno;

  return code;
}

static int32_t vnodeProcessCreateTableMsg(SVnodeObj *pVnode, void *pCont, SRspRet *pRet) {
  int code = TSDB_CODE_SUCCESS;

  STableCfg *pCfg = tsdbCreateTableCfgFromMsg((SMDCreateTableMsg *)pCont);
  if (pCfg == NULL) {
    ASSERT(terrno != 0);
    return terrno;
  }

  if (tsdbCreateTable(pVnode->tsdb, pCfg) < 0) {
    code = terrno;
    ASSERT(code != 0);
  }

  tsdbClearTableCfg(pCfg);
  return code;
}

static int32_t vnodeProcessDropTableMsg(SVnodeObj *pVnode, void *pCont, SRspRet *pRet) {
  SMDDropTableMsg *pTable = pCont;
  int32_t          code = TSDB_CODE_SUCCESS;

  vDebug("vgId:%d, table:%s, start to drop", pVnode->vgId, pTable->tableId);
  STableId tableId = {.uid = htobe64(pTable->uid), .tid = htonl(pTable->tid)};

  if (tsdbDropTable(pVnode->tsdb, tableId) < 0) code = terrno;

  return code;
}

static int32_t vnodeProcessAlterTableMsg(SVnodeObj *pVnode, void *pCont, SRspRet *pRet) {
  // TODO: disposed in tsdb
  // STableCfg *pCfg = tsdbCreateTableCfgFromMsg((SMDCreateTableMsg *)pCont);
  // if (pCfg == NULL) return terrno;
  // if (tsdbCreateTable(pVnode->tsdb, pCfg) < 0) code = terrno;

  // tsdbClearTableCfg(pCfg);
  vDebug("vgId:%d, alter table msg is received", pVnode->vgId);
  return TSDB_CODE_SUCCESS;
}

static int32_t vnodeProcessDropStableMsg(SVnodeObj *pVnode, void *pCont, SRspRet *pRet) {
  SMDDropSTableMsg *pTable = pCont;
  int32_t           code = TSDB_CODE_SUCCESS;

  vDebug("vgId:%d, stable:%s, start to drop", pVnode->vgId, pTable->tableId);

  STableId stableId = {.uid = htobe64(pTable->uid), .tid = -1};

  if (tsdbDropTable(pVnode->tsdb, stableId) < 0) code = terrno;

  vDebug("vgId:%d, stable:%s, drop stable result:%s", pVnode->vgId, pTable->tableId, tstrerror(code));

  return code;
}

static int32_t vnodeProcessUpdateTagValMsg(SVnodeObj *pVnode, void *pCont, SRspRet *pRet) {
  if (tsdbUpdateTableTagValue(pVnode->tsdb, (SUpdateTableTagValMsg *)pCont) < 0) {
    return terrno;
  }
  return TSDB_CODE_SUCCESS;
}


int vnodeWriteCqMsgToQueue(void *param, void *data, int type) {
  SVnodeObj *pVnode = param;
  SWalHead * pHead = data;

  int size = sizeof(SWalHead) + pHead->len;
  SSyncHead *pSync = (SSyncHead*) taosAllocateQitem(size + sizeof(SSyncHead));
  SWalHead *pWal = (SWalHead *)(pSync + 1);
  memcpy(pWal, pHead, size);

  atomic_add_fetch_32(&pVnode->refCount, 1);
  vTrace("CQ: vgId:%d, get vnode wqueue, refCount:%d pVnode:%p", pVnode->vgId, pVnode->refCount, pVnode);

  taosWriteQitem(pVnode->wqueue, type, pSync);

  return 0;
}


int vnodeWriteToQueue(void *param, void *data, int type) {
  SVnodeObj *pVnode = param;
  SWalHead * pHead = data;

  int size = sizeof(SWalHead) + pHead->len;
  SWalHead *pWal = (SWalHead *)taosAllocateQitem(size);
  memcpy(pWal, pHead, size);

  atomic_add_fetch_32(&pVnode->refCount, 1);
  vTrace("vgId:%d, get vnode wqueue, refCount:%d pVnode:%p", pVnode->vgId, pVnode->refCount, pVnode);

  taosWriteQitem(pVnode->wqueue, type, pWal);

  return 0;
}
