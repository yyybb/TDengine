/***************************************************************************
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
 *****************************************************************************/
package com.taosdata.jdbc;

import java.sql.*;
import java.util.ArrayList;
import java.util.List;

public class TSDBStatement implements Statement {
    private TSDBJNIConnector connecter = null;

    /**
     * To store batched commands
     */
    protected List<String> batchedArgs;

    /**
     * Timeout for a query
     */
    protected int queryTimeout = 0;

    private Long pSql = 0l;

    /**
     * Status of current statement
     */
    private boolean isClosed = true;
    private int affectedRows = 0;

    private TSDBConnection connection;

    public void setConnection(TSDBConnection connection) {
        this.connection = connection;
    }

    TSDBStatement(TSDBConnection connection, TSDBJNIConnector connecter) {
        this.connection = connection;
        this.connecter = connecter;
        this.isClosed = false;
    }

    public <T> T unwrap(Class<T> iface) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean isWrapperFor(Class<?> iface) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public ResultSet executeQuery(String sql) throws SQLException {
        if (isClosed) {
            throw new SQLException("Invalid method call on a closed statement.");
        }

        // TODO make sure it is not a update query
        pSql = this.connecter.executeQuery(sql);

        long resultSetPointer = this.connecter.getResultSet();

        if (resultSetPointer == TSDBConstants.JNI_CONNECTION_NULL) {
            this.connecter.freeResultSet(pSql);
            throw new SQLException(TSDBConstants.FixErrMsg(TSDBConstants.JNI_CONNECTION_NULL));
        }

        // create/insert/update/delete/alter
        if (resultSetPointer == TSDBConstants.JNI_NULL_POINTER) {
            this.connecter.freeResultSet(pSql);
            return null;
        }

        if (!this.connecter.isUpdateQuery(pSql)) {
            return new TSDBResultSet(this.connecter, resultSetPointer);
        } else {
            this.connecter.freeResultSet(pSql);
            return null;
        }

    }

    public int executeUpdate(String sql) throws SQLException {
        if (isClosed) {
            throw new SQLException("Invalid method call on a closed statement.");
        }

        // TODO check if current query is update query
        pSql = this.connecter.executeQuery(sql);
        long resultSetPointer = this.connecter.getResultSet();

        if (resultSetPointer == TSDBConstants.JNI_CONNECTION_NULL) {
            this.connecter.freeResultSet(pSql);
            throw new SQLException(TSDBConstants.FixErrMsg(TSDBConstants.JNI_CONNECTION_NULL));
        }

        this.affectedRows = this.connecter.getAffectedRows(pSql);
        this.connecter.freeResultSet(pSql);

        return this.affectedRows;
    }

    public String getErrorMsg(long pSql) {
        return this.connecter.getErrMsg(pSql);
    }

    public void close() throws SQLException {
        if (!isClosed) {
            if (!this.connecter.isResultsetClosed()) {
                this.connecter.freeResultSet();
            }
            isClosed = true;
        }
    }

    public int getMaxFieldSize() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public void setMaxFieldSize(int max) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int getMaxRows() throws SQLException {
        // always set maxRows to zero, meaning unlimitted rows in a resultSet
        return 0;
    }

    public void setMaxRows(int max) throws SQLException {
        // always set maxRows to zero, meaning unlimitted rows in a resultSet
    }

    public void setEscapeProcessing(boolean enable) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int getQueryTimeout() throws SQLException {
        return queryTimeout;
    }

    public void setQueryTimeout(int seconds) throws SQLException {
        this.queryTimeout = seconds;
    }

    public void cancel() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public SQLWarning getWarnings() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public void clearWarnings() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public void setCursorName(String name) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean execute(String sql) throws SQLException {
        if (isClosed) {
            throw new SQLException("Invalid method call on a closed statement.");
        }
        boolean res = true;
        pSql = this.connecter.executeQuery(sql);
        long resultSetPointer = this.connecter.getResultSet();

        if (resultSetPointer == TSDBConstants.JNI_CONNECTION_NULL) {
            this.connecter.freeResultSet(pSql);
            throw new SQLException(TSDBConstants.FixErrMsg(TSDBConstants.JNI_CONNECTION_NULL));
        } else if (resultSetPointer == TSDBConstants.JNI_NULL_POINTER) {
            // no result set is retrieved
            this.connecter.freeResultSet(pSql);
            res = false;
        }

        return res;
    }

    public ResultSet getResultSet() throws SQLException {
        if (isClosed) {
            throw new SQLException("Invalid method call on a closed statement.");
        }
        long resultSetPointer = connecter.getResultSet();
        TSDBResultSet resSet = null;
        if (resultSetPointer != TSDBConstants.JNI_NULL_POINTER) {
            resSet = new TSDBResultSet(connecter, resultSetPointer);
        }
        return resSet;
    }

    public int getUpdateCount() throws SQLException {
        if (isClosed) {
            throw new SQLException("Invalid method call on a closed statement.");
        }

        return this.affectedRows;
    }

    public boolean getMoreResults() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public void setFetchDirection(int direction) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int getFetchDirection() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    /*
     * used by spark
     */
    public void setFetchSize(int rows) throws SQLException {
    }

    /*
     * used by spark
     */
    public int getFetchSize() throws SQLException {
        return 4096;
    }

    public int getResultSetConcurrency() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int getResultSetType() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public void addBatch(String sql) throws SQLException {
        if (batchedArgs == null) {
            batchedArgs = new ArrayList<>();
        }
        batchedArgs.add(sql);
    }

    public void clearBatch() throws SQLException {
        batchedArgs.clear();
    }

    public int[] executeBatch() throws SQLException {
        if (isClosed) {
            throw new SQLException("Invalid method call on a closed statement.");
        }
        if (batchedArgs == null) {
            throw new SQLException(TSDBConstants.WrapErrMsg("Batch is empty!"));
        } else {
            int[] res = new int[batchedArgs.size()];
            for (int i = 0; i < batchedArgs.size(); i++) {
                res[i] = executeUpdate(batchedArgs.get(i));
            }
            return res;
        }
    }

    public Connection getConnection() throws SQLException {
        if (this.connecter != null)
            return this.connection;
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean getMoreResults(int current) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public ResultSet getGeneratedKeys() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int executeUpdate(String sql, int autoGeneratedKeys) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int executeUpdate(String sql, int[] columnIndexes) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int executeUpdate(String sql, String[] columnNames) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean execute(String sql, int autoGeneratedKeys) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean execute(String sql, int[] columnIndexes) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean execute(String sql, String[] columnNames) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public int getResultSetHoldability() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean isClosed() throws SQLException {
        return isClosed;
    }

    public void setPoolable(boolean poolable) throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean isPoolable() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public void closeOnCompletion() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }

    public boolean isCloseOnCompletion() throws SQLException {
        throw new SQLException(TSDBConstants.UNSUPPORT_METHOD_EXCEPTIONZ_MSG);
    }
}
