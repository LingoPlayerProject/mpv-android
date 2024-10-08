package com.lingoplay.module.mpv;

import java.io.IOException;

public interface MPVDataSource {

    interface Factory {
        MPVDataSource open(String uri) throws IOException;
    }

    /**
     * Total length of the file
     */
    long size() throws IOException;

    /**
     * read data from a custom bitstream input media.
     * @return strictly positive number of bytes read, 0 on end-of-stream
     */
    int read(byte[] buf, int len) throws IOException;

    /**
     * seek a custom bitstream input media.
     * @param offset absolute byte offset to seek to
     */
    void seek(long offset) throws IOException;

    void cancel();

    void close();
}
