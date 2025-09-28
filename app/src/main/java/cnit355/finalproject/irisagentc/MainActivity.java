package cnit355.finalproject.irisagentc;

import androidx.appcompat.app.AppCompatActivity;
import android.os.Bundle;
import android.util.Log;
import android.view.SurfaceView;

public class MainActivity extends AppCompatActivity {

    private static final String TAG = "IrisAgent_Java";

    // 1. Load the native library. The name must match the one in your CMakeLists.txt
    // In our case, it's "irisagentc".
    static {
        System.loadLibrary("irisagentc");
    }

    @Override
    protected void onCreate(Bundle savedInstanceState) {
        super.onCreate(savedInstanceState);
        // A SurfaceView is still useful for giving the app a window handle
//        setContentView(new SurfaceView(this));
        Log.i(TAG, "Activity onCreate: Calling native layer.");

        // 2. Pass the activity context and asset manager to the native layer for initialization.
        onCreateNative(this);
    }

    @Override
    protected void onResume() {
        super.onResume();
        Log.i(TAG, "Activity onResume: Calling native layer.");
        // 3. Notify the native layer that the app is resuming.
        onResumeNative();
    }

    @Override
    protected void onPause() {
        super.onPause();
        Log.i(TAG, "Activity onPause: Calling native layer.");
        // 4. Notify the native layer that the app is pausing.
        onPauseNative();
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();
        Log.i(TAG, "Activity onDestroy: Calling native layer.");
        // 5. Notify the native layer that the app is being destroyed for cleanup.
        onDestroyNative();
    }

    // --- Native Method Declarations ---
    // These functions are implemented in native-lib.cpp

    /**
     * Called when the activity is first created. This is where the main
     * OpenXR initialization happens.
     * @param activity The MainActivity instance.
     */
    public native void onCreateNative(MainActivity activity);

    /**
     * Called when the activity will start interacting with the user.
     * This is where the OpenXR session begins and the render loop starts.
     */
    public native void onResumeNative();

    /**
     * Called when the activity is no longer in the foreground.
     * This is where the OpenXR session ends and the render loop stops.
     */
    public native void onPauseNative();

    /**
     * The final call before the activity is destroyed.
     * This is where all OpenXR resources are released.
     */
    public native void onDestroyNative();
}