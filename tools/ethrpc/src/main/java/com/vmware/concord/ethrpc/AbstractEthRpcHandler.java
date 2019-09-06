/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import org.json.simple.JSONArray;
import org.json.simple.JSONObject;

import com.vmware.concord.Concord;
import com.vmware.concord.Concord.EthRequest;
import com.vmware.concord.Concord.EthResponse;

/**
 * <p>
 * Copyright 2018 VMware, all rights reserved.
 * </p>
 *
 * <p>
 * This abstract class serves as a template for all EthRPC Handler classes. These handlers are used to construct
 * ConcordRequest objects from user requests and to construct responses for the user from ConcordResponses based on the
 * method specified by the user.
 * </p>
 *
 * <p>
 * Concrete helper methods have been implemented for performing some common actions on request/response objects.
 * </p>
 */
public abstract class AbstractEthRpcHandler {


    protected String jsonRpc;

    public AbstractEthRpcHandler() {
        this.jsonRpc = Constants.JSONRPC;
    }

    /**
     * This method extracts relevant parameters from the user request and sets them in the ConcordRequest builder object
     * passed by the caller as a parameter.
     *
     * @param builder Builder object in which parameters are set.
     * @param requestJson User request
     * @return True if the request should be sent to Concord. False if the request should be ignored, and buildResponse
     *         should be called with a null ConcordResponse.
     */
    public abstract boolean buildRequest(Concord.ConcordRequest.Builder builder, JSONObject requestJson)
        throws Exception;

    /**
     * This method extracts the relevant parameters from an ConcordResponse and uses them to build a JSONObject which is
     * then sent to the user as a response to the RPC request.
     *
     * @param concordResponse Response received from Concord.
     * @param requestJson User request.
     * @return Response object to be returned to the user.
     */
    public abstract JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson)
            throws Exception;

    /**
     * Initializes an EthRequest builder object and pre-sets the request id in it.
     *
     * @param requestJson User request
     * @return Newly initialized EthRequest builder object.
     */
    EthRequest.Builder initializeRequestObject(JSONObject requestJson) throws EthRpcHandlerException {
        EthRequest.Builder b = Concord.EthRequest.newBuilder();
        long id = EthDispatcher.getEthRequestId(requestJson);
        b.setId(id);
        return b;
    }

    /**
     * Extracts the "params" part of the user's request.
     *
     * @param requestJson User request
     * @return the "params" array
     */
    JSONArray extractRequestParams(JSONObject requestJson) throws EthRpcHandlerException {
        JSONArray params = null;
        try {
            params = (JSONArray) requestJson.get("params");
            if (params == null) {
                throw new EthRpcHandlerException("'params' not present");
            }
        } catch (ClassCastException cse) {
            throw new EthRpcHandlerException("'params' must be an array");
        }
        return params;
    }

    /**
     * Initializes the response to be sent to the user and pre-sets the 'id' and 'jsonrpc' fields.
     *
     * @param ethResponse EthResponse part of the response received from Concord.
     * @return response
     */
    @SuppressWarnings("unchecked")
    JSONObject initializeResponseObject(EthResponse ethResponse) {
        JSONObject respObject = new JSONObject();
        respObject.put("id", ethResponse.getId());
        respObject.put("jsonrpc", jsonRpc);
        return respObject;
    }

}
