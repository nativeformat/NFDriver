package com.spotify.nfdrivertest_android;

import android.support.v7.app.AppCompatActivity;
import android.os.Bundle;

public class MainActivity extends AppCompatActivity {
    static {
        System.loadLibrary("NFDriverApp");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);

        nativeMain();
    }

    // Check the cpp folder for the native (C++) code. Nothing happens here in Java.
    public native void nativeMain();
}
