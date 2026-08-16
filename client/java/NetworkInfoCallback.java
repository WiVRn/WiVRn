package org.meumeu.wivrn;

public class NetworkInfoCallback extends android.net.ConnectivityManager.NetworkCallback
{
	@Override
	public native void onAvailable(android.net.Network network);

	@Override
	public native void onBlockedStatusChanged(android.net.Network network, boolean blocked);

	@Override
	public native void onCapabilitiesChanged(android.net.Network network, android.net.NetworkCapabilities caps);

	@Override
	public native void onLinkPropertiesChanged(android.net.Network network, android.net.LinkProperties props);

	@Override
	public native void onLosing(android.net.Network network, int maxMsToLive);

	@Override
	public native void onLost(android.net.Network network);

	@Override
	public native void onUnavailable();
}

