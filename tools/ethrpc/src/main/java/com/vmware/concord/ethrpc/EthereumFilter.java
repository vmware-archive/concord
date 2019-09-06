/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

/**
 * A representation of an ethereum filter at a given block. The current design
 * is for the filter object to be immutable. Updating a filter to a new latest
 * block is the process of creating a new filter of that type at the latest
 * block.
 */
public class EthereumFilter {
    /**
     * The "type" of this filter. Block filters (filters that just wait for new blocks to be mined) are the only type we
     * support so far. There are also "pending" filters that match pending (unmined) transactions, and log/event
     * filters.
     */
    public enum FilterType {
        BlockFilter
    }

    /* The ID that the application uses to poll this filter. */
    public final String id;

    /* The type of this filter. */
    public final FilterType type;

    /**
     * The last block that was returned by polling this filter. Or the block number before the first block that this
     * filter should return when polled the first time.
     */
    public final long latestBlock;

    /**
     * Web3js/geth gets really confused if you return the block it's waiting for from the first poll (a bug in their
     * code means that the callback function isn't yet set). To get around this, we "delay" so that the first poll
     * returns no blocks.
     */
    public final boolean delay;

    private EthereumFilter(String id, FilterType type, long latestBlock, boolean delay) {
        this.id = id;
        this.type = type;
        this.latestBlock = latestBlock;
        this.delay = delay;
    }

    /**
     * Create a new filter that waits for new blocks.
     */
    public static EthereumFilter newBlockFilter(String id, long latestBlock) {
        return new EthereumFilter(id, FilterType.BlockFilter, latestBlock, true);
    }

    /**
     * Update the given filter to record the last block returned during the most recent poll. Clears the "delay" flag.
     */
    public EthereumFilter updateFilter(long latestBlock) {
        return new EthereumFilter(this.id, this.type, latestBlock, false);
    }
}
