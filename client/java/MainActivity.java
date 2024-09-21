package org.meumeu.wivrn;

public class MainActivity extends android.app.NativeActivity
{
	public static native void sendMessageFromJava(String msg, String arg);

	static
	{
		System.loadLibrary("wivrn");
	}

	@Override
	public native void onRequestPermissionsResult(int requestCode, String [] permissions, int[] grantResults);

	// @Override
	// public void onRequestPermissionsResult(int requestCode, String [] permissions, int[] grantResults)
	// {
	// 	super.onRequestPermissionsResult(requestCode, permissions, grantResults);
	// 	// switch (requestCode) {
	// 	// case REQUEST_CODE_STORAGE_PERMISSION:
	// 	// 	if (permissionsGranted(permissions, grantResults)) {
	// 	// 		NativeApp.sendMessageFromJava("permission_granted", "storage");
	// 	// 	} else {
	// 	// 		NativeApp.sendMessageFromJava("permission_denied", "storage");
	// 	// 	}
	// 	// 	break;
	// 	// case REQUEST_CODE_LOCATION_PERMISSION:
	// 	// 	if (permissionsGranted(permissions, grantResults)) {
	// 	// 		mLocationHelper.startLocationUpdates();
	// 	// 	}
	// 	// 	break;
	// 	// case REQUEST_CODE_CAMERA_PERMISSION:
	// 	// 	if (mCameraHelper != null && permissionsGranted(permissions, grantResults)) {
	// 	// 		mCameraHelper.startCamera();
	// 	// 	}
	// 	// 	break;
	// 	// case REQUEST_CODE_MICROPHONE_PERMISSION:
	// 	// 	if (permissionsGranted(permissions, grantResults)) {
	// 	// 		NativeApp.audioRecording_Start();
	// 	// 	}
	// 	// 	break;
	// 	// default:
	// 	// }
 //
	// }
}
