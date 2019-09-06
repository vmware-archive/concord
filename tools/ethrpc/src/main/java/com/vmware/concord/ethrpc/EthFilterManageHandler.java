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
 * This handler is used to service following types of filter requests.
 * <ul>
 * <li>eth_newBlockFilter</li>
 * <li>eth_newFilter(TODO)</li>
 * <li>eth_newPendingTransactionFilter(TODO)</li>
 * <li>eth_uninstallFilter</li>
 * </ul>
 * </p>
 */
public class EthFilterManageHandler extends AbstractEthRpcHandler {

    private static Logger logger = LogManager.getLogger(EthFilterManageHandler.class);

    /**
     * Builds the request object from the type of eth request specified in requestJson. Adds the built request into
     * ConcordRequest using given builder.
     */
    public boolean buildRequest(Concord.ConcordRequest.Builder builder, JSONObject requestJson) throws Exception {
        // We need information about the latest block to store with a new filter.
        Concord.BlockListRequest.Builder blockListRequest = Concord.BlockListRequest.newBuilder();
        blockListRequest.setCount(1);
        builder.setBlockListRequest(blockListRequest);
        return true;
    }

    /**
     * Extracts the response objects from passed concordResponse object and returns a RPC JSONObject.
     *
     * @param concordResponse The response from concord. For uninstall, information from concord is not needed, so this
     *        argument may be null.
     * @param requestJson The original request Json
     * @return the reply JSON object with the filter create/remove result
     */
    @SuppressWarnings("unchecked")
    public JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson) throws Exception {
        JSONObject respObject = new JSONObject();
        respObject.put("id", EthDispatcher.getEthRequestId(requestJson));
        respObject.put("jsonrpc", jsonRpc);

        String ethMethodName = EthDispatcher.getEthMethodName(requestJson);
        if (ethMethodName.equals(Constants.NEWBLOCKFILTER_NAME)) {
            Concord.BlockListResponse blr = concordResponse.getBlockListResponse();
            Concord.BlockBrief block = blr.getBlock(0);

            // We use "latestBlock-1" here because Ethereum/web3 apps create their filters after submitting the
            // transaction that they expect to catch with them. They don't expect a block to appear immediately, but we
            // created the block before even returning the receipt hash, so their transaction, in the best case, is in
            // the latest block. Note that in a very active system, their transaction may be in a block that is already
            // buried. There is nothing we can do about this other than encourage people to create their filters before
            // submitting their transactions. The "-1" helps for simple compatibility testing, though.
            long latestBlock = block.getNumber() - 1;

            String filterId = FilterManager.createBlockFilter(latestBlock);
            logger.debug("Created block filter " + filterId + " at block " + latestBlock);
            respObject.put("result", filterId);
        } else if (ethMethodName.equals(Constants.NEWFILTER_NAME)
                   || ethMethodName.equals(Constants.NEWPENDINGTRANSACTIONFILTER_NAME)) {
            respObject = EthDispatcher.errorMessage("Unsupported filter type",
                                                    EthDispatcher.getEthRequestId(requestJson),
                                                    jsonRpc);
        } else {
            JSONArray params = extractRequestParams(requestJson);
            String filterId = (String) params.get(0);
            boolean success = FilterManager.uninstallFilter(filterId);
            logger.debug("Uninstalled filter " + filterId + ": " + success);
            respObject.put("result", success);
        }
        return respObject;
    }

}
