/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord;

import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.Test;

import com.google.protobuf.InvalidProtocolBufferException;

import com.vmware.concord.Concord.ConcordRequest;
import com.vmware.concord.Concord.ProtocolRequest;

/**
 * This is mostly a sanity test, to make sure that protobuf compilation happens correctly.
 */
class ProtoTest {
    /**
     * Create a very simple message. Encode it, decode it, and check that the result matches. This is not a rigorous
     * test, but will hopefully at least prove that we compiled the .proto correctly.
     */
    @Test
    void testRoundtripEncoding() throws InvalidProtocolBufferException {
        // our expected value
        final int clientVersion = 42;

        // Construct a message
        ProtocolRequest.Builder protoReqB = ProtocolRequest.newBuilder();
        protoReqB.setClientVersion(clientVersion);
        ConcordRequest.Builder concReqB = ConcordRequest.newBuilder();
        concReqB.setProtocolRequest(protoReqB);

        // Roundtrip encode/decode
        byte[] encoded = concReqB.build().toByteArray();
        ConcordRequest concReq = ConcordRequest.parseFrom(encoded);

        // Check for a match
        Assertions.assertTrue(concReq.hasProtocolRequest());
        Assertions.assertTrue(concReq.getProtocolRequest().hasClientVersion());
        Assertions.assertEquals(clientVersion, concReq.getProtocolRequest().getClientVersion());
    }
}
