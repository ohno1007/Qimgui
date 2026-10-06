import android.content.Context;
import android.graphics.PixelFormat;
import android.view.Gravity;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import java.io.*;
import android.net.LocalServerSocket;
import android.net.LocalSocket;

public final class ImeHost {
  static final String SOCK="/data/local/tmp/aimgui-ime.sock";
  static Context ctx; static EditText edit; static WindowManager wm;
  static Context context() throws Exception {
    Class<?> c=Class.forName("android.app.ActivityThread");
    java.lang.reflect.Method m=c.getDeclaredMethod("currentApplication"); m.setAccessible(true);
    Object app=m.invoke(null);
    if(app!=null)return (Context)app;
    java.lang.reflect.Method sm=c.getDeclaredMethod("systemMain"); sm.setAccessible(true);
    Object at=sm.invoke(null); java.lang.reflect.Method gc=at.getClass().getDeclaredMethod("getSystemContext"); gc.setAccessible(true);
    return (Context)gc.invoke(at);
  }
  static void show() throws Exception { if(edit!=null)return; ctx=context(); if(ctx==null)throw new RuntimeException("no Application"); wm=(WindowManager)ctx.getSystemService(Context.WINDOW_SERVICE); edit=new EditText(ctx); edit.setSingleLine(false); edit.setAlpha(0.01f); WindowManager.LayoutParams p=new WindowManager.LayoutParams(2,2,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL|WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM,PixelFormat.TRANSLUCENT); p.gravity=Gravity.TOP|Gravity.LEFT; wm.addView(edit,p); edit.requestFocus(); ((InputMethodManager)ctx.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(edit,InputMethodManager.SHOW_IMPLICIT); }
  static void hide(){ if(edit!=null){((InputMethodManager)ctx.getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(edit.getWindowToken(),0); wm.removeViewImmediate(edit); edit=null;} }
  public static void main(String[] a)throws Exception{ new File(SOCK).delete(); final LocalServerSocket s=new LocalServerSocket(SOCK); for(;;){ try(LocalSocket c=s.accept()){ BufferedReader r=new BufferedReader(new InputStreamReader(c.getInputStream())); BufferedWriter w=new BufferedWriter(new OutputStreamWriter(c.getOutputStream())); String q=r.readLine(); if("SHOW".equals(q)){show();w.write("OK\n");} else if("GET".equals(q)){w.write((edit==null?"":edit.getText().toString()).replace("\n","\\n")+"\n");} else if("HIDE".equals(q)){hide();w.write("OK\n");} w.flush(); }} }
}
