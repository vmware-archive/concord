/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.connections;

import java.io.IOException;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

/**
 * Temporary configuration to create connection pool bean.
 */
@Configuration
public class ConcordConfiguration {
    private final Logger logger = LogManager.getLogger(ConcordConfiguration.class);
    private ConcordProperties config;

    @Autowired
    public ConcordConfiguration(ConcordProperties config) {
        this.config = config;
    }

    @Bean
    ConcordConnectionPool concordConnectionPool() throws IOException {
        ConcordConnectionFactory factory =
                new ConcordConnectionFactory(ConcordConnectionFactory.ConnectionType.TCP, config);
        ConcordConnectionPool pool = new ConcordConnectionPool().initialize(config, factory);
        logger.info("concord connection pool initialized");
        return pool;
    }

}
