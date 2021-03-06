package org.gprom.jdbc.driver;

import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.SQLWarning;
import java.sql.Statement;
import java.util.Arrays;
import java.util.HashSet;
import java.util.Set;
import java.util.regex.Pattern;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.gprom.jdbc.driver.GProMJDBCUtil.BackendType;
import org.gprom.jdbc.jna.GProMWrapper;

public class GProMStatement implements GProMStatementInterface {
	// static fields
	static Logger log = LogManager.getLogger(GProMStatement.class);

	private static final String[] gpromKeywords = { "PROVENANCE", 
			"BASERELATION",
			"SEQUENCED",
			"TEMPORAL",
			"REENACT",
			"UNCERTAIN"
			};
	private static final String[] utilityKeywords = { "DROP"};
	private static final Set<String> keywordSet;
	private static final Set<String> utilityKeywordSet;
	
	static GProMWrapper w = GProMWrapper.inst;
	static final Pattern whitespaceSplitter = Pattern.compile("\\s+"); 
	
	// intitialize keyword sets
	static {
		keywordSet = new HashSet<String> ();
		utilityKeywordSet = new HashSet<String> ();
		
		keywordSet.addAll(Arrays.asList(gpromKeywords));
		utilityKeywordSet.addAll(Arrays.asList(utilityKeywords));
	}
	
	// instance field
	private BackendType backend;
	protected Statement stat;

	public GProMStatement(Statement stat, BackendType backend){
		this.stat = stat;	
		this.backend = backend;
	}
	
	/**
	 * 
	 */
	public boolean checkForGProMKeywords(String sqlQuery) {
		
		for (String token : whitespaceSplitter.split(sqlQuery)) 
		{
			if (keywordSet.contains(token))
				return true;
		}
		return false;
		//TODO use the utility keyword trick?
	}

	public ResultSet executeGProMQuery(String sqlQuery) throws SQLException{
		if(checkForGProMKeywords(sqlQuery)){
			sqlQuery = w.gpromRewriteQuery(sqlQuery);
		}
		return executeQuery(sqlQuery);
	}
	
	public ResultSet executeBackendQuery(String sqlQuery) throws SQLException {
		return  stat.executeQuery(sqlQuery);
	}

	public void addBatch(String sql) throws SQLException {
		stat.addBatch(sql);
	}

	public void cancel() throws SQLException {
		stat.cancel();
	}

	public void clearBatch() throws SQLException {
		stat.clearBatch();
	}

	public void clearWarnings() throws SQLException {
		stat.clearWarnings();
	}

	public void close() throws SQLException {
		stat.close();
	}

	public boolean execute(String sql) throws SQLException {
		if(checkForGProMKeywords(sql)){
			sql = GProMWrapper.inst.gpromRewriteQuery(sql);
		}
		return stat.execute(sql);
	}

	public boolean execute(String sql, int autoGeneratedKeys)
			throws SQLException {
		if(checkForGProMKeywords(sql)){
			sql = GProMWrapper.inst.gpromRewriteQuery(sql);
		}
		return stat.execute(sql, autoGeneratedKeys);
	}

	public boolean execute(String sql, int[] columnIndexes) throws SQLException {
		if(checkForGProMKeywords(sql)){
			sql = GProMWrapper.inst.gpromRewriteQuery(sql);
		}
		return stat.execute(sql, columnIndexes);
	}

	public boolean execute(String sql, String[] columnNames)
			throws SQLException {
		if(checkForGProMKeywords(sql)){
			sql = GProMWrapper.inst.gpromRewriteQuery(sql);
		}
		return stat.execute(sql, columnNames);
	}

	public int[] executeBatch() throws SQLException {
		return stat.executeBatch();
	}

	public ResultSet executeQuery(String sql) throws SQLException {
//		if(checkForGProMKeywords(sql)){
			sql =  GProMWrapper.inst.gpromRewriteQuery(sql);
//		}
		return stat.executeQuery(sql);
	}

	public int executeUpdate(String sql) throws SQLException {
		return stat.executeUpdate(sql);
	}

	public int executeUpdate(String sql, int autoGeneratedKeys)
			throws SQLException {
		return stat.executeUpdate(sql, autoGeneratedKeys);
	}

	public int executeUpdate(String sql, int[] columnIndexes)
			throws SQLException {
		return stat.executeUpdate(sql, columnIndexes);
	}

	public int executeUpdate(String sql, String[] columnNames)
			throws SQLException {
		return stat.executeUpdate(sql, columnNames);
	}

	public Connection getConnection() throws SQLException {
		return stat.getConnection();
	}

	public int getFetchDirection() throws SQLException {
		return stat.getFetchDirection();
	}

	public int getFetchSize() throws SQLException {
		return stat.getFetchSize();
	}

	public ResultSet getGeneratedKeys() throws SQLException {
		return stat.getGeneratedKeys();
	}

	public int getMaxFieldSize() throws SQLException {
		return stat.getMaxFieldSize();
	}

	public int getMaxRows() throws SQLException {
		return stat.getMaxRows();
	}

	public boolean getMoreResults() throws SQLException {
		return stat.getMoreResults();
	}

	public boolean getMoreResults(int current) throws SQLException {
		return stat.getMoreResults();
	}

	public int getQueryTimeout() throws SQLException {
		return stat.getQueryTimeout();
	}

	public ResultSet getResultSet() throws SQLException {
		return stat.getResultSet();
	}

	public int getResultSetConcurrency() throws SQLException {
		return stat.getResultSetConcurrency();
	}

	public int getResultSetHoldability() throws SQLException {
		return stat.getResultSetHoldability();
	}

	public int getResultSetType() throws SQLException {
		return stat.getResultSetType();
	}

	public int getUpdateCount() throws SQLException {
		return stat.getUpdateCount();
	}

	public SQLWarning getWarnings() throws SQLException {
		return stat.getWarnings();
	}

	public void setCursorName(String name) throws SQLException {
		stat.setCursorName(name);
	}

	public void setEscapeProcessing(boolean enable) throws SQLException {
		stat.setEscapeProcessing(enable);
	}

	public void setFetchDirection(int direction) throws SQLException {
		stat.setFetchDirection(direction);
	}

	public void setFetchSize(int rows) throws SQLException {
		stat.setFetchSize(rows);
	}

	public void setMaxFieldSize(int max) throws SQLException {
		stat.setMaxFieldSize(max);
	}

	public void setMaxRows(int max) throws SQLException {
		stat.setMaxRows(max);
	}

	public void setQueryTimeout(int seconds) throws SQLException {
		stat.setQueryTimeout(seconds);
	}
	
	public BackendType getDatabaseType() {
		return backend; 
	}

	public boolean isClosed() throws SQLException {
		return stat.isClosed();
	}

	public boolean isPoolable() throws SQLException {
		return stat.isPoolable();
	}

	public void setPoolable(boolean arg0) throws SQLException {
		stat.isPoolable();
	}

	public boolean isWrapperFor(Class<?> arg0) throws SQLException {
		return stat.isWrapperFor(arg0);
	}

	public <T> T unwrap(Class<T> arg0) throws SQLException {
		return stat.unwrap(arg0);
	}

	/* (non-Javadoc)
	 * @see java.sql.Statement#closeOnCompletion()
	 */
	@Override
	public void closeOnCompletion() throws SQLException {
		// TODO Auto-generated method stub
		
	}

	/* (non-Javadoc)
	 * @see java.sql.Statement#isCloseOnCompletion()
	 */
	@Override
	public boolean isCloseOnCompletion() throws SQLException {
		// TODO Auto-generated method stub
		return false;
	}


	
/*NEW Methods---------------------------------------------------
 	public boolean isClosed() throws SQLException {
		return stat.isClosed();
	}

	public boolean isPoolable() throws SQLException {
		return stat.isPoolable();
	}
	
 	public void setPoolable(boolean poolable) throws SQLException {
		stat.setPoolable(poolable);
	}
 	public boolean isWrapperFor(Class<?> iface) throws SQLException {
		return stat.isWrapperFor(iface);
	}

	public <T> T unwrap(Class<T> iface) throws SQLException {
		return stat.unwrap(iface);
	}
 */
}
