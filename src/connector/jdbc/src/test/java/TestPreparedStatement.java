import com.taosdata.jdbc.TSDBDriver;
import com.taosdata.jdbc.TSDBPreparedStatement;

import java.sql.*;
import java.util.Properties;

public class TestPreparedStatement {

    public static void main(String[] args) throws SQLException {
        Connection connection = null;
        try {
            Class.forName("com.taosdata.jdbc.TSDBDriver");
            Properties properties = new Properties();
            properties.setProperty(TSDBDriver.PROPERTY_KEY_HOST, "localhost");
            connection = DriverManager.getConnection("jdbc:TAOS://localhost:0/", properties);
            String rawSql = "select * from test.log0601";
//            String[] params = new String[]{"ts", "c1"};
            PreparedStatement pstmt = (TSDBPreparedStatement) connection.prepareStatement(rawSql);
            ResultSet resSet = pstmt.executeQuery();
            while(resSet.next()) {
                for (int i = 1; i <= resSet.getMetaData().getColumnCount(); i++) {
                    System.out.printf("%d: %s \n", i, resSet.getString(i));
                }
            }
            resSet.close();
            pstmt.close();
            connection.close();

        } catch (Exception e) {
            e.printStackTrace();
            if (null != connection) {
                connection.close();
            }
        }
    }
}
