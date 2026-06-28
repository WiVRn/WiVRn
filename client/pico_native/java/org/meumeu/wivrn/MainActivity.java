package org.meumeu.wivrn;

import android.content.Intent;
import android.os.Bundle;
import android.util.Log;
import android.view.Window;
import android.view.WindowManager;

import com.picovr.cvclient.CVController;
import com.picovr.cvclient.CVControllerListener;
import com.picovr.cvclient.CVControllerManager;
import com.picovr.cvclient.ButtonNum;
import com.picovr.picovrlib.cvcontrollerclient.ControllerClient;
import com.picovr.vractivity.Eye;
import com.picovr.vractivity.HmdState;
import com.picovr.vractivity.RenderInterface;
import com.picovr.vractivity.VRActivity;
import com.psmart.vrlib.PicovrSDK;

public class MainActivity extends VRActivity implements RenderInterface {
    private static final String TAG = "WiVRn-Pico";

    static {
        System.loadLibrary("wivrn-neo2");
    }

    private CVControllerManager cvManager;
    private CVController leftController;
    private CVController rightController;

    private long nativePtr;

    private CVControllerListener cvListener = new CVControllerListener() {
        @Override
        public void onBindSuccess() {
            Log.d(TAG, "Controller service bound");
        }

        @Override
        public void onBindFail() {
            Log.e(TAG, "Controller service bind failed");
        }

        @Override
        public void onThreadStart() {
            leftController = cvManager.getMainController();
            rightController = cvManager.getSubController();
            Log.d(TAG, "Controller thread started");
        }

        @Override
        public void onConnectStateChanged(int serialNum, int state) {
            Log.d(TAG, "Controller " + serialNum + " state: " + state);
        }

        @Override
        public void onMainControllerChanged(int serialNum) {
            leftController = cvManager.getMainController();
            rightController = cvManager.getSubController();
        }

        @Override
        public void onChannelChanged(int device, int channel) {
        }
    };

    @Override
    public void onCreate(Bundle savedInstanceState) {
        requestWindowFeature(Window.FEATURE_NO_TITLE);
        getWindow().setFlags(
                WindowManager.LayoutParams.FLAG_FULLSCREEN
                        | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON,
                WindowManager.LayoutParams.FLAG_FULLSCREEN
                        | WindowManager.LayoutParams.FLAG_KEEP_SCREEN_ON);

        super.onCreate(savedInstanceState);

        try {
            nativePtr = getNativePtr();
            Log.d(TAG, "nativePtr = " + nativePtr);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "getNativePtr failed", e);
            nativePtr = 0;
        }
        setIntent(getIntent());

        cvManager = new CVControllerManager(this.getApplicationContext());
        cvManager.setListener(cvListener);

        try {
            nativeWivrnInit(nativePtr, getIntent());
            Log.d(TAG, "nativeWivrnInit called");
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeWivrnInit failed", e);
        }
    }

    @Override
    public void onResume() {
        super.onResume();
        cvManager.bindService();
        PicovrSDK.startSensor(0);
        PicovrSDK.setTrackingOriginType(1);
        Log.d(TAG, "onResume, nativePtr=" + nativePtr);
        try {
            nativeWivrnResume(nativePtr);
        } catch (UnsatisfiedLinkError e) {
            Log.e(TAG, "nativeWivrnResume failed", e);
        }
    }

    @Override
    public void onPause() {
        nativeWivrnPause(nativePtr);
        PicovrSDK.stopSensor(0);
        cvManager.unbindService();
        super.onPause();
    }

    @Override
    public void onDestroy() {
        nativeWivrnDestroy(nativePtr);
        super.onDestroy();
    }

    @Override
    public void onNewIntent(Intent intent) {
        super.onNewIntent(intent);
        nativeWivrnNewIntent(nativePtr, intent);
    }

    @Override
    public void onFrameBegin(HmdState hmdState) {
        float[] orientation = hmdState.getOrientation();
        float[] position = hmdState.getPos();
        float[] hmdData = new float[7];
        hmdData[0] = orientation[0];
        hmdData[1] = orientation[1];
        hmdData[2] = orientation[2];
        hmdData[3] = orientation[3];
        hmdData[4] = position[0];
        hmdData[5] = position[1];
        hmdData[6] = position[2];

        cvManager.updateControllerData(hmdData);

        float[] leftPose = null, rightPose = null;
        float[] leftOrient = null, rightOrient = null;
        int leftTrigger = 0, rightTrigger = 0;
        int[] leftTouch = null, rightTouch = null;
        int leftBattery = 0, rightBattery = 0;
        boolean leftA = false, leftB = false, leftGrip = false, leftClick = false, leftMenu = false;
        boolean rightA = false, rightB = false, rightGrip = false, rightClick = false, rightMenu = false;

        if (leftController != null) {
            leftOrient = leftController.getOrientation();
            leftPose = leftController.getPosition();
            leftTrigger = leftController.getTriggerNum();
            leftTouch = leftController.getTouchPad();
            leftBattery = leftController.getBatteryLevel();
            leftA = leftController.getButtonState(ButtonNum.buttonAX);
            leftB = leftController.getButtonState(ButtonNum.buttonBY);
            leftGrip = leftController.getButtonState(ButtonNum.buttonLG);
            leftClick = leftController.getButtonState(ButtonNum.click);
            leftMenu = leftController.getButtonState(ButtonNum.app);
        }

        if (rightController != null) {
            rightOrient = rightController.getOrientation();
            rightPose = rightController.getPosition();
            rightTrigger = rightController.getTriggerNum();
            rightTouch = rightController.getTouchPad();
            rightBattery = rightController.getBatteryLevel();
            rightA = rightController.getButtonState(ButtonNum.buttonAX);
            rightB = rightController.getButtonState(ButtonNum.buttonBY);
            rightGrip = rightController.getButtonState(ButtonNum.buttonRG);
            rightClick = rightController.getButtonState(ButtonNum.click);
            rightMenu = rightController.getButtonState(ButtonNum.app);
        }

        nativeWivrnOnFrameBegin(nativePtr, orientation, position,
                leftOrient, leftPose, leftTrigger, leftTouch, leftBattery,
                leftA, leftB, leftGrip, leftClick, leftMenu,
                rightOrient, rightPose, rightTrigger, rightTouch, rightBattery,
                rightA, rightB, rightGrip, rightClick, rightMenu);
    }

    @Override
    public void onDrawEye(Eye eye) {
        nativeWivrnDrawEye(nativePtr, eye.getType());
    }

    @Override
    public void onFrameEnd() {
        nativeWivrnFrameEnd(nativePtr);
    }

    @Override
    public void onTouchEvent() {
    }

    @Override
    public void initGL(int w, int h) {
        PicovrSDK.SetEyeBufferSize(w, h);
        nativeWivrnInitGL(nativePtr, w, h);
    }

    @Override
    public void deInitGL() {
        nativeWivrnDeInitGL(nativePtr);
    }

    @Override
    public void surfaceChangedCallBack(int w, int h) {
        nativeWivrnSurfaceChanged(nativePtr, w, h);
    }

    @Override
    public void onRenderPause() {
        nativeWivrnRenderPause(nativePtr);
    }

    @Override
    public void onRenderResume() {
        nativeWivrnRenderResume(nativePtr);
    }

    @Override
    public void onRendererShutdown() {
        nativeWivrnRendererShutdown(nativePtr);
    }

    @Override
    public void renderEventCallBack(int event) {
        nativeWivrnRenderEvent(nativePtr, event);
    }

    // Native methods - WiVRn-specific (PVR SDK handles its own via VRActivity)
    public native void nativeWivrnInit(long ptr, Intent intent);
    public native void nativeWivrnDestroy(long ptr);
    public native void nativeWivrnPause(long ptr);
    public native void nativeWivrnResume(long ptr);
    public native void nativeWivrnNewIntent(long ptr, Intent intent);
    public native void nativeWivrnOnFrameBegin(long ptr, float[] headOrient, float[] headPos,
            float[] leftOrient, float[] leftPos, int leftTrigger, int[] leftTouch, int leftBattery,
            boolean leftA, boolean leftB, boolean leftGrip, boolean leftClick, boolean leftMenu,
            float[] rightOrient, float[] rightPos, int rightTrigger, int[] rightTouch, int rightBattery,
            boolean rightA, boolean rightB, boolean rightGrip, boolean rightClick, boolean rightMenu);
    public native void nativeWivrnDrawEye(long ptr, int eye);
    public native void nativeWivrnFrameEnd(long ptr);
    public native void nativeWivrnInitGL(long ptr, int w, int h);
    public native void nativeWivrnDeInitGL(long ptr);
    public native void nativeWivrnSurfaceChanged(long ptr, int w, int h);
    public native void nativeWivrnRenderPause(long ptr);
    public native void nativeWivrnRenderResume(long ptr);
    public native void nativeWivrnRendererShutdown(long ptr);
    public native void nativeWivrnRenderEvent(long ptr, int event);
}
