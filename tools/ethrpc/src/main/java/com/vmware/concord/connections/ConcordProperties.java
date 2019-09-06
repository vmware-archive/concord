/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.connections;

import org.springframework.beans.BeanUtils;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Component;

import lombok.Getter;
import lombok.Setter;

/**
 * Propeties per Concord Blockchain.  Will need to consider the connection pool.
 */
@Component
@Getter
@Setter
public class ConcordProperties {

    // Concord configurations
    @Value("${ConcordAuthorities}")
    String concordAuthorities;
    @Value("${ConnectionPoolSize}")
    int connectionPoolSize;
    @Value("${ConnectionPoolFactor}")
    int connectionPoolFactor;
    @Value("${ConnectionPoolWaitTimeoutMs}")
    int connectionPoolWaitTimeoutMs;
    @Value("${ReceiveTimeoutMs}")
    int receiveTimeoutMs;
    @Value("${ReceiveHeaderSizeBytes}")
    int receiveHeaderSizeBytes;


    /**
     * Create a copy of the default ConcordProperties.
     * @return Shallow copy
     */
    public ConcordProperties instance() {
        ConcordProperties prop = new ConcordProperties();
        BeanUtils.copyProperties(this, prop);
        return prop;
    }
}
