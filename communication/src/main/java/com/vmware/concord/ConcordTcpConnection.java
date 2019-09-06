/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord;

import java.io.IOException;
import java.net.Socket;
import java.net.UnknownHostException;
import java.nio.ByteBuffer;
import java.nio.ByteOrder;
import java.util.concurrent.atomic.AtomicBoolean;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

/**
 * TCP implementation of Concord Connections.
 */
public final class ConcordTcpConnection implements IConcordConnection {
    private static Logger logger = LoggerFactory.getLogger(ConcordTcpConnection.class);

    // Connection state
    private Socket socket;
    private AtomicBoolean disposed;

    // Connection properties
    private final int receiveTimeoutMs;
    private final int receiveHeaderSizeBytes;
    private final int maxMessageSizeBytes;

    // Pre-allocated check message
    private static Concord.ProtocolRequest _protocolRequestMsg =
            Concord.ProtocolRequest.newBuilder().setClientVersion(1).build();
    private static Concord.ConcordRequest _concordRequest =
            Concord.ConcordRequest.newBuilder().setProtocolRequest(_protocolRequestMsg).build();

    /**
     * Sets up a TCP connection with Concord.
     *
     */
    public ConcordTcpConnection(int receiveTimeoutMs, int receiveHeaderSizeBytes, String host, int port)
        throws IOException {
        this.receiveTimeoutMs = receiveTimeoutMs;
        this.receiveHeaderSizeBytes = receiveHeaderSizeBytes;

        /* This limit has been temporarily lowered to 60000 (from (1 <<
         * (receivedHeaderSizeBytes * 8) - 1)) as a quick fix (VB-962) to
         * prevent Concord from crashing when large transactions are sent, as
         * Concord currently does not handle such cases correctly. This limit
         * should be restored to its original value once the underlying issues
         * have actually been fixed.
         */
        maxMessageSizeBytes = 60000; // (1 << (receiveHeaderSizeBytes * 8)) - 1;

        disposed = new AtomicBoolean(false);

        // Create the TCP connection and input and output streams
        try {
            socket = new Socket(host, port);
            socket.setTcpNoDelay(true);
            socket.setSoTimeout(receiveTimeoutMs);
        } catch (UnknownHostException e) {
            logger.error("Error creating TCP connection with Concord. Host= " + host + ", port= " + port);
            throw e;
        } catch (IOException e) {
            logger.error("Error creating input/output stream with Concord. Host= " + host + ", port= " + port);
            throw e;
        }

        logger.debug("Socket connection with Concord created");
    }

    /**
     * Closes the TCP connection.
     */
    @Override
    public void close() {
        if (disposed.get()) {
            return;
        }

        if (socket != null && !socket.isClosed()) {
            try {
                socket.close();
            } catch (IOException e) {
                logger.error("Error in closing TCP socket");
            } finally {
                disposed.set(true);
            }
        }
    }

    /**
     * Close the connection before garbage collection.
     * Should be using closable for this.
     */
    @SuppressWarnings("checkstyle:NoFinalizer")
    @Override
    protected void finalize() throws Throwable {
        logger.info("connection disposed");
        try {
            if (!disposed.get()) {
                close();
            }
        } finally {
            super.finalize();
        }
    }

    /**
     * Reads responses from Concord. Concord sends the size of the response before the actual response
     */
    @Override
    public byte[] receive() throws IOException {
        java.io.InputStream is = socket.getInputStream();
        long start = System.currentTimeMillis();
        int msgSize = -1;
        byte[] msgSizeBuf = new byte[receiveHeaderSizeBytes];
        int msgSizeOffset = 0;
        byte[] result = null;
        int resultOffset = 0;

        while (System.currentTimeMillis() - start < receiveTimeoutMs) {
            // we need to read at least the header before we can do anything
            if (msgSizeOffset < receiveHeaderSizeBytes) {
                int count = is.read(msgSizeBuf, msgSizeOffset, receiveHeaderSizeBytes - msgSizeOffset);
                if (count < 0) {
                    logger.error("No bytes read from concord");
                    break;
                } else {
                    msgSizeOffset += count;
                }
            }

            // we have the header - find out how big the body is
            if (msgSizeOffset == receiveHeaderSizeBytes && msgSize < 0) {
                // msgSize is sent as an unsigned 16-bit integer
                msgSize =
                    Short.toUnsignedInt(ByteBuffer.wrap(msgSizeBuf).order(ByteOrder.LITTLE_ENDIAN).getShort());

                result = new byte[msgSize];
            }

            // now we can read the body
            if (result != null) {
                int count = is.read(result, resultOffset, msgSize - resultOffset);
                if (count < 0) {
                    logger.error("No bytes read from concord");
                    break;
                } else {
                    resultOffset += count;
                }

                // stop when we've reached the end
                if (resultOffset == msgSize) {
                    break;
                }
            }
        }

        // if we didn't read the whole message, consider the stream corrupt and close it
        if (resultOffset != msgSize) {
            logger.error("Failed to receive message (" + resultOffset + " != " + msgSize + "). Closing socket.");
            close();
            return null;
        }

        return result;
    }

    /**
     * Converts an int into two bytes.
     *
     * @param value that needs to be converted
     * @param size size of returned byte array
     * @return A byte array containing two bytes.
     */
    private static byte[] intToSizeBytes(int value, int size) {
        byte[] bytes = ByteBuffer.allocate(size).order(ByteOrder.LITTLE_ENDIAN).putShort((short) value).array();
        return bytes;
    }

    /**
     * Sends data to Concord over the connection.
     */
    @Override
    public void send(byte[] msg) throws IOException {
        if (msg.length > maxMessageSizeBytes) {
            throw new IOException("Request too large: " + msg.length);
        } else if (msg.length == 0) {
            logger.error("Do not send empty messages to concord.");
            throw new IOException("Empty request");
        }

        // TODO: we should be able to call write twice instead of reallocating and copying to a new buffer
        ByteBuffer buf = ByteBuffer.allocate(receiveHeaderSizeBytes + msg.length);
        buf.put(intToSizeBytes(msg.length, receiveHeaderSizeBytes));
        buf.put(msg);
        socket.getOutputStream().write(buf.array());
    }

    @Override
    public boolean check() {
        try {
            logger.trace("check enter");
            boolean res = ConcordHelper.sendToConcord(_concordRequest, this);
            if (res) {
                Concord.ConcordResponse resp = ConcordHelper.receiveFromConcord(this);
                if (resp != null && resp.hasProtocolResponse()) {
                    logger.debug("check, got server version: " + resp.getProtocolResponse().getServerVersion());
                    return true;
                }
            }

            return false;
        } catch (IOException e) {
            logger.error("check", e);
            return false;
        } finally {
            logger.trace("check exit");
        }
    }
}
