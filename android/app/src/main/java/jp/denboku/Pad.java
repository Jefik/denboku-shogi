package jp.denboku;

import android.content.Context;
import android.hardware.input.InputManager;
import android.os.Build;
import android.os.VibrationEffect;
import android.os.Vibrator;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;

/* Game controllers. Android hands them to the app as keys and joystick motion (/dev/input is
 * closed to apps); their inputs go to the game as one bit set in PadInput's order
 * (src/core/app.h), "g BITS", after "gn DRIVER NAME" when another controller is used. A button
 * the app does not take becomes a key of its own: B, for one, would be Back. */
final class Pad {
    private static final int A = 0, B = 1, X = 2, Y = 3, LB = 4, RB = 5, LT = 6, RT = 7, BACK = 8, START = 9,
        LS = 10, RS = 11, UP = 12, DOWN = 13, LEFT = 14, RIGHT = 15, STICK_UP = 16, STICK_DOWN = 17,
        STICK_LEFT = 18, STICK_RIGHT = 19, RSTICK_UP = 20, RSTICK_DOWN = 21;
    /* Plain DirectInput controllers without a key layout: numbered buttons, in the order of
     * gamepad.c's BTN_TRIGGER… on Linux. */
    private static final int[] NUMBERED = {A, B, X, Y, LB, RB, LT, RT, BACK, START, LS, RS};

    private static int keys, axes, sent, device = -1;
    private static boolean watching; /* once for the process: a new activity does not add another listener */

    static void watch(Context c) {
        if (watching)
            return;
        watching = true;
        InputManager im = (InputManager) c.getApplicationContext().getSystemService(Context.INPUT_SERVICE);
        im.registerInputDeviceListener(new InputManager.InputDeviceListener() {
            @Override public void onInputDeviceAdded(int id) {}
            @Override public void onInputDeviceChanged(int id) {}
            @Override public void onInputDeviceRemoved(int id) {
                if (id == device) {
                    keys = axes = 0;
                    send();
                }
            }
        }, null);
    }

    private static boolean isPad(InputDevice d) {
        if (d == null || d.isVirtual())
            return false;
        int s = d.getSources();
        return (s & InputDevice.SOURCE_GAMEPAD) == InputDevice.SOURCE_GAMEPAD ||
               (s & InputDevice.SOURCE_JOYSTICK) == InputDevice.SOURCE_JOYSTICK;
    }

    private static int input(int code) {
        switch (code) {
        case KeyEvent.KEYCODE_BUTTON_A: return A;
        case KeyEvent.KEYCODE_BUTTON_B: return B;
        case KeyEvent.KEYCODE_BUTTON_X: return X;
        case KeyEvent.KEYCODE_BUTTON_Y: return Y;
        case KeyEvent.KEYCODE_BUTTON_L1: return LB;
        case KeyEvent.KEYCODE_BUTTON_R1: return RB;
        case KeyEvent.KEYCODE_BUTTON_L2: return LT;
        case KeyEvent.KEYCODE_BUTTON_R2: return RT;
        case KeyEvent.KEYCODE_BUTTON_SELECT: case KeyEvent.KEYCODE_BACK: return BACK;
        case KeyEvent.KEYCODE_BUTTON_START: case KeyEvent.KEYCODE_MENU: return START;
        case KeyEvent.KEYCODE_BUTTON_THUMBL: return LS;
        case KeyEvent.KEYCODE_BUTTON_THUMBR: return RS;
        case KeyEvent.KEYCODE_DPAD_UP: return UP;
        case KeyEvent.KEYCODE_DPAD_DOWN: return DOWN;
        case KeyEvent.KEYCODE_DPAD_LEFT: return LEFT;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return RIGHT;
        case KeyEvent.KEYCODE_DPAD_CENTER: return A;
        }
        int n = code - KeyEvent.KEYCODE_BUTTON_1;
        return n >= 0 && n < NUMBERED.length ? NUMBERED[n] : -1;
    }

    private static void use(InputDevice d) {
        if (d.getId() == device)
            return;
        device = d.getId();
        keys = axes = 0;
        int vendor = d.getVendorId();
        String driver = vendor == 0x054c ? "sony" : vendor == 0x057e ? "nintendo" : "-";
        Game.send("gn " + driver + " " + d.getName().replace('\n', ' '));
    }

    private static void send() {
        int now = keys | axes;
        if (now != sent)
            Game.send("g " + now);
        sent = now;
    }

    /* Whether the key was a controller's (taken, or dropped when it means nothing here). */
    static boolean key(KeyEvent e) {
        InputDevice d = e.getDevice();
        int code = e.getKeyCode();
        if (!KeyEvent.isGamepadButton(code) && !isPad(d))
            return false;
        int in = input(code);
        if (in < 0)
            return KeyEvent.isGamepadButton(code);
        if (!Game.running() || d == null)
            return true;
        if (e.getRepeatCount() > 0)
            return true;
        use(d);
        if (e.getAction() == KeyEvent.ACTION_DOWN)
            keys |= 1 << in;
        else if (e.getAction() == KeyEvent.ACTION_UP)
            keys &= ~(1 << in);
        send();
        return true;
    }

    private static int bit(boolean on, int in) { return on ? 1 << in : 0; }

    /* Sticks, the hat and the triggers. */
    static boolean motion(MotionEvent e) {
        if (!e.isFromSource(InputDevice.SOURCE_JOYSTICK) || e.getActionMasked() != MotionEvent.ACTION_MOVE ||
            e.getDevice() == null)
            return false;
        if (!Game.running())
            return true;
        use(e.getDevice());
        float x = e.getAxisValue(MotionEvent.AXIS_X), y = e.getAxisValue(MotionEvent.AXIS_Y);
        float hx = e.getAxisValue(MotionEvent.AXIS_HAT_X), hy = e.getAxisValue(MotionEvent.AXIS_HAT_Y);
        float lt = Math.max(e.getAxisValue(MotionEvent.AXIS_LTRIGGER), e.getAxisValue(MotionEvent.AXIS_BRAKE));
        float rt = Math.max(e.getAxisValue(MotionEvent.AXIS_RTRIGGER), e.getAxisValue(MotionEvent.AXIS_GAS));
        float ry = e.getAxisValue(MotionEvent.AXIS_RZ); /* the right stick's vertical axis (RY on some) */
        if (ry == 0)
            ry = e.getAxisValue(MotionEvent.AXIS_RY);
        axes = bit(y < -0.5f, STICK_UP) | bit(y > 0.5f, STICK_DOWN) | bit(x < -0.5f, STICK_LEFT) |
               bit(x > 0.5f, STICK_RIGHT) | bit(hy < -0.5f, UP) | bit(hy > 0.5f, DOWN) | bit(hx < -0.5f, LEFT) |
               bit(hx > 0.5f, RIGHT) | bit(lt > 0.5f, LT) | bit(rt > 0.5f, RT) | bit(ry < -0.5f, RSTICK_UP) |
               bit(ry > 0.5f, RSTICK_DOWN);
        send();
        return true;
    }

    /* "MS STRONG WEAK", each motor 0-255, on the controller last used. */
    static void rumble(String args) {
        InputDevice d = InputDevice.getDevice(device);
        if (d == null)
            return;
        String[] a = args.split(" ");
        int ms = Integer.parseInt(a[0]), amp = Math.max(Integer.parseInt(a[1]), Integer.parseInt(a[2]));
        Vibrator v = Build.VERSION.SDK_INT >= 31 ? d.getVibratorManager().getDefaultVibrator() : d.getVibrator();
        if (v == null || !v.hasVibrator() || ms <= 0 || amp <= 0)
            return;
        if (Build.VERSION.SDK_INT >= 26)
            v.vibrate(VibrationEffect.createOneShot(ms, v.hasAmplitudeControl() ? Math.min(255, amp) : VibrationEffect.DEFAULT_AMPLITUDE));
        else
            v.vibrate(ms);
    }
}
