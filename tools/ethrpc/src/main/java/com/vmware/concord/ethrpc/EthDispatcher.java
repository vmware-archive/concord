/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import java.io.IOException;
import java.util.Map;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.apache.logging.log4j.ThreadContext;
import org.apache.logging.log4j.message.ObjectMessage;
import org.json.simple.JSONArray;
import org.json.simple.JSONAware;
import org.json.simple.JSONObject;
import org.json.simple.parser.JSONParser;
import org.json.simple.parser.ParseException;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.ComponentScan;
import org.springframework.http.HttpHeaders;
import org.springframework.http.HttpStatus;
import org.springframework.http.MediaType;
import org.springframework.http.ResponseEntity;
import org.springframework.stereotype.Controller;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RequestMethod;
import org.springframework.web.socket.TextMessage;
import org.springframework.web.socket.WebSocketSession;
import org.springframework.web.socket.handler.TextWebSocketHandler;

import com.fasterxml.jackson.core.type.TypeReference;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.vmware.concord.Concord;
import com.vmware.concord.Concord.ConcordResponse;
import com.vmware.concord.Concord.ErrorResponse;
import com.vmware.concord.ConcordHelper;
import com.vmware.concord.IConcordConnection;
import com.vmware.concord.connections.ConcordConnectionPool;

/**
 * <p>
 * GET: Used to list available RPC methods. A list of currently exposed Eth RPC methods is read from the config file and
 * returned to the client.
 * </p>
 *
 * <p>
 * url endpoint : /
 * </p>
 *
 * <p>
 * POST: Used to execute the specified method. Request and response construction are handled by the appropriate
 * handlers. A TCP socket connection is made to Concord and requests and responses are encoded in the Google Protocol
 * Buffer format. Also supports a group of requests.
 * </p>
 */
@Controller
@ComponentScan("com.vmware.concord.connections")
public final class EthDispatcher extends TextWebSocketHandler {
    private static final long serialVersionUID = 1L;
    public static long netVersion;
    public static boolean netVersionSet;
    private static Logger logger = LogManager.getLogger("ethLogger");
    private JSONArray rpcList;
    private String jsonRpc;
    private HttpHeaders standardHeaders;
    private ConcordConnectionPool concordConnectionPool;

    @Autowired
    public EthDispatcher(ConcordConnectionPool connectionPool) throws ParseException {
        concordConnectionPool = connectionPool;

        JSONParser p = new JSONParser();
        try {
            rpcList = (JSONArray) p.parse(Constants.ETHRPC_LIST);
            jsonRpc = Constants.JSONRPC;
        } catch (Exception e) {
            logger.error("Failed to read RPC information from config file", e);
        }

        standardHeaders = new HttpHeaders();
        standardHeaders.setContentType(MediaType.APPLICATION_JSON_UTF8);
    }

    /**
     * Handling websocket requests.
     */
    @Override
    public void handleTextMessage(WebSocketSession session, TextMessage message)
            throws InterruptedException, IOException {
        ThreadContext.put("organization_id", "1234");
        ThreadContext.put("consortium_id", "1234");
        ThreadContext.put("method", "websocket");
        ThreadContext.put("uri", "/ws");
        JSONAware aware = getResponse(message.getPayload());
        session.sendMessage(new TextMessage(aware.toJSONString()));
    }

    /**
     * Builds the response for sending to client.
     */
    public JSONAware getResponse(String paramString) {
        JSONArray batchRequest = null;
        JSONArray batchResponse = new JSONArray();
        JSONParser parser = new JSONParser();
        JSONAware responseBody;
        boolean isBatch = false;
        try {
            logger.debug("Request Parameters: " + paramString);

            // If we receive a single request, add it to a JSONArray for the sake
            // of uniformity.
            if (paramString.startsWith("[")) {
                isBatch = true;
                batchRequest = (JSONArray) parser.parse(paramString);
                if (batchRequest == null || batchRequest.size() == 0) {
                    throw new Exception("Invalid request");
                }
            } else {
                batchRequest = new JSONArray();
                batchRequest.add(parser.parse(paramString));
            }

            for (Object params : batchRequest) {
                JSONObject requestParams = (JSONObject) params;

                // Dispatch requests to the corresponding handlers
                batchResponse.add(dispatch(requestParams));
            }
            if (isBatch) {
                responseBody = batchResponse;
            } else {
                responseBody = (JSONObject) batchResponse.get(0);
            }
        } catch (ParseException e) {
            logger.error("Invalid request", e);
            responseBody = errorMessage("Unable to parse request", -1, jsonRpc);
        } catch (Exception e) {
            logger.error(ApiHelper.exceptionToString(e));
            responseBody = errorMessage(e.getMessage(), -1, jsonRpc);
        } finally {
            ThreadContext.clearAll();
        }
        logger.debug("Response: " + responseBody.toJSONString());
        return responseBody;
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
    public static JSONObject errorMessage(String message, long id, String jsonRpc) {
        JSONObject responseJson = new JSONObject();
        responseJson.put("id", id);
        responseJson.put("jsonprc", jsonRpc);

        JSONObject error = new JSONObject();
        error.put("message", message);
        responseJson.put("error", error);

        return responseJson;
    }

    /**
     * Extracts the RPC method name from the request JSON.
     *
     * @param ethRequestJson Request JSON
     * @return Method name
     */
    public static String getEthMethodName(JSONObject ethRequestJson) throws EthRpcHandlerException {
        if (ethRequestJson.containsKey("method")) {
            if (ethRequestJson.get("method") instanceof String) {
                return (String) ethRequestJson.get("method");
            } else {
                throw new EthRpcHandlerException("method must be a string");
            }
        } else {
            throw new EthRpcHandlerException("request must contain a method");
        }
    }

    /**
     * Extracts the Request Id from the request JSON.
     *
     * @param ethRequestJson Request JSON
     * @return Request id
     */
    public static long getEthRequestId(JSONObject ethRequestJson) throws EthRpcHandlerException {
        if (ethRequestJson.containsKey("id")) {
            if (ethRequestJson.get("id") instanceof Number) {
                return ((Number) ethRequestJson.get("id")).longValue();
            } else {
                throw new EthRpcHandlerException("id must be a number");
            }
        } else {
            throw new EthRpcHandlerException("request must contain an id");
        }
    }

    /**
     * Services the Get request for listing currently exposed Eth RPCs. Retrieves the list from the configurations file
     * and returns it to the client.
     */
    @RequestMapping(path = "/", method = RequestMethod.GET)
    public ResponseEntity<JSONAware> doGet() {
        ThreadContext.put("consortium_id", "100");
        ThreadContext.put("organization_id", "10");
        ThreadContext.put("replica_id", "1");
        ThreadContext.put("method", "GET");
        ThreadContext.put("uri", "/");
        ThreadContext.put("source", "rpcList");
        if (rpcList == null) {
            logger.error("Configurations not read.");
            return new ResponseEntity<>(new JSONArray(), standardHeaders, HttpStatus.SERVICE_UNAVAILABLE);
        }
        logger.info("Request Eth RPC API list");
        ThreadContext.clearAll();
        return new ResponseEntity<>(rpcList, standardHeaders, HttpStatus.OK);
    }

    /**
     * Services the POST request for executing the specified methods. Retrieves the request parameters and calls the
     * dispatch function. Builds the response for sending to client.
     */
    @SuppressWarnings("unchecked")
    @RequestMapping(path = "/", method = RequestMethod.POST)
    public ResponseEntity<JSONAware> doPost(@RequestBody String paramString) {
        // Retrieve the request fields
        JSONArray batchRequest = null;
        JSONArray batchResponse = new JSONArray();
        JSONParser parser = new JSONParser();
        boolean isBatch = false;
        ResponseEntity<JSONAware> responseEntity;
        // TODO change the organization_id and consortium_id to real ones in future
        ThreadContext.put("organization_id", "100");
        ThreadContext.put("consortium_id", "10");
        ThreadContext.put("replica_id", "1");
        ThreadContext.put("method", "POST");
        ThreadContext.put("uri", "/");
        JSONAware responseBody = getResponse(paramString);
        logger.debug("Response: " + responseBody.toJSONString());
        return new ResponseEntity<>(responseBody, standardHeaders, HttpStatus.OK);
    }


    /**
     * Creates the appropriate handler object and calls its functions to construct an ConcordRequest object. Sends this
     * request to Concord and converts its response into a format required by the user.
     *
     * @param  requestJson  User input
     * @return JSONObject   JSON returned to the caller
     */
    public JSONObject dispatch(JSONObject requestJson) throws Exception {
        // Default initialize variables, so that if exception is thrown
        // while initializing the variables error message can be constructed
        // with default values.
        long id = -1;
        String ethMethodName;
        AbstractEthRpcHandler handler;
        boolean isLocal = false;
        JSONObject responseObject;
        ConcordResponse concordResponse;
        // Create object of the suitable handler based on the method specified in
        // the request
        try {
            ethMethodName = getEthMethodName(requestJson);
            ThreadContext.put("source", ethMethodName);
            id = getEthRequestId(requestJson);
            switch (ethMethodName) {
                case Constants.SEND_TRANSACTION_NAME:
                case Constants.SEND_RAWTRANSACTION_NAME:
                case Constants.CALL_NAME:
                    handler = new EthSendTxHandler();
                    break;

                case Constants.GET_TRANSACTIONBYHASH_NAME:
                    handler = new EthGetTransactionByHashHandler();
                    break;

                case Constants.GET_TRANSACTIONRECEIPT_NAME:
                    handler = new EthGetTransactionReceiptHandler();
                    break;

                case Constants.GET_STORAGEAT_NAME:
                    handler = new EthGetStorageAtHandler();
                    break;

                case Constants.GET_CODE_NAME:
                    handler = new EthGetCodeHandler();
                    break;

                case Constants.GET_TRANSACTIONCOUNT_NAME:
                    handler = new EthGetTransactionCountHandler();
                    break;

                case Constants.GET_BALANCE_NAME:
                    handler = new EthGetBalanceHandler();
                    break;

                case Constants.GET_BLOCKBYHASH_NAME:
                case Constants.GET_BLOCKBYNUMBER_NAME:
                    handler = new EthGetBlockHandler();
                    break;

                case Constants.UNINSTALLFILTER_NAME:
                    isLocal = true; // fallthrough
                case Constants.NEWFILTER_NAME:
                case Constants.NEWBLOCKFILTER_NAME:
                case Constants.NEWPENDINGTRANSACTIONFILTER_NAME:
                    handler = new EthFilterManageHandler();
                    break;

                case Constants.FILTERCHANGE_NAME:
                    handler = new EthFilterChangeHandler();
                    break;

                case Constants.NETVERSION_NAME:
                    handler = new EthLocalResponseHandler();
                    isLocal = netVersionSet;
                    break;

                case Constants.WEB3_SHA3_NAME:
                case Constants.RPC_MODULES_NAME:
                case Constants.COINBASE_NAME:
                case Constants.CLIENT_VERSION_NAME:
                case Constants.MINING_NAME:
                case Constants.ACCOUNTS_NAME:
                case Constants.GAS_PRICE_NAME:
                case Constants.ESTIMATE_GAS_NAME:
                case Constants.SYNCING_NAME:
                    handler = new EthLocalResponseHandler();
                    isLocal = true;
                    break;

                case Constants.BLOCKNUMBER_NAME:
                    handler = new EthBlockNumberHandler();
                    break;

                case Constants.GET_LOGS_NAME:
                    handler = new EthGetLogsHandler();
                    break;

                default:
                    throw new Exception("Invalid method name.");
            }

            if (!isLocal) {
                Concord.ConcordRequest.Builder concordRequestBuilder = Concord.ConcordRequest.newBuilder();
                if (handler.buildRequest(concordRequestBuilder, requestJson)) {
                    concordResponse = communicateWithConcord(concordRequestBuilder.build());
                    // If there is an error reported by Concord
                    if (concordResponse.getErrorResponseCount() > 0) {
                        ErrorResponse errResponse = concordResponse.getErrorResponse(0);
                        if (errResponse.hasDescription()) {
                            responseObject = errorMessage(errResponse.getDescription(), id, jsonRpc);
                        } else {
                            responseObject = errorMessage("Error received from concord", id, jsonRpc);
                        }
                    } else {
                        responseObject = handler.buildResponse(concordResponse, requestJson);
                    }
                } else {
                    // Handler said not to bother with the request. This request is handled locally instead.
                    responseObject = handler.buildResponse(null, requestJson);
                }
            }
            // There are some RPC methods which are handled locally. No
            // need to talk to Concord for these cases.
            else {
                // In local request we don't have valid eth resposne from
                // concord. Just pass null.
                responseObject = handler.buildResponse(null, requestJson);
            }
            // Add method name in response object
            responseObject.put("method", ethMethodName);
            Map<String, Object> jsonMap = convertJsonToMap(responseObject);
            logger.info(new ObjectMessage(jsonMap));
            // TODO: Need to refactor exception handling.  Shouldn't just catch an exception and eat it.
        } catch (Exception e) {
            logger.error(ApiHelper.exceptionToString(e));
            responseObject = errorMessage(e.getMessage(), id, jsonRpc);
        }
        return responseObject;
    }

    /**
     * Sends an ConcordRequest to Concord and receives Concord's response.
     *
     * @param req ConcordRequest object
     * @return Response received from Concord
     */
    private ConcordResponse communicateWithConcord(Concord.ConcordRequest req) throws Exception {
        IConcordConnection conn = null;
        Concord.ConcordResponse concordResponse = null;
        try {
            conn = concordConnectionPool.getConnection();
            if (conn == null) {
                throw new Exception("Error communicating with concord");
            }

            boolean res = ConcordHelper.sendToConcord(req, conn);
            if (!res) {
                throw new Exception("Error communicating with concord");
            }

            // receive response from Concord
            concordResponse = ConcordHelper.receiveFromConcord(conn);
            if (concordResponse == null) {
                throw new Exception("Error communicating with concord");
            }
        } catch (Exception e) {
            logger.error("General exception communicating with concord: ", e);
            throw e;
        } finally {
            concordConnectionPool.putConnection(conn);
        }

        return concordResponse;
    }

    /**
     * Converts a JSONObject to a Map to be used for logging.
     * @param jsonObject JSONObject
     * @return map of string and object
     */
    private Map<String, Object> convertJsonToMap(JSONObject jsonObject) throws IOException {

        Map<String, Object> jsonMap = new ObjectMapper().readValue(jsonObject.toJSONString(),
                new TypeReference<Map<String, Object>>() {});
        return jsonMap;
    }
}
