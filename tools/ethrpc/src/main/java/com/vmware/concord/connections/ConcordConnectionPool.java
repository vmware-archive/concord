/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.connections;

import java.io.IOException;
import java.util.concurrent.ArrayBlockingQueue;
import java.util.concurrent.TimeUnit;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicInteger;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.springframework.stereotype.Component;

import com.vmware.concord.ConcordTcpConnection;
import com.vmware.concord.IConcordConnection;

/**
 * Connection pool for a specific Blockchain.
 */
@Component
public class ConcordConnectionPool {
    // Instantiate the instance of this class
    private static Logger log = LogManager.getLogger(ConcordConnectionPool.class);
    private AtomicInteger connectionCount;
    // initialized with fairness = true, longest waiting threads
    // are served first
    private ArrayBlockingQueue<IConcordConnection> pool;
    private ConcordProperties config;
    private AtomicBoolean initialized;
    // max wait time for pool to return connection
    private int waitTimeout;
    // pool starts at this size
    private int minPoolSize;
    // pool can grow up to this size. currently no cleaning routine is
    // implemented, TODO
    private int maxPoolSize;
    private ConcordConnectionFactory factory;

    /**
     * Initializes local variables.
     */
    public ConcordConnectionPool() {
        initialized = new AtomicBoolean(false);
        connectionCount = new AtomicInteger(0);
    }

    /**
     * Creates a new TCP connection with Concord.
     *
     * @return the connection
     */
    private IConcordConnection createConnection() {
        log.trace("createConnection enter");
        try {
            // increment first, so that all errors can decrement
            int c = connectionCount.incrementAndGet();
            if (c <= maxPoolSize) {
                IConcordConnection res = factory.create();
                log.info("new connection created, active connections: " + c);
                return res;
            } else {
                log.debug("pool size at maximum");
                connectionCount.decrementAndGet();
                return null;
            }
        } catch (Exception e) {
            // all exceptions are failures - undo the increment, since we failed
            connectionCount.decrementAndGet();
            log.error("createConnection", e);
            return null;
        } finally {
            log.trace("createConnection exit");
        }
    }

    /**
     * Closes a single connection instance with Concord.
     *
     * @param conn Connection
     */
    private void closeConnection(IConcordConnection conn) {
        log.trace("closeConnection enter");
        try {
            if (conn != null) {
                conn.close();
                int c = connectionCount.decrementAndGet();
                log.debug("connection closed, active connections: " + c);
                log.info("broken connection closed");

                // attempt to replace the broken connection
                if (c < minPoolSize && initialized.get()) {
                    putConnection(createConnection());
                }
            }
        } catch (Exception e) {
            log.error("closeConnection", e);
        } finally {
            log.trace("closeConnection exit");
        }
    }

    /**
     * Removes a connection from the connection pool data structure, checks it, and returns it.
     */
    public IConcordConnection getConnection() throws IOException, IllegalStateException, InterruptedException {
        log.trace("getConnection enter");

        if (!initialized.get()) {
            throw new IllegalStateException("getConnection, pool not initialized");
        }

        boolean first = true;
        long start = System.currentTimeMillis();
        while (System.currentTimeMillis() - start < waitTimeout) {
            IConcordConnection conn;

            if (first) {
                // don't wait on the first poll; if the pool is empty, jump
                // immediately to checking if a new connection can be added
                first = false;
                conn = pool.poll();

                if (conn == null) {
                    // this may fail if there are _maxPoolSize connections already
                    conn = createConnection();
                }
            } else {
                // if this is not our first wait, then we weren't allowed to
                // increase the pool size, so we just have to wait for a connection
                conn = pool.poll(waitTimeout, TimeUnit.MILLISECONDS);
            }

            if (conn != null) {
                boolean res = conn.check();
                if (!res) {
                    log.error("Failed to check connection");
                    closeConnection(conn);
                    // see if we can get another connection
                    continue;
                }

                log.trace("getConnection exit");
                return conn;
            }
        }

        log.trace("getConnection exit");
        return null;
    }

    /**
     * Adds a connection to the connection pool data structure.
     */
    public void putConnection(IConcordConnection conn) throws IllegalStateException, NullPointerException {
        log.trace("putConnection enter");

        if (!initialized.get()) {
            throw new IllegalStateException("returnConnection, pool not initialized");
        }

        // cannot be null in normal flow
        if (conn == null) {
            log.fatal("putConnection, conn is null");
        } else {
            boolean res = pool.offer(conn);

            // cannot fail in normal flow
            if (!res) {
                ((ConcordTcpConnection) conn).close();
                log.fatal("putConnection, pool at maximum");
            }
        }

        log.trace("putConnection exit");
    }

    /**
     * Reads connection pool related configurations.
     */
    public ConcordConnectionPool initialize(ConcordProperties config, ConcordConnectionFactory factory)
            throws IOException {
        if (initialized.compareAndSet(false, true)) {
            this.config = config;
            this.factory = factory;
            waitTimeout = config.getConnectionPoolWaitTimeoutMs();
            minPoolSize = config.getConnectionPoolSize();
            int poolFactor = config.getConnectionPoolFactor();
            maxPoolSize = minPoolSize * poolFactor;

            pool = new ArrayBlockingQueue<IConcordConnection>(maxPoolSize, true);
            for (int i = 0; i < minPoolSize; i++) {
                putConnection(createConnection());
            }

            log.info(String.format("ConcordConnectionPool initialized with %d connections", connectionCount.get()));
        }
        return this;
    }

    /**
     * Closes all connections in the connection pool.
     */
    public void closeAll() {
        initialized.set(false);
        for (IConcordConnection conn : pool) {
            closeConnection(conn);
        }

        pool.clear();
        connectionCount.set(0);
    }

    /**
     * Returns total number of connections.
     */
    public int getTotalConnections() {
        if (!initialized.get()) {
            throw new IllegalStateException("returnConnection, pool not initialized");
        }
        return connectionCount.get();
    }
}
