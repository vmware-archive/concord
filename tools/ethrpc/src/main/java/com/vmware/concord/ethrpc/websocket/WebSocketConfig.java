/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc.websocket;

import org.apache.logging.log4j.LogManager;
import org.apache.logging.log4j.Logger;
import org.json.simple.parser.ParseException;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.context.annotation.Configuration;
import org.springframework.web.socket.config.annotation.EnableWebSocket;
import org.springframework.web.socket.config.annotation.WebSocketConfigurer;
import org.springframework.web.socket.config.annotation.WebSocketHandlerRegistry;

import com.vmware.concord.connections.ConcordConnectionPool;
import com.vmware.concord.ethrpc.EthDispatcher;

/**
 * Temporary configuration to serve websocket.
 */
@Configuration
@EnableWebSocket
public class WebSocketConfig implements WebSocketConfigurer {
    private static Logger logger = LogManager.getLogger("WebSocketConfig");
    private ConcordConnectionPool concordConnectionPool;

    @Autowired
    public WebSocketConfig(ConcordConnectionPool connectionPool) {
        concordConnectionPool = connectionPool;
    }

    /**
     * Register websocket handlers.
     */
    public void registerWebSocketHandlers(WebSocketHandlerRegistry registry) {
        try {
            registry.addHandler(new EthDispatcher(concordConnectionPool), "/ws").setAllowedOrigins("*");
        } catch (ParseException e) {
            logger.error("Failed to regist websocket handler", e);
        }
    }

}
