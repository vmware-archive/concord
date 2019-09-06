/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

import java.util.concurrent.ConcurrentHashMap;
import java.util.concurrent.atomic.AtomicLong;

/**
 * Holder of the state of each ethereum filter. This is basically a singleton
 * (it's a bunch of static methods around a static concurrent hash map).
 */
public class FilterManager {
    private static ConcurrentHashMap<String, EthereumFilter> filters =
        new ConcurrentHashMap<String, EthereumFilter>();

    private static AtomicLong nextFilterId = new AtomicLong();

    /**
     * Create a new block filter, starting at latestBlock. Returns the ID of this new filter.
     */
    public static String createBlockFilter(long latestBlock) {
        String filterId = "0x" + Long.toHexString(nextFilterId.getAndIncrement());

        EthereumFilter filter = EthereumFilter.newBlockFilter(filterId, latestBlock);
        filters.put(filterId, filter);

        return filterId;
    }

    /**
     * Get the filter with ID filterId. Returns null if there is no filter with that ID.
     */
    public static EthereumFilter getFilter(String filterId) {
        return filters.get(filterId);
    }

    /**
     * Update the latest block recorded in the given filter. The passed-in filter is not changed. To see the changes
     * made, use getFilter to re-read the filter from the manager.
     */
    public static void updateFilter(EthereumFilter filter, long latestBlock) {
        EthereumFilter newFilter = filter.updateFilter(latestBlock);
        filters.put(newFilter.id, newFilter);
    }

    /**
     * Remove the filter with the given ID from the manager.
     */
    public static boolean uninstallFilter(String filterId) {
        if (filters.containsKey(filterId)) {
            filters.remove(filterId);
            return true;
        }
        return false;
    }
}
