/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord;

import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;
import java.net.InetAddress;
import java.net.ServerSocket;
import java.net.Socket;
import java.net.SocketTimeoutException;
import java.util.concurrent.locks.Condition;
import java.util.concurrent.locks.Lock;
import java.util.concurrent.locks.ReentrantLock;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * This is a mock server for tests in TcpTest to connect its ConcordTcpConnection to. The server handles only one
 * connection at a time.
 */
public class MockTcpServer extends Thread {
    private static Logger logger = LoggerFactory.getLogger(MockTcpServer.class);

    private int port;
    private InetAddress host;
    private Concord.ConcordRequest lastRequest;
    private Concord.ConcordResponse lastResponse;
    private boolean rejectClientVersion;

    private Exception exception;
    private final Lock listenCloseLock;
    private final Condition listen;
    private final Condition close;

    /**
     * Prepare a server. Call `startServer` to actually start the server.
     */
    public MockTcpServer() {
        super("MockTcpServer");

        listenCloseLock = new ReentrantLock();
        listen = listenCloseLock.newCondition();
        close = listenCloseLock.newCondition();
    }

    /**
     * Start the server, and wait for it to be ready.
     */
    public boolean startServer() {
        listenCloseLock.lock();
        try {
            this.start();
            listen.await();
            return exception == null;
        } catch (Exception e) {
            exception = e;
            return false;
        } finally {
            listenCloseLock.unlock();
        }
    }

    /**
     * Ask the server to shutdown, and then wait for it to do so.
     */
    public void stopServer() {
        listenCloseLock.lock();
        try {
            this.interrupt();
            close.await();
        } catch (Exception e) {
            exception = e;
        } finally {
            listenCloseLock.unlock();
        }
    }

    /**
     * Thread.run implementation of this server.
     */
    @Override
    public void run() {
        logger.debug("Mock server thread started");
        ServerSocket socket = null;
        try {
            socket = new ServerSocket(0);
            port = socket.getLocalPort();
            host = socket.getInetAddress();
            listenCloseLock.lock();
            try {
                logger.debug("Mock server thread listening on " + getHostName() + ":" + getPort() + ". Singaling.");
                listen.signal();
            } finally {
                listenCloseLock.unlock();
            }

            while (true) {
                Socket connection = socket.accept();

                // This timeout is just a chance for us to check whether the test is ready for the server to shut down,
                // 100ms keeps us from checking too often, while not delaying test runs too much.
                connection.setSoTimeout(100);

                InputStream is = connection.getInputStream();
                OutputStream os = connection.getOutputStream();
                logger.debug("Mock server accepted connection.");
                handleConnection(is, os);
            }
        } catch (InterruptedException e) {
            // this is expected - it's our shutdown signal
            logger.debug("Mock server interrupted!");
        } catch (Exception e) {
            logger.debug("Mock server exception: " + e);

            // to help test figure out if the mock crashed
            exception = e;

            // to unblock the startup waiter, if we failed setting up the server socket
            listenCloseLock.lock();
            try {
                listen.signal();
            } finally {
                listenCloseLock.unlock();
            }
        } finally {
            logger.debug("Mock server shutting down.");
            if (socket != null) {
                try {
                    socket.close();
                }
                catch (IOException e) {
                    /* nothing we can do */
                }
            }
            listenCloseLock.lock();
            try {
                close.signal();
            } finally {
                listenCloseLock.unlock();
            }
        }
    }

    /**
     * Get the last request received by this server.
     */
    public Concord.ConcordRequest getLastRequest() {
        return lastRequest;
    }

    /**
     * Get the last response sent by this server.
     */
    public Concord.ConcordResponse getLastResponse() {
        return lastResponse;
    }

    /**
     * Get the last exception the server encountered.
     */
    public Exception getLastException() {
        return exception;
    }

    /**
     * Get the port the server is listening on.
     */
    public int getPort() {
        return port;
    }

    /**
     * Get the hostname the server is listening on.
     */
    public String getHostName() {
        return host.getHostName();
    }

    /**
     * Make the server reject the protocol requests.
     */
    public void setRejectClientVersion() {
        rejectClientVersion = true;
    }

    /**
     * Get the number of bytes this server expects to be used to encode the length of a request or response message.
     */
    public int getHeaderSizeBytes() {
        // 2 is the number used by Concord today
        return 2;
    }

    /**
     * Continue reading requests and replying with responses until the streams close.
     */
    private void handleConnection(InputStream is, OutputStream os) throws IOException, InterruptedException {
        // Continue reading & processing until the stream closes, or the test signals we're done.
        while (true) {
            // Read message length
            byte[] b = new byte[getHeaderSizeBytes()];
            read(is, b);

            // Yes, there is an implementation of this decoding in ConcordTcpConnection. The independent implementation
            // here is meant as validation that the other implentation is correct.
            int length = 0;
            for (int i = 0; i < b.length; i++) {
                length = length + (b[i] << (8 * i));
            }

            logger.debug("Mock server read header, expecting " + length + "-byte message.");

            // Read message
            b = new byte[length];
            read(is, b);
            lastRequest = Concord.ConcordRequest.parseFrom(b);

            logger.debug("Mock server read request.");

            // Process & respond
            lastResponse = dispatch(lastRequest);
            b = lastResponse.toByteArray();
            byte[] h = new byte[getHeaderSizeBytes()];
            for (int i = 0; i < h.length; i++) {
                h[i] = (byte) ((b.length >> (8 * i)) & 0xff);
            }
            os.write(h);
            os.write(b);
            logger.debug("Mock server responded with " + b.length + "-byte message.");
        }
    }

    /**
     * Read enough bytes to fill the buffer. Throw an exception if the stream reaches EOF before we can fill the buffer.
     */
    private void read(InputStream is, byte[] buffer) throws IOException, InterruptedException {
        int index = 0;
        while (index < buffer.length) {
            try {
                int count = is.read(buffer, index, buffer.length - index);
                logger.debug("Mock server read " + count + " bytes of " + (buffer.length - index) + " expected");
                if (count == -1) {
                    throw new IOException("Stream closed after " + index
                                          + " of " + buffer.length + " bytes were read.");
                }
                index += count;
            } catch (SocketTimeoutException ste) {
                if (this.isInterrupted()) {
                    throw new InterruptedException("Server shutdown requested");
                }
            }
        }
    }

    /**
     * Get the string that the server will return for an invalid EthRequest.
     */
    public String getEthErrorResponseString() {
        return "Unknown request type";
    }

    /**
     * Figure out what response we should give to a given request.
     */
    private Concord.ConcordResponse dispatch(Concord.ConcordRequest request) {
        Concord.ConcordResponse.Builder response = Concord.ConcordResponse.newBuilder();

        if (request.hasProtocolRequest()) {
            if (rejectClientVersion) {
                logger.debug("Mock server received ProtocolRequest, replying with unknown client error");
                response.addErrorResponse(
                    Concord.ErrorResponse.newBuilder().setDescription("Unknown client version"));
            } else {
                logger.debug("Mock server received ProtocolRequest, replying with ProtocolResponse");
                response.setProtocolResponse(
                    Concord.ProtocolResponse.newBuilder().setServerVersion(
                        request.getProtocolRequest().getClientVersion()));
            }
        } else if (request.hasTestRequest()) {
            logger.debug("Mock server received TestRequest, replying with echo");
            response.setTestResponse(
                Concord.TestResponse.newBuilder().setEchoBytes(
                    request.getTestRequest().getEchoBytes()));
        } else {
            logger.debug("Mock server received other message, replying with unknown message error");
            response.addErrorResponse(
                Concord.ErrorResponse.newBuilder().setDescription(getEthErrorResponseString()));
        }

        return response.build();
    }
}
