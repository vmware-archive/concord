/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.json.simple.JSONObject;

import com.google.protobuf.ByteString;
import com.vmware.concord.Concord;

/**
 * Handler for handling the `eth_blockNumber` RPC call.
 */
public class EthBlockNumberHandler extends AbstractEthRpcHandler {

    Logger logger = LogManager.getLogger(EthBlockNumberHandler.class);

    /**
     * Builds the EthRequest object which can be sent to Concord from given requestJson object.
     *
     * @param builder Builder object in which parameters are set.
     * @param requestJson User request
     * @return Always true - send the request.
     */
    @Override
    public boolean buildRequest(Concord.ConcordRequest.Builder builder, JSONObject requestJson) throws Exception {
        Concord.EthRequest.Builder ethRequestBuilder = initializeRequestObject(requestJson);
        ethRequestBuilder.setMethod(Concord.EthRequest.EthMethod.BLOCK_NUMBER);
        builder.addEthRequest(ethRequestBuilder.build());
        return true;
    }

    /**
     * Builds the RPC JSON response object from given ConcordResponse. Extracts the EthResponse object from
     * concordRequest. This EthResponse object must contain the data filed with hex string representing latex block
     * number.
     *
     * @param concordResponse Response received from Concord.
     * @param requestJson User request.
     * @return JSON RPC response object
     */
    @Override
    public JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson) throws Exception {
        Concord.EthResponse ethResponse = concordResponse.getEthResponse(0);
        JSONObject responseObject = initializeResponseObject(ethResponse);
        ByteString latestBlock = ethResponse.getData();
        responseObject.put("result", ApiHelper.binaryStringToHex(latestBlock, true /* drop leading zeros */));
        return responseObject;
    }
}
