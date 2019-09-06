/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import java.io.IOException;
import java.io.PrintWriter;
import java.io.StringWriter;
import java.util.stream.Collectors;

import javax.servlet.http.HttpServletRequest;

import org.bouncycastle.jcajce.provider.digest.Keccak;
import org.bouncycastle.util.encoders.Hex;
import org.json.simple.JSONObject;

import com.google.protobuf.ByteString;

/**
 * Helper functions for Ethereum APIs.
 */
public class ApiHelper {

    /**
     * Converts a hex string into a binary string.
     *
     * @param param A hex string
     * @return A ByteString object
     */
    public static ByteString hexStringToBinary(String param) throws HexParseException {
        // Param should strictly be a hex string
        if (param == null || param.trim().length() < 1) {
            return null;
        }
        String curr = param.trim();

        if (curr.length() % 2 != 0) {
            throw new HexParseException("Hex string has odd nibble count.");
        }

        if (curr.equals("0x")) {
            return ByteString.EMPTY;
        }

        int adjust = (curr.charAt(0) == '0' && curr.charAt(1) == 'x') ? 2 : 0;
        int resultSize = (curr.length() - adjust) / 2;

        byte[] resultBytes = new byte[resultSize];

        if (resultSize > 0) {
            for (int i = 0; i < resultSize; i++) {
                resultBytes[i] =
                        (byte) ((hexVal(curr.charAt(i * 2 + adjust)) << 4) | hexVal(curr.charAt(i * 2 + adjust + 1)));
            }
        }
        return ByteString.copyFrom(resultBytes);
    }

    /**
     * Convert a big-endian byte string to its 64-bit integer value. Throws exception if there are more than eight
     * bytes.
     */
    public static long bytesToLong(ByteString bytes) throws HexParseException {
        if (bytes.size() > 8) {
            throw new HexParseException("Value to large for long");
        }

        long result = 0;
        ByteString.ByteIterator i = bytes.iterator();
        while (i.hasNext()) {
            result = (result << 8) | (0xff & i.nextByte());
        }
        return result;
    }

    /**
     * Converts a hex character into its corresponding numerical value.
     */
    private static char hexVal(char c) throws HexParseException {
        if (c >= '0' && c <= '9') {
            return (char) (c - '0');
        } else if (c >= 'a' && c <= 'f') {
            return (char) (10 + c - 'a');
        } else if (c >= 'A' && c <= 'F') {
            return (char) (10 + c - 'A');
        } else {
            throw new HexParseException("Invalid hex character");
        }
    }

    /**
     * Converts a bytestring to a hex string.
     *
     * @param binary Binary string
     * @param dropLeadingZeros If true, do not include "0" characters in the output for zero bytes at the start of the
     *        input. (e.g. if binary is [0, 0, 1, 2, 3], output "0x10203" instead of "0x0000010203").
     * @return A string starting with "0x", followed by the hex representation of the 'binary' parameter.
     */
    public static String binaryStringToHex(ByteString binary, boolean dropLeadingZeros) {
        byte[] resultBytes = binary.toByteArray();
        StringBuilder sb = new StringBuilder("0x");

        boolean first = true;

        for (byte b : resultBytes) {
            if (first && dropLeadingZeros) {
                if (b != 0) {
                    first = false;

                    // Don't force two characters, if the first byte only needs one.
                    sb.append(String.format("%x", b));
                }
            } else {
                sb.append(String.format("%02x", b));
            }
        }

        if (dropLeadingZeros && first) {
            // We only drop leading zeros for QUANTITY fields, and "0x" is not
            // allowed there. It must be "0x0" at least.
            sb.append("0");
        }

        String result = sb.toString();
        return result;
    }

    /**
     * Wrapper around binaryStringToHex that never drops leading zeros.
     */
    public static String binaryStringToHex(ByteString binary) {
        return binaryStringToHex(binary, false);
    }

    /**
     * Computes the Keccak-256 hash as per ethereum specifications.
     */
    public static String getKeccak256Hash(String hex) throws Exception {
        String result = null;

        Keccak.Digest256 digest = new Keccak.Digest256();
        digest.update(hexStringToBinary(hex).toByteArray());
        byte[] res = digest.digest();
        result = "0x" + Hex.toHexString(res).toLowerCase();

        return result;
    }

    /**
     * Pads zeroes to the hex string to ensure a uniform length of 64 hex characters.
     */
    public static String padZeroes(String p) {
        int zeroes = 0;
        StringBuilder sb = new StringBuilder();
        if (p.startsWith("0x")) {
            p = p.substring(2);
        }
        if (p.length() < 64) {
            zeroes = 64 - p.length();
        }
        sb.append("0x");
        while (zeroes > 0) {
            sb.append("0");
            zeroes--;
        }
        sb.append(p);
        return sb.toString();
    }

    /**
     * An utility function to convert java exceptions stack trace into Strings.
     *
     * @return string of exception stack trace
     */
    public static String exceptionToString(Exception e) {
        StringWriter sw = new StringWriter();
        PrintWriter pw = new PrintWriter(sw);
        e.printStackTrace(pw);
        return sw.toString(); // stack trace as a string
    }

    /**
     * Constructs the response in case of error.
     *
     * @param message Error message
     * @param id Request Id
     * @param jsonRpc RPC version
     * @return Error message string
     */
    @SuppressWarnings("unchecked")
    public static String errorMessage(String message, long id, String jsonRpc) {
        JSONObject responseJson = new JSONObject();
        responseJson.put("id", id);
        responseJson.put("jsonprc", jsonRpc);

        JSONObject error = new JSONObject();
        error.put("message", message);
        responseJson.put("error", error);

        return responseJson.toJSONString();
    }

    /**
     * Prepare a json error message.
     */
    public static JSONObject errorJson(String message) {
        JSONObject error = new JSONObject();
        error.put("error", message);
        return error;
    }

    public static String getRequestBody(final HttpServletRequest request) throws IOException {
        String paramString = request.getReader().lines().collect(Collectors.joining(System.lineSeparator()));
        return paramString;
    }

    /**
     * Exception during parsing.
     */
    public static class HexParseException extends Exception {
        public HexParseException(String message) {
            super(message);
        }
    }

    /**
     * Pull the block number out of JSON.
     */
    public static long parseBlockNumber(String blockNumber) {
        if (blockNumber.equals("earliest")) {
            return 0;
        } else if (blockNumber.equals("latest") || blockNumber.equals("pending")) {
            return -1;
        } else {
            if (blockNumber.startsWith("0x")) {
                return Long.valueOf(blockNumber.substring(2), 16);
            } else {
                return Long.valueOf(blockNumber);
            }
        }
    }

}
