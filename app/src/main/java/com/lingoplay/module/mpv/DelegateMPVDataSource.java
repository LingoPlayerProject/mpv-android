package com.lingoplay.module.mpv;

import java.io.IOException;

public class DelegateMPVDataSource implements MPVDataSource {
    private final MPVDataSource dataSource;

    public DelegateMPVDataSource(MPVDataSource dataSource) {
        this.dataSource = dataSource;
    }

    @Override
    public long size() throws IOException {
        return dataSource.size();
    }

    @Override
    public int read(byte[] buf, int len) throws IOException {
        return dataSource.read(buf, len);
    }

    @Override
    public void seek(long offset) throws IOException {
        dataSource.seek(offset);
    }

    @Override
    public void close() {
        dataSource.close();
    }
}
