package com.lingoplay.module.mpv;

import java.io.IOException;

public class DefaultMPVDataSourceFactory implements MPVDataSource.Factory {
    @Override
    public MPVDataSource open(String uri) throws IOException {
        return new FileMPVDataSource(uri.substring("datasource://".length()));
    }
}
