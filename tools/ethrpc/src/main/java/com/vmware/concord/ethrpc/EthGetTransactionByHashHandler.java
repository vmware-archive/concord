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
 * This handler is used for all `eth_getTransactionByHash` requests.
 */
public class EthGetTransactionByHashHandler extends AbstractEthRpcHandler {

    Logger logger = LogManager.getLogger(EthGetTransactionByHashHandler.class);

    /**
     * Build a TransactionRequest from the given requestJson.
     *
     * @param  builder      ConcordRequest Builder
     * @param  requestJson  JSONObject from the original RPC request
     * @return true         Always send the request
     */
    @Override
    public boolean buildRequest(Concord.ConcordRequest.Builder builder, JSONObject requestJson) throws Exception {

        try {
            JSONArray params = extractRequestParams(requestJson);
            String txHash = (String) params.get(0);
            ByteString hashBytes = ApiHelper.hexStringToBinary(txHash);
            Concord.TransactionRequest txRequestObj =
                Concord.TransactionRequest.newBuilder().setHash(hashBytes).build();

            builder.setTransactionRequest(txRequestObj);
        } catch (Exception e) {
            logger.error("Building the EthGetTransactionByHash request failed.");
            throw e;
        }
        return true;
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

            result.put("blockHash", ApiHelper.binaryStringToHex(tx.getBlockHash()));
            result.put("blockNumber", "0x" + Long.toHexString(tx.getBlockNumber()));
            result.put("from", ApiHelper.binaryStringToHex(tx.getFrom()));
            result.put("gas", "0x" + Long.toHexString(tx.getGasLimit()));
            result.put("gasPrice", "0x" + Long.toHexString(tx.getGasPrice()));
            result.put("hash", ApiHelper.binaryStringToHex(tx.getHash()));
            result.put("input", ApiHelper.binaryStringToHex(tx.getInput()));
            result.put("nonce", "0x" + Long.toHexString(tx.getNonce()));
            result.put("to", ApiHelper.binaryStringToHex(tx.getTo()));
            result.put("transactionIndex", "0x" + Long.toHexString(tx.getTransactionIndex()));
            result.put("value", ApiHelper.binaryStringToHex(tx.getValue(), true));
            result.put("v", "0x" + Long.toHexString(tx.getSigV()));
            result.put("r", ApiHelper.binaryStringToHex(tx.getSigR()));
            result.put("s", ApiHelper.binaryStringToHex(tx.getSigS()));

            respObject.put("id", EthDispatcher.getEthRequestId(requestJson));
            respObject.put("jsonrpc", Constants.JSONRPC);
            respObject.put("result", result);
        } catch (Exception e) {
            logger.fatal("Building JSON response failed.", e);
        }
        return respObject;
    }
}
