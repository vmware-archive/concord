/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord;

import java.io.IOException;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

import com.google.protobuf.InvalidProtocolBufferException;

/**
 * Some helper functions dealing with protobuf.
 */
public class ConcordHelper {

    private static Logger log = LoggerFactory.getLogger(ConcordHelper.class);

    /**
     * Sends a Google Protocol Buffer request to Concord. Concord expects two bytes signifying the size of the request
     * before the actual request.
     */
    public static boolean sendToConcord(Concord.ConcordRequest request, IConcordConnection conn)
            throws IOException {
        // here specifically, request.toString() it time consuming,
        // so checking level enabled can gain performance
        if (log.isTraceEnabled()) {
            log.trace(String.format("Sending request to Concord : %s %s", System.lineSeparator(), request));
        }

        // Write requests over the output stream.
        conn.send(request.toByteArray());

        // TODO: now that send throws IOException, there's no reason to return a boolean here
        return true;
    }

    /**
     * Receives a Google Protocol Buffer response from Concord. Concord sends two bytes signifying the size of the
     * response before the actual response.
     **/
    public static Concord.ConcordResponse receiveFromConcord(IConcordConnection conn) {
        try {
            byte[] data = conn.receive();
            if (data == null) {
                return null;
            }

            // Convert read bytes into a Protocol Buffer object.
            Concord.ConcordResponse concordResponse;
            try {
                concordResponse = Concord.ConcordResponse.parseFrom(data);
            } catch (InvalidProtocolBufferException e) {
                log.error("Error in parsing Concord's response");
                throw new InvalidProtocolBufferException(e.getMessage());
            }

            return concordResponse;
        } catch (Exception e) {
            log.error("receiveFromConcord", e);
            conn.close();
            return null;
        }
    }
}
