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
 * <li>eth_getFilterChanges</li>
 * </ul>
 * </p>
 */
public class EthFilterChangeHandler extends AbstractEthRpcHandler {

    private static Logger logger = LogManager.getLogger(EthFilterChangeHandler.class);

    /* The number of blocks to ask for when checking if there are updates to a block filter. */
    private static final int BLOCK_FILTER_WINDOW = 10;

    /* The filter that this request is about. Stored here to persist between buildRequest and buildResponse. */
    private EthereumFilter filter;

    /**
     * Builds the request object from the type of eth request specified in requestJson. Adds the built request into
     * ConcordRequest using given builder.
     */
    public boolean buildRequest(Concord.ConcordRequest.Builder builder, JSONObject requestJson) throws Exception {
        JSONArray params = extractRequestParams(requestJson);

        String filterId = (String) params.get(0);
        filter = FilterManager.getFilter(filterId);

        if (filter != null) {
            if (filter.type == EthereumFilter.FilterType.BlockFilter) {
                // We implement block filter change checking by asking Concord
                // for some number of blocks after the last block the filter saw.
                Concord.BlockListRequest.Builder blockListRequest = Concord.BlockListRequest.newBuilder();
                blockListRequest.setLatest(filter.latestBlock + BLOCK_FILTER_WINDOW);
                blockListRequest.setCount(BLOCK_FILTER_WINDOW);
                builder.setBlockListRequest(blockListRequest);
                return true;
            } else {
                throw new EthRpcHandlerException("Unknown filter type " + filter.type);
            }
        }

        // if the filter was not found, we just won't send a request, so we won't get a response
        return false;
    }

    /**
     * Extracts the FilterResponse objects from passed concordResponse object and returns a RPC JSONObject made from
     * FilterResponse.
     *
     * @param concordResponse Object of ConcordResponse
     * @param requestJson The original request Json
     * @return the reply JSON object made from FilterResponse object inside ConcordResponse.
     */
    @SuppressWarnings("unchecked")
    public JSONObject buildResponse(Concord.ConcordResponse concordResponse, JSONObject requestJson) throws Exception {
        JSONObject respObject;

        if (filter != null
            && filter.type == EthereumFilter.FilterType.BlockFilter
            && concordResponse.hasBlockListResponse()) {
            Concord.BlockListResponse blr = concordResponse.getBlockListResponse();
            JSONArray blocks = new JSONArray();
            long latestBlock = filter.latestBlock;

            // See comment on EthereumFilter.delay - we don't want to return anything during the first poll, to work
            // around a web3js/geth bug.
            if (!filter.delay) {
                for (Concord.BlockBrief block: blr.getBlockList()) {
                    // we get the latest WINDOW blocks back, which might be blocks we've already sent
                    if (block.getNumber() > filter.latestBlock) {
                        blocks.add(ApiHelper.binaryStringToHex(block.getHash()));
                    }
                    // doing this separately, just in case the blocks are in the wrong order for this iteration
                    latestBlock = Long.max(latestBlock, block.getNumber());
                }
            }

            respObject = new JSONObject();
            respObject.put("id", EthDispatcher.getEthRequestId(requestJson));
            respObject.put("jsonrpc", jsonRpc);
            respObject.put("result", blocks);

            if (latestBlock > filter.latestBlock || filter.delay) {
                // updating clears the delay
                FilterManager.updateFilter(filter, latestBlock);
                logger.debug("Updated filter " + filter.id + " to block " + latestBlock);
            }
        } else {
            JSONArray params = extractRequestParams(requestJson);
            String filterId = (String) params.get(0);
            logger.debug("Filter " + filterId + " not found.");
            respObject = EthDispatcher.errorMessage("Filter not found",
                                                    EthDispatcher.getEthRequestId(requestJson),
                                                    jsonRpc);
        }
        return respObject;
    }
}
