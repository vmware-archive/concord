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
import com.vmware.concord.Concord.EthRequest;
import com.vmware.concord.Concord.EthRequest.EthMethod;
import com.vmware.concord.Concord.EthResponse;

/**
 * This handler is used to service eth_getStorageAt POST requests.
 */
public class EthGetStorageAtHandler extends AbstractEthRpcHandler {

    Logger logger = LogManager.getLogger(EthBlockNumberHandler.class);

    /**
     * Builds the Concord request builder. Extracts the 'to' address and data from the request and uses it to set up an
     * Concord Request builder with an EthRequest.
     *
     * @param concordRequestBuilder Object in which request is built
     * @param requestJson Request parameters passed by the user
     * @return Always true - send the request.
     */
    @Override
    public boolean buildRequest(Concord.ConcordRequest.Builder concordRequestBuilder, JSONObject requestJson)
            throws Exception {
        Concord.EthRequest ethRequest = null;
        try {
            EthRequest.Builder b = initializeRequestObject(requestJson);
            b.setMethod(EthMethod.GET_STORAGE_AT);
            JSONArray params = extractRequestParams(requestJson);
            b.setAddrTo(ApiHelper.hexStringToBinary((String) params.get(0)));
            String p = (String) params.get(1);
            String s = ApiHelper.padZeroes(p);
            b.setData(ApiHelper.hexStringToBinary(s));
            // add "block" parameter, the default block parameter is "latest".
            // if no parameter or its value is negative, concord treat is as default
            if (params.size() == 3) {
                long blockNumber = ApiHelper.parseBlockNumber((String) params.get(2));
                if (blockNumber >= 0) {
                    b.setBlockNumber(blockNumber);
                }
            }
            ethRequest = b.build();
        } catch (Exception e) {
            logger.error("Exception in get storage at handler", e);
            throw e;
        }
        concordRequestBuilder.addEthRequest(ethRequest);
        return true;
    }

    /**
     * Builds the response object to be returned to the user.
     *
     * @param concordResponse Response received from Concord
     * @param requestJson Request parameters passed by the user
     * @return response to be returned to the user
     */
    @SuppressWarnings("unchecked")
    @Override
    public JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson) {
        EthResponse ethResponse = concordResponse.getEthResponse(0);
        JSONObject respObject = initializeResponseObject(ethResponse);
        respObject.put("result", ApiHelper.binaryStringToHex(ethResponse.getData()));
        return respObject;
    }
}
