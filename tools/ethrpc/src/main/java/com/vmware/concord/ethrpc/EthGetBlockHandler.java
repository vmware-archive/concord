/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import java.util.List;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.json.simple.JSONArray;
import org.json.simple.JSONObject;

import com.google.protobuf.ByteString;

import com.vmware.concord.Concord;
import com.vmware.concord.Concord.BlockResponse;
import com.vmware.concord.Concord.ConcordRequest.Builder;
import com.vmware.concord.Concord.ConcordResponse;
import com.vmware.concord.Concord.TransactionResponse;

/**
 * This handler is used to service eth_getBlockByHash POST requests.
 */
public class EthGetBlockHandler extends AbstractEthRpcHandler {

    Logger logger = LogManager.getLogger(EthGetBlockHandler.class);

    /**
     * Builds the Concord request builder. Extracts the block hash from the request and uses it to set up an Concord
     * Request builder with a BlockRequest. Also performs basic checks on parameters.
     *
     * @param builder Object in which request is built
     * @param requestJson Request parameters passed by the user
     * @return Always true - send the request.
     */
    @Override
    public boolean buildRequest(Builder builder, JSONObject requestJson) throws Exception {
        try {
            JSONArray params = extractRequestParams(requestJson);
            if (params.size() != 2) {
                throw new Exception("Params should contain 2 elements for this " + "request type");
            }

            String ethMethodName = EthDispatcher.getEthMethodName(requestJson);

            // Perform type checking of the flag at this stage itself rather than
            // while building the response
            @SuppressWarnings("unused")
            boolean flag = (boolean) params.get(1);

            // Construct a blockNumberRequest object. Set its start field.
            final Concord.BlockRequest blockRequestObj;

            if (ethMethodName.equals(Constants.GET_BLOCKBYNUMBER_NAME)) {
                // Block number string can be either a hex number or it can be one
                // of "latest", "earliest", "pending". Since, concord only accepts
                // uint64_t for block number we will replace "latest" with -1
                // "earliest" with 0 (genesis block) and "pending" with -1 (since
                // in concord blocks are generated instantaneously we can say that
                // "latest" = "pending"
                String requestedBlockStr = (String) params.get(0);
                logger.info("requestBlokcStr: " + requestedBlockStr);

                long requestedBlockNumber = -1;
                if (requestedBlockStr.equals("earliest")) {
                    requestedBlockNumber = 0;
                } else if (requestedBlockStr.equals("latest") || requestedBlockStr.equals("pending")) {
                    requestedBlockNumber = -1;
                } else if (requestedBlockStr.startsWith("0x")) {
                    requestedBlockNumber = Long.parseLong(requestedBlockStr.substring(2), 16);
                } else {
                    throw new Exception("Invalid block number requested. Block "
                            + "number can either be 'latest', 'pending', 'earliest',"
                            + " or a hex number starting with '0x'");
                }
                blockRequestObj = Concord.BlockRequest.newBuilder().setNumber(requestedBlockNumber).build();
            } else { // BlockByHash_Name
                ByteString blockHash = ApiHelper.hexStringToBinary((String) params.get(0));
                blockRequestObj = Concord.BlockRequest.newBuilder().setHash(blockHash).build();
            }

            // Add the request to the concord request builder
            builder.setBlockRequest(blockRequestObj);
            return true;
        } catch (Exception e) {
            logger.error("Exception in get block handler", e);
            throw e;
        }
    }

    /**
     * Builds the response object to be returned to the user. Checks the flag in the request to determine whether a list
     * of transaction hashes or a list of transaction objects needs to be returned to the user.
     *
     * @param concordResponse Response received from Concord
     * @param requestJson Request parameters passed by the user
     * @return response to be returned to the user
     */
    @SuppressWarnings("unchecked")
    @Override
    public JSONObject buildResponse(ConcordResponse concordResponse, JSONObject requestJson) throws Exception {
        BlockResponse blockResponseObj = concordResponse.getBlockResponse();
        long id = EthDispatcher.getEthRequestId(requestJson);

        JSONObject response = new JSONObject();
        response.put("id", id);
        response.put("jsonrpc", jsonRpc);

        JSONObject result = new JSONObject();
        result.put("number", "0x" + Long.toHexString(blockResponseObj.getNumber()));
        result.put("hash", ApiHelper.binaryStringToHex(blockResponseObj.getHash()));
        result.put("parentHash", ApiHelper.binaryStringToHex(blockResponseObj.getParentHash()));
        result.put("nonce", ApiHelper.binaryStringToHex(blockResponseObj.getNonce()));
        result.put("size", blockResponseObj.getSize());

        if (blockResponseObj.hasTimestamp()) {
            result.put("timestamp", "0x" + Long.toHexString(blockResponseObj.getTimestamp()));
        }

        if (blockResponseObj.hasGasLimit()) {
            result.put("gasLimit", "0x" + Long.toHexString(blockResponseObj.getGasLimit()));
        }

        if (blockResponseObj.hasGasUsed()) {
            result.put("gasUsed", "0x" + Long.toHexString(blockResponseObj.getGasUsed()));
        }

        JSONArray transactions = new JSONArray();
        JSONArray params = extractRequestParams(requestJson);
        boolean flag = (boolean) params.get(1);

        List<TransactionResponse> list = blockResponseObj.getTransactionList();

        // include all details about transaction
        if (flag) {
            for (TransactionResponse tr : list) {
                JSONObject transaction = new JSONObject();
                transaction.put("hash", ApiHelper.binaryStringToHex(tr.getHash()));
                transaction.put("nonce", "0x" + Long.toHexString(tr.getNonce()));
                transaction.put("blockHash", ApiHelper.binaryStringToHex(tr.getBlockHash()));
                transaction.put("blockNumber", "0x" + Long.toHexString(tr.getBlockNumber()));
                transaction.put("transactionIndex", "0x" + Long.toHexString(tr.getTransactionIndex()));
                transaction.put("from", ApiHelper.binaryStringToHex(tr.getFrom()));
                transaction.put("to", ApiHelper.binaryStringToHex(tr.getTo()));
                transaction.put("value", ApiHelper.binaryStringToHex(tr.getValue(), true));
                transaction.put("input", ApiHelper.binaryStringToHex(tr.getInput()));
                transaction.put("contractAddress", ApiHelper.binaryStringToHex(tr.getContractAddress()));
                transaction.put("logs", EthGetTransactionReceiptHandler.buildLogs(tr));

                transactions.add(transaction);
            }
        }
        // only include the transaction hashes
        else {
            for (TransactionResponse tr : list) {
                transactions.add(ApiHelper.binaryStringToHex(tr.getHash()));
            }
        }
        result.put("transactions", transactions);
        response.put("result", result);

        return response;
    }
}
