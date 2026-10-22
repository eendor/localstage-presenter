package io.github.eendor.localstage;

import android.app.Activity;
import android.app.NativeActivity;
import android.content.Intent;
import android.net.Uri;
import android.webkit.ValueCallback;
import android.webkit.WebChromeClient;
import android.webkit.WebView;

/* The app itself is C (app.c). This small class only lets the page's "choose file" button
   open Android's file picker, which a WebView can't do without it. */
public class Main extends NativeActivity {
    static ValueCallback<Uri[]> pending;

    public static class Chooser extends WebChromeClient {
        final Activity act;
        public Chooser(Activity a) { act = a; }
        @Override
        public boolean onShowFileChooser(WebView v, ValueCallback<Uri[]> cb, FileChooserParams p) {
            if (pending != null) pending.onReceiveValue(null);
            pending = cb;
            Intent i = new Intent(Intent.ACTION_GET_CONTENT);
            i.addCategory(Intent.CATEGORY_OPENABLE);
            i.setType("*/*");
            i.putExtra(Intent.EXTRA_MIME_TYPES, new String[]{"application/pdf",
                "application/vnd.openxmlformats-officedocument.presentationml.presentation",
                "application/vnd.ms-powerpoint"});
            try { act.startActivityForResult(Intent.createChooser(i, "Choose slides"), 7); }
            catch (Exception e) { pending = null; cb.onReceiveValue(null); return false; }
            return true;
        }
    }

    @Override
    protected void onActivityResult(int req, int res, Intent data) {
        super.onActivityResult(req, res, data);
        if (req != 7 || pending == null) return;
        pending.onReceiveValue(WebChromeClient.FileChooserParams.parseResult(res, data));
        pending = null;
    }
}
