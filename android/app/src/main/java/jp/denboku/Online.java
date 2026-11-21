package jp.denboku;

import android.app.Notification;
import android.app.NotificationChannel;
import android.app.NotificationManager;
import android.app.PendingIntent;
import android.app.Service;
import android.content.Context;
import android.content.Intent;
import android.content.pm.ServiceInfo;
import android.net.wifi.WifiManager;
import android.os.Build;
import android.os.IBinder;
import android.os.PowerManager;

/* While a network game is on (src/core/android.c says "online 1"): Android leaves the network,
 * the Wi-Fi and the processor to an app in the background only as a foreground service. */
public class Online extends Service {
    private static final String CHANNEL = "online";
    private PowerManager.WakeLock wake;
    private WifiManager.WifiLock wifi;

    static void set(Context c, boolean on) {
        Intent i = new Intent(c, Online.class);
        if (!on)
            c.stopService(i);
        else if (Build.VERSION.SDK_INT >= 26)
            c.startForegroundService(i);
        else
            c.startService(i);
    }

    @Override public int onStartCommand(Intent intent, int flags, int id) {
        Notification.Builder b;
        if (Build.VERSION.SDK_INT >= 26) {
            NotificationManager m = getSystemService(NotificationManager.class);
            m.createNotificationChannel(new NotificationChannel(CHANNEL, getString(R.string.online_channel), NotificationManager.IMPORTANCE_LOW));
            b = new Notification.Builder(this, CHANNEL);
        } else
            b = new Notification.Builder(this);
        Intent open = new Intent(this, MainActivity.class).setFlags(Intent.FLAG_ACTIVITY_NEW_TASK);
        Notification n = b.setSmallIcon(R.drawable.icon).setContentTitle(getString(R.string.app_name))
            .setContentText(getString(R.string.online_text)).setOngoing(true)
            .setContentIntent(PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE)).build();
        if (Build.VERSION.SDK_INT >= 34)
            startForeground(1, n, ServiceInfo.FOREGROUND_SERVICE_TYPE_SPECIAL_USE);
        else
            startForeground(1, n);
        if (wake == null) {
            wake = ((PowerManager) getSystemService(POWER_SERVICE)).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "denboku:online");
            wake.acquire();
            wifi = ((WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE))
                .createWifiLock(Build.VERSION.SDK_INT >= 29 ? WifiManager.WIFI_MODE_FULL_LOW_LATENCY : WifiManager.WIFI_MODE_FULL_HIGH_PERF, "denboku:online");
            wifi.acquire();
        }
        return START_NOT_STICKY;
    }

    /* The app swiped away from the recent apps: the game ends with it, and so does its notification. */
    @Override public void onTaskRemoved(Intent root) {
        stopForeground(true);
        stopSelf();
        android.os.Process.killProcess(android.os.Process.myPid());
    }

    @Override public void onDestroy() {
        if (wake != null) {
            wake.release();
            wifi.release();
        }
        super.onDestroy();
    }

    @Override public IBinder onBind(Intent i) { return null; }
}
