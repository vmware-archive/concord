/*
 * Copyright (c) 2018-2019 VMware, Inc. All Rights Reserved.
 * SPDX-License-Identifier: Apache-2.0
 */

package com.vmware.concord;

import java.io.IOException;

/**
 * Concord Connection interface.
 */
public interface IConcordConnection {
    void close();

    void send(byte[] msg) throws IOException;

    byte[] receive() throws IOException;

    boolean check();
}
