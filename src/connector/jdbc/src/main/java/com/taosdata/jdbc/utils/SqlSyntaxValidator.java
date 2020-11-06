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
package com.taosdata.jdbc.utils;

import com.taosdata.jdbc.TSDBConnection;
import com.taosdata.jdbc.TSDBJNIConnector;

import java.sql.Connection;
import java.sql.SQLException;

public class SqlSyntaxValidator {

    private TSDBConnection tsdbConnection;

    public SqlSyntaxValidator(Connection connection) {
        this.tsdbConnection = (TSDBConnection) connection;
    }

    public boolean validateSqlSyntax(String sql) throws SQLException {

        boolean res = false;
        if (tsdbConnection == null || tsdbConnection.isClosed()) {
            throw new SQLException("invalid connection");
        } else {
            TSDBJNIConnector jniConnector  = tsdbConnection.getConnection();
            if (jniConnector == null) {
                throw new SQLException("jniConnector is null");
            } else {
                res = jniConnector.validateCreateTableSql(sql);
            }
        }
        return res;
    }
}
