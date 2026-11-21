package jp.denboku;

import android.content.Context;
import android.text.Editable;
import android.text.InputType;
import android.text.Selection;
import android.text.SpannableStringBuilder;
import android.view.GestureDetector;
import android.view.HapticFeedbackConstants;
import android.view.InputDevice;
import android.view.KeyEvent;
import android.view.MotionEvent;
import android.view.SurfaceHolder;
import android.view.SurfaceView;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/* The X screen, and the fingers, mouse and keys that go to it (src/core/android.c reads them). */
final class Screen extends SurfaceView implements SurfaceHolder.Callback {
    static {
        System.loadLibrary("screen");
    }

    private static native boolean open(String path);
    private static native void attach(Object surface, int w, int h);
    private static native void draw(int x, int y, int w, int h);

    private static final Object lock = new Object();
    private static Screen shown; /* whose surface the game draws on */
    private int width, height, keyboardHeight, sentW, sentH, sentK = -1;
    /* The on-screen keyboard's copy of the text typed (onCreateInputConnection), and what of it the game has. */
    private Editable copy = new SpannableStringBuilder();
    private String sent = "";
    /* The field's text as the keyboard asks for it (getExtractedText), and its request to be told each
     * change (-1 without one), as Android's fields answer: BaseInputConnection answers nothing, and
     * SwiftKey went on with its own copy, the line sent included (written again in kana on Japanese). */
    private int extractToken = -1;

    private android.view.inputmethod.ExtractedText extracted() {
        android.view.inputmethod.ExtractedText t = new android.view.inputmethod.ExtractedText();
        t.text = copy.toString();
        t.startOffset = 0;
        t.partialStartOffset = t.partialEndOffset = -1;
        t.selectionStart = Math.max(0, Selection.getSelectionStart(copy));
        t.selectionEnd = Math.max(0, Selection.getSelectionEnd(copy));
        return t;
    }

    private void tellText() {
        if (extractToken >= 0)
            ((InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE)).updateExtractedText(this, extractToken, extracted());
    }
    private float scrollY;
    private boolean typing; /* a text field of the game has the focus */
    private boolean scrolling;

    /* Android's long press, as on its own text fields: the game shows the text toolbar there if it
     * was on a field. */
    private final GestureDetector longPress;

    Screen(Context c) {
        super(c);
        longPress = new GestureDetector(c, new GestureDetector.SimpleOnGestureListener() {
            @Override public void onLongPress(MotionEvent e) {
                performHapticFeedback(HapticFeedbackConstants.LONG_PRESS);
                Game.send("h");
            }
        });
        getHolder().addCallback(this);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    /* The game's view: the surface, less what the on-screen keyboard covers. */
    void sendView() {
        int w = width, h = Math.max(1, height - keyboardHeight);
        int k = Math.min(keyboardHeight, height);
        if (w > 0 && (w != sentW || h != sentH || k != sentK) && Game.running()) {
            Game.send("v " + w + " " + h + " " + k);
            sentW = w;
            sentH = h;
            sentK = k;
        }
    }

    void setKeyboardHeight(int h) {
        keyboardHeight = h;
        sendView();
    }

    /* The X screen's file; not while it is being drawn from. */
    static boolean map(String path) {
        synchronized (lock) {
            return open(path);
        }
    }

    void frame(int x, int y, int w, int h) {
        synchronized (lock) {
            draw(x, y, w, h);
        }
    }

    @Override public void surfaceCreated(SurfaceHolder h) {}

    @Override public void surfaceChanged(SurfaceHolder holder, int format, int w, int h) {
        synchronized (lock) {
            attach(holder.getSurface(), w, h);
            draw(0, 0, w, h);
            shown = this;
        }
        width = w;
        height = h;
        sendView();
    }

    @Override public void surfaceDestroyed(SurfaceHolder h) {
        synchronized (lock) {
            if (shown == this) { /* not the new activity's, if the old one closes after it opened */
                attach(null, 0, 0);
                shown = null;
            }
        }
    }

    /* Below the view, so no button under it stays lit after a tap. */
    private void park() { Game.send("p"); }

    private void releaseAway() { Game.send("u 0 " + (height - keyboardHeight)); }

    /* Where the finger is, and when (the game measures a flick by Android's times, not by when it got the moves). */
    private static String at(MotionEvent e, int i) { return " " + (int) e.getX(i) + " " + (int) e.getY(i) + " " + e.getEventTime(); }

    @Override public boolean onTouchEvent(MotionEvent e) {
        boolean mouse = e.isFromSource(InputDevice.SOURCE_MOUSE);
        if (!mouse)
            longPress.onTouchEvent(e);
        switch (e.getActionMasked()) {
        case MotionEvent.ACTION_DOWN:
            requestFocus();
            scrolling = false;
            Game.send("d" + at(e, 0));
            break;
        case MotionEvent.ACTION_POINTER_DOWN:
            /* Two fingers scroll, as a mouse wheel: the press of the first one is let go away
             * from what it was on. */
            if (!scrolling) {
                releaseAway();
                scrolling = true;
            }
            scrollY = e.getY(0);
            break;
        case MotionEvent.ACTION_MOVE:
            if (scrolling) {
                float step = getResources().getDisplayMetrics().density * 24, dy = e.getY(0) - scrollY;
                if (Math.abs(dy) >= step) {
                    Game.send("m" + at(e, 0));
                    for (int i = 0; i < (int) (Math.abs(dy) / step); i++)
                        Game.send(dy > 0 ? "w 4" : "w 5");
                    scrollY = e.getY(0);
                    park();
                }
            } else
                Game.send("m" + at(e, 0));
            break;
        case MotionEvent.ACTION_UP:
            if (!scrolling) {
                Game.send("u" + at(e, 0));
                if (!mouse)
                    park();
            }
            break;
        case MotionEvent.ACTION_CANCEL:
            if (!scrolling)
                releaseAway();
            break;
        }
        return true;
    }

    /* A mouse points as on a computer. */
    @Override public boolean onGenericMotionEvent(MotionEvent e) {
        switch (e.getActionMasked()) {
        case MotionEvent.ACTION_HOVER_MOVE:
            Game.send("m" + at(e, 0));
            return true;
        case MotionEvent.ACTION_HOVER_EXIT:
            park();
            return true;
        case MotionEvent.ACTION_SCROLL:
            Game.send("m" + at(e, 0));
            float v = e.getAxisValue(MotionEvent.AXIS_VSCROLL);
            if (v != 0)
                Game.send(v > 0 ? "w 4" : "w 5");
            return true;
        }
        return super.onGenericMotionEvent(e);
    }

    /* Android key codes as X keysyms (the rest are typed as their character). */
    private static int keysym(int code) {
        switch (code) {
        case KeyEvent.KEYCODE_DEL: return 0xff08;
        case KeyEvent.KEYCODE_TAB: return 0xff09;
        case KeyEvent.KEYCODE_ENTER: case KeyEvent.KEYCODE_NUMPAD_ENTER: return 0xff0d;
        case KeyEvent.KEYCODE_ESCAPE: return 0xff1b;
        case KeyEvent.KEYCODE_MOVE_HOME: return 0xff50;
        case KeyEvent.KEYCODE_DPAD_LEFT: return 0xff51;
        case KeyEvent.KEYCODE_DPAD_UP: return 0xff52;
        case KeyEvent.KEYCODE_DPAD_RIGHT: return 0xff53;
        case KeyEvent.KEYCODE_DPAD_DOWN: return 0xff54;
        case KeyEvent.KEYCODE_PAGE_UP: return 0xff55;
        case KeyEvent.KEYCODE_PAGE_DOWN: return 0xff56;
        case KeyEvent.KEYCODE_MOVE_END: return 0xff57;
        case KeyEvent.KEYCODE_INSERT: return 0xff63;
        case KeyEvent.KEYCODE_FORWARD_DEL: return 0xffff;
        case KeyEvent.KEYCODE_SPACE: return 0x20;
        }
        if (code >= KeyEvent.KEYCODE_F1 && code <= KeyEvent.KEYCODE_F12)
            return 0xffbe + code - KeyEvent.KEYCODE_F1;
        return 0;
    }

    static void typeChar(int c) { Game.send("k " + (c < 0x100 ? c : 0x1000000 + c)); }

    @Override public boolean onKeyDown(int code, KeyEvent e) {
        if (code == KeyEvent.KEYCODE_BACK) {
            Game.send("b");
            return true;
        }
        int ks = keysym(code), c = e.getUnicodeChar();
        if (ks != 0)
            Game.send("k " + ks);
        else if (c > 0)
            typeChar(c);
        else
            return super.onKeyDown(code, e);
        return true;
    }

    void keyboard(boolean show) {
        InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        typing = show;
        if (show) {
            requestFocus();
            imm.restartInput(this);
            imm.showSoftInput(this, 0);
        } else {
            imm.hideSoftInputFromWindow(getWindowToken(), 0);
            imm.restartInput(this);
        }
    }

    /* The game emptied the field (a chat line sent): the keyboard's copy too, and the keyboard is told,
     * or it kept the line and, on another layout (Japanese), read it back and wrote it again in kana. */
    void resetInput() {
        copy.clear();
        Selection.setSelection(copy, 0);
        sent = "";
        if (!typing)
            return;
        tellText();
        /* As Android's own fields do when their text is set: no composing word any more (SwiftKey kept
         * the keys of the last one and typed them again on another layout), then a new start. */
        InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
        imm.updateSelection(this, 0, 0, -1, -1);
        imm.restartInput(this);
    }

    /* Otherwise Android opens the keyboard by itself when the app comes to the front. */
    @Override public boolean onCheckIsTextEditor() { return typing; }

    /* The on-screen keyboard edits a copy of the text typed since it opened (Android keyboards
     * read it back to compose, convert and correct words), and each change goes to the game's field
     * as Backspaces back to where the copy changed and the new text after it. */
    @Override public InputConnection onCreateInputConnection(EditorInfo out) {
        out.inputType = InputType.TYPE_CLASS_TEXT;
        out.imeOptions = EditorInfo.IME_FLAG_NO_EXTRACT_UI | EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_DONE;
        out.initialSelStart = out.initialSelEnd = 0;
        copy = new SpannableStringBuilder();
        /* The cursor at the start, as in an empty field of Android's: without one the keyboard was
         * told -1, took the field for unknown and went on with the word before (SwiftKey's Japanese
         * then took no key at all). */
        Selection.setSelection(copy, 0);
        sent = "";
        extractToken = -1;
        return new BaseInputConnection(this, true) {
            @Override public Editable getEditable() { return copy; }

            @Override public android.view.inputmethod.ExtractedText getExtractedText(android.view.inputmethod.ExtractedTextRequest r, int flags) {
                if ((flags & GET_EXTRACTED_TEXT_MONITOR) != 0)
                    extractToken = r.token;
                return extracted();
            }

            private void enter() {
                Game.send("k 65293");
                copy.clear();
                Selection.setSelection(copy, 0);
                sent = "";
            }

            private boolean sync() {
                String now = copy.toString();
                int nl = now.indexOf('\n');
                if (nl >= 0)
                    now = now.substring(0, nl);
                int same = 0, max = Math.min(now.length(), sent.length());
                while (same < max && now.charAt(same) == sent.charAt(same))
                    same++;
                if (same > 0 && Character.isHighSurrogate(now.charAt(same - 1)))
                    same--;
                for (int i = sent.codePointCount(same, sent.length()); i > 0; i--)
                    Game.send("k 65288");
                if (same < now.length())
                    Game.send("t " + now.substring(same));
                sent = now;
                if (nl >= 0)
                    enter();
                tellText();
                InputMethodManager imm = (InputMethodManager) getContext().getSystemService(Context.INPUT_METHOD_SERVICE);
                imm.updateSelection(Screen.this, Selection.getSelectionStart(copy), Selection.getSelectionEnd(copy),
                                    getComposingSpanStart(copy), getComposingSpanEnd(copy));
                return true;
            }

            @Override public boolean commitText(CharSequence text, int pos) { super.commitText(text, pos); return sync(); }
            @Override public boolean setComposingText(CharSequence text, int pos) { super.setComposingText(text, pos); return sync(); }
            @Override public boolean setComposingRegion(int start, int end) { super.setComposingRegion(start, end); return sync(); }
            @Override public boolean finishComposingText() { super.finishComposingText(); return sync(); }
            @Override public boolean setSelection(int start, int end) { super.setSelection(start, end); return sync(); }
            @Override public boolean deleteSurroundingText(int before, int after) { super.deleteSurroundingText(before, after); return sync(); }
            @Override public boolean deleteSurroundingTextInCodePoints(int before, int after) {
                super.deleteSurroundingTextInCodePoints(before, after);
                return sync();
            }

            /* Keys typed as keys: Backspace and letters go into the copy too, the rest straight to the game. */
            @Override public boolean sendKeyEvent(KeyEvent e) {
                if (e.getAction() != KeyEvent.ACTION_DOWN)
                    return true;
                int code = e.getKeyCode(), c = e.getUnicodeChar();
                if (code == KeyEvent.KEYCODE_DEL && copy.length() > 0) {
                    int end = Selection.getSelectionEnd(copy);
                    if (end > 0) {
                        copy.delete(Character.offsetByCodePoints(copy, end, -1), end);
                        return sync();
                    }
                } else if (code == KeyEvent.KEYCODE_ENTER) {
                    finishComposingText();
                    enter();
                    return true;
                } else if (c > 0 && (keysym(code) == 0 || code == KeyEvent.KEYCODE_SPACE)) {
                    commitText(new String(Character.toChars(c)), 1);
                    return true;
                }
                onKeyDown(code, e);
                return true;
            }

            /* The keyboard's own Paste (its clipboard key or toolbar). */
            @Override public boolean performContextMenuAction(int id) {
                if (id == android.R.id.paste && getContext() instanceof MainActivity)
                    ((MainActivity) getContext()).pasteHere();
                return true;
            }

            @Override public boolean performEditorAction(int action) {
                finishComposingText();
                enter();
                return true;
            }
        };
    }
}
