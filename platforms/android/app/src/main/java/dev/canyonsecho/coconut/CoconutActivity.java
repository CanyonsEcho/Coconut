package dev.canyonsecho.coconut;

import android.os.Bundle;
import org.libsdl.app.SDLActivity;

/**
 * Android entry Activity hosting Coconut's native SDL3 + OpenGL ES 3.0 + RmlUi 6.3 workbench.
 */
public class CoconutActivity extends SDLActivity {
    @Override
    protected String[] getLibraries() {
        return new String[] {
            "SDL3",
            "coconut"
        };
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
    }
}
