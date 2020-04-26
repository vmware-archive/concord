/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord.ethrpc;

/**
 * Etherum RPC method names and other constants.
 */
public class Constants {
    //API Endpoints
    // Below endpoints are needed because some servlets still use this information
    public static final String API_URI_PREFIX = "/api";
    public static final String STATIC_RESOURCE_LOCATION = "classpath:/static/";
    public static final String HOME_PAGE_LOCATION = "classpath:/static/index.html";


    // Static content configurations
    public static final String ETHRPC_LIST =
            "[{\"name\": \"eth_accounts\",\"params\": [],\"returns\": \"array\"},"
            + "{\"name\": \"eth_blockNumber\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_call\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_coinbase\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_gasPrice\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_getBalance\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_getBlockByHash\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_getBlockByNumber\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_getCode\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_getFilterChanges\",\"params\": [],\"returns\": \"array\"},"
            + "{\"name\": \"eth_getLogs\",\"params\": [],\"returns\": \"array\"},"
            + "{\"name\": \"eth_getStorageAt\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_getTransactionCount\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_getTransactionByHash\",\"params\": [],\"returns\": \"object\"},"
            + "{\"name\": \"eth_getTransactionReceipt\",\"params\": [],\"returns\": \"object\"},"
            + "{\"name\": \"eth_mining\",\"params\": [],\"returns\": \"bool\"},"
            + "{\"name\": \"eth_newFilter\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_newBlockFilter\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_sendTransaction\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_sendRawTransaction\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_syncing\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"eth_uninstallFilter\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"net_version\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"personal_newAccount\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"rpc_modules\",\"params\": [],\"returns\": \"object\"},"
            + "{\"name\": \"web3_clientVersion\",\"params\": [],\"returns\": \"string\"},"
            + "{\"name\": \"web3_sha3\",\"params\": [],\"returns\": \"string\"}]";
    public static final String RPC_MODULES =
            "[{\"eth\": \"1.0\",\"net\": \"1.0\",\"personal\": \"1.0\",\"rpc\": \"1.0\",\"web3\": \"1.0\"}]";
    public static final String CLIENT_VERSION = "EthRPC/v1.1.0/linux/java1.8.0";
    public static final int IS_MINING = 0;
    // Gas price is a quantity, which allows odd-nibble values
    public static final String GAS_PRICE = "0x0";

    //Other Constants.  Maybe think if these should be properties
    public static final int BLOCKLIST_DEFAULTCOUNT = 10;
    public static final int TRANSACTIONLIST_DEFAULTCOUNT = 10;
    public static final String COINBASE = "0x262c0d7ab5ffd4ede2199f6ea793f819e1abb019";

    //EthRPCs
    public static final String JSONRPC = "2.0";
    public static final String SEND_TRANSACTION_NAME = "eth_sendTransaction";
    public static final String SEND_RAWTRANSACTION_NAME = "eth_sendRawTransaction";
    public static final String GET_TRANSACTIONRECEIPT_NAME = "eth_getTransactionReceipt";
    public static final String GET_TRANSACTIONBYHASH_NAME = "eth_getTransactionByHash";
    public static final String GET_STORAGEAT_NAME = "eth_getStorageAt";
    public static final String WEB3_SHA3_NAME = "web3_sha3";
    public static final String RPC_MODULES_NAME = "rpc_modules";
    public static final String CLIENT_VERSION_NAME = "web3_clientVersion";
    public static final String MINING_NAME = "eth_mining";
    public static final String CALL_NAME = "eth_call";
    public static final String NETVERSION_NAME = "net_version";
    public static final String ACCOUNTS_NAME = "eth_accounts";
    //Temporary workaround for eth_accounts.  Comma-delimited list of users from our genesis.json
    public static final String USERS = "0x262c0d7ab5ffd4ede2199f6ea793f819e1abb019,"
                                       + "0x5bb088f57365907b1840e45984cae028a82af934,"
                                       + "0x0000a12b3f3d6c9b0d3f126a83ec2dd3dad15f39,"
                                       + "0x5101f077F4C2FD96a3bFC33bC37C25DFB7Bf4E99,"
                                       + "0x318cc89B29910D61961fDb4d308e4D2203d447d0,"
                                       + "0x67842Ce7404c59d7F0F36ae34bE5ca87367E9dF6,"
                                       + "0xe5577469562c5fDF2fF1117c8b888C3CFA043F73,"
                                       + "0x4d8A8dCd92d27730705efDa917e1dAf31Bc30DE2,"
                                       + "0x9717a1E194601885bFE61768321D7e98917ca9c4,"
                                       + "0x5089d747B810A13979e6Ef227e9eb1E66F7e4DA0,"
                                       + "0xC4af22ad4D1467Cd60Dfe3145E00F23a032EA185,"
                                       + "0xD3bbEc12DefDca0b96056a7fF5Cc00D76AfB6F6a,"
                                       + "0x4AffeC47CB121aF0e9fd5915895996e3f8Ad3A4A,"
                                       + "0x0eDDA8153B03c40fA3A40cC07e28089DD0435464,"
                                       + "0x06e20BB182FC80AA16903a565CC4f93e2b47D423,"
                                       + "0xfD444ECB2c868C3A851cE6389c24dbCB4Eb665fd,"
                                       + "0xafCE1Bf29cE631B0AeE33308Ff6A8b37586b3034,"
                                       + "0x4461d0d1e80073b3490398af1252F0B925e42A8F,"
                                       + "0xe1c7a0aDE49A2B7D8788bfFb1438F722A0bE925F,"
                                       + "0xEe7fE9b0fEA0242032b57b9c0A34E8db15b67422";
    public static final String NEWFILTER_NAME = "eth_newFilter";
    public static final String NEWBLOCKFILTER_NAME = "eth_newBlockFilter";
    public static final String NEWPENDINGTRANSACTIONFILTER_NAME = "eth_newPendingTransactionFilter";
    public static final String FILTERCHANGE_NAME = "eth_getFilterChanges";
    public static final String UNINSTALLFILTER_NAME = "eth_uninstallFilter";
    public static final String GET_CODE_NAME = "eth_getCode";
    public static final String COINBASE_NAME = "eth_coinbase";
    public static final String GET_BLOCKBYHASH_NAME = "eth_getBlockByHash";
    public static final String BLOCKNUMBER_NAME = "eth_blockNumber";
    public static final String GET_BLOCKBYNUMBER_NAME = "eth_getBlockByNumber";
    public static final String GET_TRANSACTIONCOUNT_NAME = "eth_getTransactionCount";
    public static final String GET_BALANCE_NAME = "eth_getBalance";
    public static final String GAS_PRICE_NAME = "eth_gasPrice";
    public static final String ESTIMATE_GAS_NAME = "eth_estimateGas";
    public static final String SYNCING_NAME = "eth_syncing";
    public static final String GET_LOGS_NAME = "eth_getLogs";
}
