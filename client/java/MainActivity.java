package org.meumeu.wivrn;

public class MainActivity extends android.app.NativeActivity
{
	public static native void sendMessageFromJava(String msg, String arg);

	static
	{
		System.loadLibrary("wivrn");
	}

	@Override
	public native void onRequestPermissionsResult(int requestCode, String[] permissions, int[] grantResults);

	@Override
	public native void onNewIntent(android.content.Intent intent);
}
