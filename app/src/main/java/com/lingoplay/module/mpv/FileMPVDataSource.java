package com.lingoplay.module.mpv;

import android.util.Log;

import java.io.IOException;
import java.io.RandomAccessFile;

public class FileMPVDataSource implements MPVDataSource {
    private final String path;
    private final RandomAccessFile raf;

    public FileMPVDataSource(String path) throws IOException {
        this.path = path;
        this.raf = new RandomAccessFile(path, "r");
    }

    @Override
    public long size() throws IOException {
        try {
            long length = raf.length();
            Log.d("FileMPVDataSource", String.format("call FileMPVDataSource size. %s, ret %s", path, length));
            return length;
        } catch (IOException e) {
            Log.e("FileMPVDataSource", String.format("call FileMPVDataSource size exception. %s", path), e);
            throw e;
        }
    }

    @Override
    public int read(byte[] buf, int len) throws IOException {
        try {
            int read = raf.read(buf, 0, len);
            int ret =  read == -1 ? 0 : read;
//            Log.d("FileMPVDataSource", String.format("call FileMPVDataSource read, len %s, path %s, ret %s", len, path, ret));
            return ret;
        } catch (IOException e) {
            Log.e("FileMPVDataSource", String.format("call FileMPVDataSource read exception. %s", path), e);
            throw e;
        }
    }

    @Override
    public void seek(long offset) throws IOException {
        try {
            raf.seek(offset);
            Log.d("FileMPVDataSource", String.format("call FileMPVDataSource seek. offset %s, path %s", offset, path));
        } catch (IOException e) {
            Log.e("FileMPVDataSource", String.format("call FileMPVDataSource seek exception. %s", path), e);
            throw e;
        }
    }

    @Override
    public void cancel() {
    }

    @Override
    public void close() {
        try {
            Log.d("FileMPVDataSource", String.format("call FileMPVDataSource close. path %s", path));
            raf.close();
        } catch (IOException e) {
        }
    }
}
