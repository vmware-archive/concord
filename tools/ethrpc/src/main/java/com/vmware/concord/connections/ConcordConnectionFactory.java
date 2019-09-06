/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.connections;

import java.io.IOException;
import java.util.ArrayList;
import java.util.concurrent.atomic.AtomicLong;

import com.vmware.concord.ConcordTcpConnection;
import com.vmware.concord.IConcordConnection;

/**
 * Factory to create ConcordConnections of the proper type.
 */
public final class ConcordConnectionFactory {
    private ConnectionType type;
    private ConcordProperties config;
    private ArrayList<Authority> concordList;
    private AtomicLong nextAuthority;

    /**
     * Connection types supported.
     */
    public enum ConnectionType {
        TCP, Mock
    }

    /**
     * Connection Factory for the proper type.
     */
    public ConcordConnectionFactory(ConnectionType type, ConcordProperties config) {
        this.type = type;
        this.config = config;
        nextAuthority = new AtomicLong();

        // Read list of concords from config
        concordList = new ArrayList<>();
        String authorities = config.getConcordAuthorities();
        String[] authorityList = authorities.split(",");
        for (String authority : authorityList) {
            String[] group = authority.split(":");
            Authority a = new Authority(group[0], Integer.parseInt(group[1]));
            concordList.add(a);
        }
    }

    /**
     * Create the connection.
     */
    public IConcordConnection create() throws IOException, UnsupportedOperationException {
        switch (type) {
            case TCP:
                // Select an Concord instance to connect with in a round robin fashion
                int chosenAuthority = (int) nextAuthority.getAndIncrement() % concordList.size();
                Authority concordInstance = concordList.get(chosenAuthority);
                ConcordTcpConnection connection =
                    new ConcordTcpConnection(config.getReceiveTimeoutMs(), config.getReceiveHeaderSizeBytes(),
                                             concordInstance.getHost(), concordInstance.getPort());
                return connection;
            case Mock:
                return new MockConnection();
            default:
                throw new UnsupportedOperationException("type not supported" + type);
        }
    }

    private class Authority {
        String host;
        int port;

        Authority(String h, int p) {
            host = h;
            port = p;
        }

        String getHost() {
            return host;
        }

        int getPort() {
            return port;
        }
    }
}
