import android.content.ClipData;
import android.content.ClipboardManager;
import android.content.Context;
import android.graphics.PixelFormat;
import android.util.Base64;
import android.view.Gravity;
import android.view.WindowManager;
import android.view.inputmethod.InputMethodManager;
import android.widget.EditText;
import java.io.*;
import android.net.LocalServerSocket;
import android.net.LocalSocket;

public final class ImeHost {
  static String SOCK="/data/local/tmp/aimgui-ime.sock"; static String TOKEN="";
  static Context ctx; static EditText edit; static WindowManager wm;
  static Context context() throws Exception {
    Class<?> c=Class.forName("android.app.ActivityThread");
    java.lang.reflect.Method m=c.getDeclaredMethod("currentApplication"); m.setAccessible(true);
    Object app=m.invoke(null); if(app!=null)return (Context)app;
    java.lang.reflect.Method sm=c.getDeclaredMethod("systemMain"); sm.setAccessible(true);
    Object at=sm.invoke(null); java.lang.reflect.Method gc=at.getClass().getDeclaredMethod("getSystemContext"); gc.setAccessible(true);
    return (Context)gc.invoke(at);
  }
  static void show() throws Exception {
    if(edit!=null)return; ctx=context(); if(ctx==null)throw new RuntimeException("no Application");
    wm=(WindowManager)ctx.getSystemService(Context.WINDOW_SERVICE); edit=new EditText(ctx); edit.setSingleLine(false); edit.setAlpha(0.01f);
    WindowManager.LayoutParams p=new WindowManager.LayoutParams(2,2,WindowManager.LayoutParams.TYPE_APPLICATION_OVERLAY,
      WindowManager.LayoutParams.FLAG_NOT_TOUCH_MODAL|WindowManager.LayoutParams.FLAG_ALT_FOCUSABLE_IM,PixelFormat.TRANSLUCENT);
    p.gravity=Gravity.TOP|Gravity.LEFT; wm.addView(edit,p); edit.requestFocus();
    ((InputMethodManager)ctx.getSystemService(Context.INPUT_METHOD_SERVICE)).showSoftInput(edit,InputMethodManager.SHOW_IMPLICIT);
  }
  static void hide(){ if(edit!=null){((InputMethodManager)ctx.getSystemService(Context.INPUT_METHOD_SERVICE)).hideSoftInputFromWindow(edit.getWindowToken(),0); wm.removeViewImmediate(edit); edit=null;} }
  static String enc(String s){return Base64.encodeToString(s.getBytes(java.nio.charset.StandardCharsets.UTF_8),Base64.NO_WRAP);}
  static String dec(String s){return new String(Base64.decode(s,Base64.DEFAULT),java.nio.charset.StandardCharsets.UTF_8);}
  static String clipGet() throws Exception { ClipboardManager cm=(ClipboardManager)context().getSystemService(Context.CLIPBOARD_SERVICE); if(!cm.hasPrimaryClip())return ""; CharSequence t=cm.getPrimaryClip().getItemAt(0).coerceToText(context()); return t==null?"":t.toString(); }
  static void clipSet(String s) throws Exception { ClipboardManager cm=(ClipboardManager)context().getSystemService(Context.CLIPBOARD_SERVICE); cm.setPrimaryClip(ClipData.newPlainText("AImGui",s)); }
  static String command(String q) throws Exception {
    if("SHOW".equals(q)){show();return "OK";} if("HIDE".equals(q)){hide();return "OK";}
    if("GET".equals(q))return edit==null?"":enc(edit.getText().toString());
    if("CLIPGET".equals(q))return enc(clipGet());
    if(q.startsWith("CLIPSET ")){clipSet(dec(q.substring(8)));return "OK";}
    return "ERR unknown command";
  }
  public static void main(String[] a)throws Exception{ if(a.length>0)SOCK=a[0]; if(a.length>1)TOKEN=a[1]; new File(SOCK).delete(); final LocalServerSocket s=new LocalServerSocket(SOCK); File sf=new File(SOCK); sf.setReadable(false,false); sf.setWritable(false,false); sf.setReadable(true,true); sf.setWritable(true,true); for(;;){ try(LocalSocket c=s.accept()){ BufferedReader r=new BufferedReader(new InputStreamReader(c.getInputStream())); BufferedWriter w=new BufferedWriter(new OutputStreamWriter(c.getOutputStream())); String q=r.readLine(); String ans; if(q==null||!q.startsWith(TOKEN+" ")){ans="ERR unauthorized";} else try{ans=command(q.substring(TOKEN.length()+1));}catch(Throwable t){ans="ERR "+t.getClass().getSimpleName()+" "+String.valueOf(t.getMessage());} w.write(ans+"\n"); w.flush(); }} }
}
