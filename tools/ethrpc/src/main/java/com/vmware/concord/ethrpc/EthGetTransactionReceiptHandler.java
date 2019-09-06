/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.json.simple.JSONArray;
import org.json.simple.JSONObject;

import com.google.protobuf.ByteString;
import com.vmware.concord.Concord;

/**
 * This handler is used for all `eth_getTransactionReceipt` requests.
 */
public class EthGetTransactionReceiptHandler extends AbstractEthRpcHandler {

    Logger logger = LogManager.getLogger(EthGetTransactionReceiptHandler.class);

    /**
     * Build a TransactionRequest from the given requestJson.
     *
     * @param  builder      ConcordRequest Builder
     * @param  requestJson  JSONObject from the original RPC request
     * @return true         Always send the request
     */
    public boolean buildRequest(Concord.ConcordRequest.Builder builder, JSONObject requestJson) throws Exception {
        try {
            logger.debug("Inside GetTXReceipt buildRequest");
            // Construct a transaction request object.
            JSONArray params = extractRequestParams(requestJson);
            String txHash = (String) params.get(0);
            ByteString hashBytes = ApiHelper.hexStringToBinary(txHash);

            Concord.TransactionRequest txRequestObj =
                Concord.TransactionRequest.newBuilder().setHash(hashBytes).build();
            builder.setTransactionRequest(txRequestObj);
            return true;
        } catch (Exception e) {
            logger.error("Exception in tx receipt handler", e);
            throw e;
        }
    }

    /**
     * Build a JSONObject response from the TransactionResponse within the given ConcordResponse.
     *
     * @param  concordResponse  ConcordResponse object
     * @param  requestJson      JSONObject from the original RPC request
     * @return JSONObject
     */
    @Override
    public JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson) {

        JSONObject respObject = new JSONObject();
        JSONObject result = new JSONObject();
        try {
            Concord.TransactionResponse tx = concordResponse.getTransactionResponse();

            result.put("transactionHash", ApiHelper.binaryStringToHex(tx.getHash()));
            result.put("transactionIndex", "0x" + Long.toHexString(tx.getTransactionIndex()));
            result.put("blockHash", ApiHelper.binaryStringToHex(tx.getBlockHash()));
            result.put("blockNumber", "0x" + Long.toHexString(tx.getBlockNumber()));
            result.put("from", ApiHelper.binaryStringToHex(tx.getFrom()));
            result.put("to", ApiHelper.binaryStringToHex(tx.getTo()));
            // TODO: Sum up all `gasUsed` from previous tx in the same block
            result.put("cumulativeGasUsed", "0x" + Long.toHexString(tx.getGasUsed()));
            result.put("gasUsed", "0x" + Long.toHexString(tx.getGasUsed()));
            if (tx.hasContractAddress()) {
                result.put("contractAddress", ApiHelper.binaryStringToHex(tx.getContractAddress()));
            } else {
                result.put("contractAddress", null);
            }
            result.put("logs", buildLogs(tx));
            // TODO: Attach bloom filter
            result.put("logsBloom", "0x00");
            // Concord EVM has status code '0' for success and other Positive
            // values to denote error. However, for JSON RPC '1' is success
            // and '0' is failure. Here we need to reverse status value of concord
            // response before returning it.
            result.put("status", "0x" + Integer.toString(tx.getStatus() == 0 ? 1 : 0));

            respObject.put("id", EthDispatcher.getEthRequestId(requestJson));
            respObject.put("jsonrpc", Constants.JSONRPC);
            respObject.put("result", result);
        } catch (Exception e) {
            logger.fatal("Building JSON response failed.", e);
        }
        return respObject;
    }

    /**
     * Build the loggin JSON.
     */
    public static JSONArray buildLogs(Concord.TransactionResponse tx) {
        JSONArray logs = new JSONArray();
        for (int i = 0; i < tx.getLogCount(); i++) {
            Concord.LogResponse log = tx.getLog(i);
            JSONObject logJson = new JSONObject();
            logJson.put("address", ApiHelper.binaryStringToHex(log.getContractAddress()));

            JSONArray topics = new JSONArray();
            for (int j = 0; j < log.getTopicCount(); j++) {
                topics.add(ApiHelper.binaryStringToHex(log.getTopic(j)));
            }
            logJson.put("topics", topics);

            if (log.hasData()) {
                logJson.put("data", ApiHelper.binaryStringToHex(log.getData()));
            } else {
                logJson.put("data", "0x");
            }

            logJson.put("blockHash", ApiHelper.binaryStringToHex(tx.getBlockHash()));
            logJson.put("blockNumber", "0x" + Long.toHexString(tx.getBlockNumber()));
            logJson.put("transactionHash", ApiHelper.binaryStringToHex(tx.getHash()));
            logJson.put("transactionIndex", "0x" + Long.toHexString(tx.getTransactionIndex()));
            logJson.put("transactionLogIndex", "0x" + Long.toHexString(i));

            // At the moment we mine one block per transaction. Therefore, the
            // transaction log index becomes the block log index.
            logJson.put("logIndex", "0x" + Long.toHexString(i));

            logs.add(logJson);
        }
        return logs;
    }

}
