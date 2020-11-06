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

#ifndef TDENGINE_TSCHEMAUTIL_H
#define TDENGINE_TSCHEMAUTIL_H

#ifdef __cplusplus
extern "C" {
#endif

#include "taosmsg.h"
#include "tstoken.h"
#include "tsclient.h"

#define VALIDNUMOFCOLS(x) ((x) >= TSDB_MIN_COLUMNS && (x) <= TSDB_MAX_COLUMNS)

//struct SSchema;

/**
 * get the number of tags of this table
 * @param pTableMeta
 * @return
 */
int32_t tscGetNumOfTags(const STableMeta* pTableMeta);

/**
 * get the number of columns of this table
 * @param pTableMeta
 * @return
 */
int32_t tscGetNumOfColumns(const STableMeta* pTableMeta);

/**
 * get the basic info of this table
 * @param pTableMeta
 * @return
 */
STableComInfo tscGetTableInfo(const STableMeta* pTableMeta);

/**
 * get the schema
 * @param pTableMeta
 * @return
 */
SSchema* tscGetTableSchema(const STableMeta* pTableMeta);

/**
 * get the tag schema
 * @param pMeta
 * @return
 */
SSchema *tscGetTableTagSchema(const STableMeta *pMeta);

/**
 * get the column schema according to the column index
 * @param pMeta
 * @param colIndex
 * @return
 */
SSchema *tscGetTableColumnSchema(const STableMeta *pMeta, int32_t colIndex);

/**
 * get the column schema according to the column id
 * @param pTableMeta
 * @param colId
 * @return
 */
SSchema* tscGetTableColumnSchemaById(STableMeta* pTableMeta, int16_t colId);

/**
 * check if the schema is valid or not, including following aspects:
 * 1. number of columns
 * 2. column types
 * 3. column length
 * 4. column names
 * 5. total length
 *
 * @param pSchema
 * @param numOfCols
 * @return
 */
bool isValidSchema(struct SSchema *pSchema, int32_t numOfCols);

/**
 * get the schema for the "tbname" column. it is a built column
 * @return
 */
SSchema tscGetTbnameColumnSchema();

/**
 * create the table meta from the msg
 * @param pTableMetaMsg
 * @param size size of the table meta
 * @return
 */
STableMeta* tscCreateTableMetaFromMsg(STableMetaMsg* pTableMetaMsg, size_t* size);

//todo tags value as well as the table id structure needs refactor
char *tsGetTagsValue(STableMeta *pMeta);

#ifdef __cplusplus
}
#endif

#endif  // TDENGINE_TSCHEMAUTIL_H
