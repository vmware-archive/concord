/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord;

import java.io.IOException;
import java.util.Random;

import org.junit.jupiter.api.AfterEach;
import org.junit.jupiter.api.Assertions;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.Test;

import com.google.protobuf.ByteString;

/**
 * Basic tests of ConcordTcpConnection. This uses a mock server, so that we can deal with just our client
 * side. Integration with the server side is tested by integration tests.
 */
class TcpTest {

    private MockTcpServer server;

    // This is a local echo server, it should be fast.
    private static final int receiveTimeoutMs = 1000;

    /**
     * Start and wait for the mock server.
     */
    @BeforeEach
    void startServer() {
        server = new MockTcpServer();
        if (!server.startServer()) {
            Exception e = server.getLastException();
            e.printStackTrace();
            Assertions.fail("Mock server did not start: " + e);
        }
    }

    /**
     * Tear down the mock server.
     */
    @AfterEach
    void stopServer() {
        if (server != null) {
            server.stopServer();
            server = null;
        }
    }

    /**
     * This is the very basic, expected configuration, happy path initial protocol setup. It opens a connection to the
     * mock, sends the initial protocol-check message, and verifies both that the client decoded a successful response,
     * and that the mock server actually received the request. If this test breaks, it means either something simple
     * broke, or some default configuration changed, like the protocol version.
     */
    @Test
    void testConnectionCheckSuccess() throws IOException {
        // Open connection, and make sure the client side believes it checks out.
        ConcordTcpConnection conn = new ConcordTcpConnection(receiveTimeoutMs, server.getHeaderSizeBytes(),
                                                             server.getHostName(), server.getPort());
        Assertions.assertTrue(conn.check());

        // Then make sure that the client side isn't blindly returning true, and that the server did actually receive
        // the check request, and sent a check response. The client could still be lying, but we have some indication
        // that it didn't have to be.
        Concord.ConcordRequest req = server.getLastRequest();
        Assertions.assertNotNull(req);
        Assertions.assertTrue(req.hasProtocolRequest());

        Concord.ConcordResponse resp = server.getLastResponse();
        Assertions.assertNotNull(resp);
        Assertions.assertTrue(resp.hasProtocolResponse());
    }

    /**
     * Test that the client handles rejection from the server correctly. This is the other half of the check for lies
     * from above. The server is going to respond that the client version is not supported, so the client should report
     * that the connection did not check out.
     */
    @Test
    void testConnectionCheckRejection() throws IOException {
        server.setRejectClientVersion();

        // Open connection, and make sure the client side believes it does not check out.
        ConcordTcpConnection conn = new ConcordTcpConnection(receiveTimeoutMs, server.getHeaderSizeBytes(),
                                                             server.getHostName(), server.getPort());
        Assertions.assertFalse(conn.check());
    }

    /**
     * Test actual message data, and thus ConcordHelper. Use the echo feature of TestRequest to send random data.
     */
    @Test
    void testMessageData() throws IOException {
        ConcordTcpConnection conn = new ConcordTcpConnection(receiveTimeoutMs, server.getHeaderSizeBytes(),
                                                             server.getHostName(), server.getPort());
        Assertions.assertTrue(conn.check());

        // Get some data to echo. Randomness is just a small hurdle to fight "coding to the test".
        byte[] echoString = new byte[64];
        new Random().nextBytes(echoString);
        final ByteString echoByteString = ByteString.copyFrom(echoString);

        final Concord.ConcordRequest request = Concord.ConcordRequest.newBuilder()
            .setTestRequest(Concord.TestRequest.newBuilder().setEchoBytes(echoByteString))
            .build();

        // Send and receive the data. Make sure it's the same data.
        ConcordHelper.sendToConcord(request, conn);
        final Concord.ConcordResponse response = ConcordHelper.receiveFromConcord(conn);
        Assertions.assertNotNull(response);
        Assertions.assertTrue(response.hasTestResponse());
        Assertions.assertTrue(response.getTestResponse().hasEcho());
        Assertions.assertEquals(echoByteString, response.getTestResponse().getEchoBytes());

        // Make sure the server really saw our request, and that it really sent our response.
        final Concord.ConcordRequest serverRequest = server.getLastRequest();
        Assertions.assertEquals(request, serverRequest);
        final Concord.ConcordResponse serverResponse = server.getLastResponse();
        Assertions.assertEquals(response, serverResponse);
    }

    /**
     * Test actual error message, and thus ConcordHelper. The rejection error message was hidden from our test in
     * testConnectionCheckRejection. Send an invalid EthRequest to make an error happen.
     */
    @Test
    void testErrorMessage() throws IOException {
        ConcordTcpConnection conn = new ConcordTcpConnection(receiveTimeoutMs, server.getHeaderSizeBytes(),
                                                             server.getHostName(), server.getPort());
        Assertions.assertTrue(conn.check());

        // An EthRequest with no method is invalid.
        final Concord.ConcordRequest request = Concord.ConcordRequest.newBuilder()
            .addEthRequest(Concord.EthRequest.newBuilder())
            .build();

        // Send and receive the data. Make sure it's an error.
        ConcordHelper.sendToConcord(request, conn);
        final Concord.ConcordResponse response = ConcordHelper.receiveFromConcord(conn);
        Assertions.assertNotNull(response);
        Assertions.assertEquals(0, response.getEthResponseCount());
        Assertions.assertEquals(1, response.getErrorResponseCount());
        Assertions.assertTrue(response.getErrorResponse(0).hasDescription());
        Assertions.assertEquals(server.getEthErrorResponseString(), response.getErrorResponse(0).getDescription());

        // Make sure the server really saw our request, and that it really sent our response.
        final Concord.ConcordRequest serverRequest = server.getLastRequest();
        Assertions.assertEquals(request, serverRequest);
        final Concord.ConcordResponse serverResponse = server.getLastResponse();
        Assertions.assertEquals(response, serverResponse);
    }

    /**
     * Concord doesn't like empty messages. It closes connections, and this makes EthRPC unhappy sometimes. Checks have
     * been put in the client side to prevent empty messages from going to Concord. This test makes sure those checks
     * are in place.
     */
    @Test
    void testEmptyMessage() throws IOException {
        ConcordTcpConnection conn = new ConcordTcpConnection(receiveTimeoutMs, server.getHeaderSizeBytes(),
                                                             server.getHostName(), server.getPort());
        Assertions.assertTrue(conn.check());

        // An empty request
        final Concord.ConcordRequest request = Concord.ConcordRequest.newBuilder().build();

        try {
            ConcordHelper.sendToConcord(request, conn);
            Assertions.fail("Sending an empty message should have thrown an exception.");
        } catch (IOException e) {
            // This is the expected path. The message may change - please to change it to match ConcordTcpConnection.
            // It is checked here just to make sure that we didn't get some other IOException.
            Assertions.assertEquals("Empty request", e.getMessage());
        }
    }
}
