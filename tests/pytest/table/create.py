###################################################################
#           Copyright (c) 2016 by TAOS Technologies, Inc.
#                     All rights reserved.
#
#  This file is proprietary and confidential to TAOS Technologies.
#  No part of this file may be reproduced, stored, transmitted,
#  disclosed or used in any form or by any means other than as
#  expressly provided by the written permission from Jianhui Tao
#
###################################################################

# -*- coding: utf-8 -*-

import sys
import taos
from util.log import tdLog
from util.cases import tdCases
from util.sql import tdSql


class TDTestCase:
    def init(self, conn, logSql):
        tdLog.debug("start to execute %s" % __file__)
        tdSql.init(conn.cursor(), logSql)

    def run(self):
        tdSql.prepare()

        print("==============step1")
        print("prepare data")
        tdSql.execute("create table db.st (ts timestamp, i int) tags(j int)")
        tdSql.execute("create table db.tb using st tags(1)")
        tdSql.execute("insert into db.tb values(now, 1)")

        print("==============step2")
        print("create table as select")
        try:
            tdSql.execute("create table db.test as select * from db.st")
        except Exception as e:
            tdLog.exit(e)

    def stop(self):
        tdSql.close()
        tdLog.success("%s successfully executed" % __file__)


tdCases.addWindows(__file__, TDTestCase())
tdCases.addLinux(__file__, TDTestCase())
