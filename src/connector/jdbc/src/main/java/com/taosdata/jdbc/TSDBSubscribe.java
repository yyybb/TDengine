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

import javax.management.OperationsException;
import java.sql.SQLException;
import java.util.Map;
import java.util.TimerTask;
import java.util.concurrent.*;

public class TSDBSubscribe {
    private TSDBJNIConnector connecter = null;
    private long id = 0;

    TSDBSubscribe(TSDBJNIConnector connecter, long id) throws SQLException {
        if (null != connecter) {
            this.connecter = connecter;
            this.id = id;
        } else {
            throw new SQLException(TSDBConstants.FixErrMsg(TSDBConstants.JNI_CONNECTION_NULL));
        }
    }

    /**
     * consume
     *
     * @throws OperationsException, SQLException
     */
    public TSDBResultSet consume() throws OperationsException, SQLException {
        if (this.connecter.isClosed()) {
            throw new SQLException(TSDBConstants.FixErrMsg(TSDBConstants.JNI_CONNECTION_NULL));
        }

        long resultSetPointer = this.connecter.consume(this.id);

        if (resultSetPointer == TSDBConstants.JNI_CONNECTION_NULL) {
            throw new SQLException(TSDBConstants.FixErrMsg(TSDBConstants.JNI_CONNECTION_NULL));
        } else if (resultSetPointer == TSDBConstants.JNI_NULL_POINTER) {
            return null;
        } else {
            return new TSDBResultSet(this.connecter, resultSetPointer);
        }
    }

    /**
     * close subscription
     *
     * @param keepProgress
     * @throws SQLException
     */
    public void close(boolean keepProgress) throws SQLException {
        if (this.connecter.isClosed()) {
            throw new SQLException(TSDBConstants.FixErrMsg(TSDBConstants.JNI_CONNECTION_NULL));
        }
        this.connecter.unsubscribe(this.id, keepProgress);
    }
}

