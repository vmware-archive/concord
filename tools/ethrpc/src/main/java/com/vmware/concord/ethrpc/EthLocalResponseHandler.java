/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import java.util.Collections;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.json.simple.JSONArray;
import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;
import org.springframework.util.StringUtils;

import com.vmware.concord.Concord;

/**
 * <p>
 * Handles the RPC requests which can be processes directly without forwarding them to Concord. However, this
 * is an exception. When handling `net_version` we do connect to concord but just get the version number from
 * concord. This handler handles following eth RPC requests :
 * <ul>
 * <li>web3_sha3</li>
 * <li>eth_coinbase</li>
 * <li>web3_clientVersion</li>
 * <li>eth_mining</li>
 * <li>net_version</li>
 * <li>rpc_modules</li>
 * <li>eth_gasPrice</li>
 * <li>eth_estimateGas</li>
 * <li>eth_syncing</li>
 * </ul>
 * </p>
 */
public class EthLocalResponseHandler extends AbstractEthRpcHandler {

    private static Logger logger = LogManager.getLogger(EthLocalResponseHandler.class);

    /**
     * The *local* response handler shouldn't need to send a request ... except that it does in some very specific cases
     * (documented inline). These cases could be refactored into other handlers, if they get more complicated.
     *
     * @return True when a request does need to be sent, false otherwise.
     */
    public boolean buildRequest(Concord.ConcordRequest.Builder concordRequestBuilder, JSONObject requestJson)
        throws Exception {
        String ethMethodName = EthDispatcher.getEthMethodName(requestJson);
        if (ethMethodName.equals(Constants.NETVERSION_NAME)) {
            // eth_netVersion has to send a request to concord the first time, but the value is cached afterward
            concordRequestBuilder.setProtocolRequest(Concord.ProtocolRequest.newBuilder());
            return true;
        } else {
            // no other methods send a request to concord
            return false;
        }
    }

    /**
     * Initializes response object by using request JSONObject for local methods. An overload of this method which
     * accepts EthResponse as input parameter is defined in {@link AbstractEthRpcHandler} class. However, for requests
     * which can be handled locally we do not even call concord and hence do not have a valid concord response. Hence we
     * can not use the method provided by parent class.
     *
     */
    @SuppressWarnings("unchecked")
    JSONObject initializeResponseObject(JSONObject requestJson) {
        JSONObject respObject = new JSONObject();
        respObject.put("id", requestJson.get("id"));
        respObject.put("jsonrpc", Constants.JSONRPC);
        return respObject;
    }

    /**
     * Builds the response JSONObject by processing the given type of request locally. For example if the request is of
     * type `web3_sha3` then here we will actually generate the hash of given data and produce a response object
     * containing that hash. We do not do kind of processing in `buildRequest` method.
     *
     * @param concordResponse The response receive from concord Note: Since, we do not build anything in
     *        buildConcordRequest and we also do not call concord for these requests. Hence this method completely
     *        ignores the concordResponse object. It can be NULL
     * @param requestJson The original RPC request JSONObject.
     * @return the JSONObject of the response.
     */
    @SuppressWarnings("unchecked")
    public JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson) throws Exception {
        long id = (long) requestJson.get("id");
        String ethMethodName = EthDispatcher.getEthMethodName(requestJson);
        /*
         * Here we can not use parents initializeResponseObject method because it takes a valid EthResponse object as
         * input parameter, however in local methods we do not even call concord and hence do not have a valid value for
         * that parameter. Instead we provide our own overload of initializeResponseObject method in this class which
         * initializes object with requestJson object.
         */
        JSONObject result = initializeResponseObject(requestJson);

        Object localData = null;

        if (ethMethodName.equals(Constants.WEB3_SHA3_NAME)) {
            JSONArray params = extractRequestParams(requestJson);
            // Request should contain just one param value
            if (params.size() != 1) {
                logger.error("Invalid request parameter : params");
                throw new EthRpcHandlerException(EthDispatcher
                        .errorMessage("'params' must contain only one element", id, jsonRpc).toJSONString());
            }

            try {
                localData = ApiHelper.getKeccak256Hash((String) params.get(0));
                logger.info("Generated keccak hash is: " + localData);
            } catch (Exception e) {
                logger.error("Error in calculating Keccak hash", e);
                throw new EthRpcHandlerException(
                        EthDispatcher.errorMessage("'invalid param", id, jsonRpc).toJSONString());
            }
        } else if (ethMethodName.equals(Constants.RPC_MODULES_NAME)) {
            JSONParser p = new JSONParser();
            localData = (((JSONArray) p.parse(Constants.RPC_MODULES)).get(0));
        } else if (ethMethodName.equals(Constants.COINBASE_NAME)) {
            localData = Constants.COINBASE;
        } else if (ethMethodName.equals(Constants.CLIENT_VERSION_NAME)) {
            localData = Constants.CLIENT_VERSION;
        } else if (ethMethodName.equals(Constants.MINING_NAME)) {
            localData = Constants.IS_MINING == 0 ? false : true;
        } else if (ethMethodName.equals(Constants.NETVERSION_NAME)) {
            if (!EthDispatcher.netVersionSet && concordResponse != null && concordResponse.hasProtocolResponse()) {
                Concord.ProtocolResponse protocolResponse = concordResponse.getProtocolResponse();
                if (protocolResponse.hasNetVersion()) {
                    EthDispatcher.netVersion = protocolResponse.getNetVersion();
                    EthDispatcher.netVersionSet = true;
                }
            }
            localData = EthDispatcher.netVersion;
        } else if (ethMethodName.equals(Constants.GAS_PRICE_NAME)) {
            localData = Constants.GAS_PRICE;
        } else if (ethMethodName.equals(Constants.ESTIMATE_GAS_NAME)) {
            localData = Constants.GAS_PRICE;
        } else if (ethMethodName.equals(Constants.SYNCING_NAME)) {
            // "false" in this context means that the node believes it is up to
            // date. In the future, we may use this to share when a node knows that
            // it is processing a state transfer, but for now, all nodes believe
            // they are always up to date.
            localData = false;
        } else if (ethMethodName.equals(Constants.ACCOUNTS_NAME)) {
            // HACK: Concord has all the account information but if Ethrpc is queried directly we at least want to
            // return something. This is used to connect `truffle console` to Ethrpc directly.
            JSONArray usersJsonArr = new JSONArray();
            String usersStr = Constants.USERS;
            if (!StringUtils.isEmpty(usersStr)) {
                String[] usersArr = usersStr.split(",");
                Collections.addAll(usersJsonArr, usersArr);
            }
            localData = usersJsonArr;
        }

        result.put("result", localData);

        return result;
    }

}
