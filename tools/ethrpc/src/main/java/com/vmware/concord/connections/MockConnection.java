/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.connections;

import java.io.IOException;

import com.vmware.concord.Concord;
import com.vmware.concord.IConcordConnection;

/**
 * Mock Concord connection, used for testing.
 */
public class MockConnection implements IConcordConnection {
    // Pre-allocated check response
    private static Concord.ProtocolResponse protocolResponse =
        Concord.ProtocolResponse.newBuilder().setServerVersion(1).build();
    private static Concord.ConcordResponse concordResponse =
        Concord.ConcordResponse.newBuilder().setProtocolResponse(protocolResponse).build();

    public MockConnection() {
    }

    @Override
    public void close() {

    }

    @Override
    public void send(byte[] msg) throws IOException {
        // MockConnection just throws the request away
    }

    @Override
    /**
     * this method should be extended to remember last message sent and to return corresponding response. Currently it
     * returns hardcoded ConcordProtocolResponse message
     */
    public byte[] receive() throws IOException {
        return concordResponse.toByteArray();
    }

    @Override
    public boolean check() {
        return true;
    }

}
