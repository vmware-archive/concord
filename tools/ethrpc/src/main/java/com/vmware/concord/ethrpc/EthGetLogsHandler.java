/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.json.simple.JSONArray;
import org.json.simple.JSONObject;

import com.vmware.concord.Concord;

/**
 * This handler is used for all `eth_getTransactionReceipt` requests.
 */
public class EthGetLogsHandler extends AbstractEthRpcHandler {

    Logger logger = LogManager.getLogger(EthGetLogsHandler.class);

    /**
     * Build a LogsRequest from the given requestJson.
     *
     * @param  builder      ConcordRequest Builder
     * @param  requestJson  JSONObject from the original RPC request
     * @return true         Always send the request
     */
    public boolean buildRequest(Concord.ConcordRequest.Builder builder, JSONObject requestJson) throws Exception {
        try {
            JSONArray params = extractRequestParams(requestJson);

            if (params.size() > 1) {
                throw new EthRpcHandlerException("Too many parameters. Either none or a JSON object.");
            }

            Concord.LogsRequest.Builder logsReq = Concord.LogsRequest.newBuilder();

            if (params.size() == 0) {
                // Default - request all logs from the latest block
                logsReq.setFromBlock(ApiHelper.parseBlockNumber("latest"));
                logsReq.setToBlock(ApiHelper.parseBlockNumber("latest"));
            } else {
                // Evaluate filter options
                JSONObject filter = (JSONObject) params.get(0);

                // Block hash or block numbers
                if (filter.containsKey("blockHash")
                    && (filter.containsKey("fromBlock") || filter.containsKey("toBlock"))) {
                    throw new EthRpcHandlerException("Invalid request: Choose block numbers or a block hash");
                }

                if (filter.containsKey("fromBlock")) {
                    logsReq.setFromBlock(ApiHelper.parseBlockNumber((String) filter.get("fromBlock")));
                } else {
                    logsReq.setFromBlock(ApiHelper.parseBlockNumber("latest"));
                }
                if (filter.containsKey("toBlock")) {
                    logsReq.setToBlock(ApiHelper.parseBlockNumber((String) filter.get("toBlock")));
                } else {
                    logsReq.setToBlock(ApiHelper.parseBlockNumber("latest"));
                }

                // Let's make sure we have a valid range
                if (logsReq.getToBlock() >= 0 && logsReq.getToBlock() < logsReq.getFromBlock()) {
                    throw new EthRpcHandlerException("Invalid request: Infinite block range");
                }

                if (filter.containsKey("address")) {
                    logsReq.setContractAddress(ApiHelper.hexStringToBinary((String) filter.get("address")));
                }
                if (filter.containsKey("topics")) {
                    JSONArray topics = (JSONArray) filter.get("topics");
                    for (int i = 0; i < topics.size(); ++i) {
                        logsReq.addTopic(ApiHelper.hexStringToBinary((String) topics.get(i)));
                    }
                }
                if (filter.containsKey("blockHash")) {
                    logsReq.setBlockHash(ApiHelper.hexStringToBinary((String) filter.get("blockHash")));
                }
            }

            builder.setLogsRequest(logsReq.build());
            return true;
        } catch (Exception e) {
            logger.error("Exception in get logs request handler", e);
            throw e;
        }
    }

    /**
     * Build a JSONObject response from the LogsResponse within the given ConcordResponse.
     *
     * @param  concordResponse  ConcordResponse object
     * @param  requestJson      JSONObject from the original RPC request
     * @return JSONObject
     */
    @Override
    public JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson) {

        JSONObject respObject = new JSONObject();
        JSONArray result = new JSONArray();
        try {
            Concord.LogsResponse logsResp = concordResponse.getLogsResponse();
            for (int i = 0; i < logsResp.getLogCount(); i++) {
                Concord.LogResponse log = logsResp.getLog(i);
                JSONObject logJson = new JSONObject();

                logJson.put("address",             ApiHelper.binaryStringToHex(log.getContractAddress()));
                logJson.put("blockHash",           ApiHelper.binaryStringToHex(log.getBlockHash()));
                logJson.put("blockNumber",         "0x" + Long.toHexString(log.getBlockNumber()));
                logJson.put("logIndex",            "0x" + Long.toHexString(log.getLogIndex()));
                logJson.put("transactionHash",     ApiHelper.binaryStringToHex(log.getTransactionHash()));
                logJson.put("transactionIndex",    "0x" + Long.toHexString(log.getTransactionIndex()));
                logJson.put("transactionLogIndex", "0x" + Long.toHexString(log.getTransactionLogIndex()));

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

                result.add(logJson);
            }

            respObject.put("result", result);
            respObject.put("id", EthDispatcher.getEthRequestId(requestJson));
            respObject.put("jsonrpc", Constants.JSONRPC);
        } catch (Exception e) {
            logger.fatal("Building JSON response failed.", e);
        }
        return respObject;
    }
}
