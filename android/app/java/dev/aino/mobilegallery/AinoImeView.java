package dev.aino.mobilegallery;

import android.app.Activity;
import android.content.Context;
import android.text.InputType;
import android.view.KeyEvent;
import android.view.View;
import android.view.ViewGroup;
import android.view.inputmethod.BaseInputConnection;
import android.view.inputmethod.EditorInfo;
import android.view.inputmethod.InputConnection;
import android.view.inputmethod.InputMethodManager;

/**
 * aino 的软键盘桥。
 *
 * Android 11+ 只有"能提供 InputConnection 的 View"才允许弹软键盘——
 * NativeActivity 自带的 NativeContentView 没有实现，纯 C 调
 * ANativeActivity_showSoftInput 会被 ImeTracker 在 PHASE_CLIENT_VIEW_SERVED
 * 拒绝。所以往 decor view 挂一个 1x1 的可聚焦 View，由它 serve
 * InputConnection，IME 的提交文字经 JNI 直接回传 C++（拼音组字提交的
 * 中文整串送达）；退格 / 回车退化成按键事件，走 aino 现有的按键管线。
 */
public class AinoImeView extends View {
    private static volatile AinoImeView instance;

    static {
        // libmain.so 已被 NativeActivity 加载过，这里只是把它注册进
        // 本类的类加载器，静态 native 方法才能绑定到 JNI 实现。
        System.loadLibrary("main");
    }

    public AinoImeView(Activity activity) {
        super(activity);
        setFocusable(true);
        setFocusableInTouchMode(true);
    }

    /** 创建并挂到 decor view 上；幂等，应用生命周期只挂一次。
     *  注意这里**不能** requestFocus：本 View 声明了 onCheckIsTextEditor，
     *  一旦聚焦，系统会在窗口获得焦点时自动弹键盘（打开应用即弹）。
     *  焦点改由 showIme() 在用户点输入框时再抢。 */
    public static void attach(final Activity activity) {
        if (instance != null) return;
        activity.getWindow().getDecorView().post(new Runnable() {
            @Override public void run() {
                if (instance != null) return;
                AinoImeView view = new AinoImeView(activity);
                ViewGroup decor = (ViewGroup) activity.getWindow().getDecorView();
                // 窗口获得焦点时不自动弹键盘（双保险）
                activity.getWindow().setSoftInputMode(
                    android.view.WindowManager.LayoutParams.SOFT_INPUT_STATE_ALWAYS_HIDDEN);
                // 键盘高度（ime insets）实时回传 C++：UI 据此收缩视口，
                // 避免聚焦的输入框被键盘盖住
                view.setOnApplyWindowInsetsListener((v, insets) -> {
                    int bottom = android.os.Build.VERSION.SDK_INT >= 30
                        ? insets.getInsets(android.view.WindowInsets.Type.ime()).bottom
                        : insets.getSystemWindowInsetBottom();
                    imeInsets(bottom);
                    return insets;
                });
                decor.addView(view, new ViewGroup.LayoutParams(1, 1));
                instance = view;
            }
        });
    }

    public static void showIme(Activity activity) {
        final AinoImeView view = instance;
        if (view == null) return;
        // C++ 从胶水线程调这里；View/IMM 操作必须 post 到主线程
        view.post(() -> {
            // 此时才聚焦：IMM 只 serve 焦点 View，弹键盘前必须先抢到焦点
            view.requestFocus();
            InputMethodManager imm = (InputMethodManager)
                activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.showSoftInput(view, 0);
        });
    }

    public static void hideIme(Activity activity) {
        final AinoImeView view = instance;
        if (view == null) return;
        view.post(() -> {
            view.clearFocus();
            InputMethodManager imm = (InputMethodManager)
                activity.getSystemService(Context.INPUT_METHOD_SERVICE);
            if (imm != null) imm.hideSoftInputFromWindow(view.getWindowToken(), 0);
        });
    }

    @Override public boolean onCheckIsTextEditor() { return true; }

    @Override public InputConnection onCreateInputConnection(EditorInfo outAttrs) {
        outAttrs.inputType = InputType.TYPE_CLASS_TEXT;
        outAttrs.imeOptions = EditorInfo.IME_FLAG_NO_FULLSCREEN | EditorInfo.IME_ACTION_DONE;
        return new ImeConnection(this, false);
    }

    /** IME 的提交文字回传 C++（aino.hpp 里 Java_dev_aino_mobilegallery_AinoImeView_imeCommitText）。 */
    public static native void imeCommitText(String text);

    /** 退格 / 回车等作为按键注入（对应 AKEYCODE_DEL / AKEYCODE_ENTER）。 */
    public static native void imeKey(int keycode);

    /** 键盘可见高度（ime insets 的 bottom）回传，C++ 据此收缩视口。 */
    public static native void imeInsets(int bottom);

    /** 不带按键翻译的 InputConnection：文本与按键直接转发，组字串吞掉
     *  （P1 不画组字预览，只取 IME 的最终提交结果）。 */
    private static final class ImeConnection extends BaseInputConnection {
        ImeConnection(View view, boolean fullEditor) { super(view, fullEditor); }

        @Override public boolean commitText(CharSequence text, int newCursorPosition) {
            imeCommitText(text.toString());
            return true;
        }

        @Override public boolean setComposingText(CharSequence text, int newCursorPosition) {
            return true;   // 组字过程不显示，只取最终提交
        }

        @Override public boolean setComposingRegion(int start, int end) {
            return true;
        }

        @Override public boolean deleteSurroundingText(int beforeLength, int afterLength) {
            imeKey(KeyEvent.KEYCODE_DEL);
            return true;
        }

        @Override public boolean performEditorAction(int actionCode) {
            imeKey(KeyEvent.KEYCODE_ENTER);
            return true;
        }
    }
}
