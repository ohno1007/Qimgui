#include "java_bridge.h"
#include <android/log.h>
#include <dlfcn.h>
#include <jni.h>
#include <mutex>
#include <string>
#include <vector>
#include <cerrno>
#include <cstring>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <chrono>
#include <thread>
#define LOG_TAG "AImGui"
#define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)
namespace aimgui::java_bridge {
namespace {
using GetCreatedVMs = jint (*)(JavaVM**, jsize, jsize*);
std::mutex g_mu; JavaVM* g_vm=nullptr; void* g_art=nullptr; std::string g_error; pid_t g_host_pid=-1;
std::string g_sock; std::string g_token;
void Error(const char* s){g_error=s?s:"error";}
void ClearException(JNIEnv* e,const char* where){if(!e->ExceptionCheck())return; e->ExceptionClear(); Error(where);}
JNIEnv* Env(bool* attached){*attached=false; if(!g_vm){g_art=dlopen("libart.so",RTLD_NOW|RTLD_LOCAL); if(!g_art){Error("libart unavailable");return nullptr;} auto get=(GetCreatedVMs)dlsym(g_art,"JNI_GetCreatedJavaVMs"); jsize n=0; if(!get||get(&g_vm,1,&n)!=JNI_OK||n<1||!g_vm){Error("no running Java VM");return nullptr;}} JNIEnv* e=nullptr; jint st=g_vm->GetEnv((void**)&e,JNI_VERSION_1_6); if(st==JNI_EDETACHED){if(g_vm->AttachCurrentThread(&e,nullptr)!=JNI_OK){Error("AttachCurrentThread failed");return nullptr;}*attached=true;} else if(st!=JNI_OK){Error("GetEnv failed");return nullptr;} return e;}
int Connect(){if(g_sock.empty())return -1; int fd=socket(AF_UNIX,SOCK_STREAM,0); if(fd<0)return -1; sockaddr_un a{}; a.sun_family=AF_UNIX; std::strncpy(a.sun_path,g_sock.c_str(),sizeof(a.sun_path)-1); if(connect(fd,(sockaddr*)&a,sizeof(a))<0){close(fd);return -1;} return fd;}
int StartHost(){if(g_sock.empty()){char b[96]; std::snprintf(b,sizeof(b),"/data/local/tmp/aimgui-ime-%d.sock",(int)getpid()); g_sock=b; char t[64]; std::snprintf(t,sizeof(t),"%08x%08x",(unsigned)getpid(),(unsigned)std::chrono::steady_clock::now().time_since_epoch().count()); g_token=t;} if(g_host_pid>0){if(kill(g_host_pid,0)==0)return Connect(); g_host_pid=-1;} pid_t p=fork(); if(p==0){execl("/system/bin/app_process","app_process","-Djava.class.path=/data/local/tmp/ime-host.dex","/system/bin","ImeHost",g_sock.c_str(),g_token.c_str(),(char*)nullptr); _exit(127);} if(p<0){Error("fork app_process failed");return -1;} g_host_pid=p; for(int i=0;i<60;i++){int fd=Connect(); if(fd>=0)return fd; std::this_thread::sleep_for(std::chrono::milliseconds(40));} Error("IME host unavailable"); return -1;}
int HostFd(){int fd=Connect(); return fd>=0?fd:StartHost();}
bool HostCommand(const std::string& q,std::string* out){int fd=HostFd(); if(fd<0)return false; std::string line=g_token+" "+q+"\n"; const char* p=line.data(); size_t left=line.size(); while(left){ssize_t n=write(fd,p,left); if(n<=0){close(fd);Error("IME host write failed");return false;} p+=n;left-=n;} std::string r; char b[256]; while(r.find('\n')==std::string::npos){ssize_t n=read(fd,b,sizeof(b)); if(n<=0)break; r.append(b,(size_t)n);} close(fd); if(!r.empty()&&r.back()=='\n')r.pop_back(); if(r.rfind("ERR ",0)==0){Error(r.c_str()+4);return false;} if(out)*out=r; return true;}
static const char B64[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
std::string B64Encode(const std::string& s){std::string o; for(size_t i=0;i<s.size();i+=3){unsigned v=(unsigned char)s[i]<<16; if(i+1<s.size())v|=(unsigned char)s[i+1]<<8; if(i+2<s.size())v|=(unsigned char)s[i+2]; o.push_back(B64[(v>>18)&63]);o.push_back(B64[(v>>12)&63]);o.push_back(i+1<s.size()?B64[(v>>6)&63]:'=');o.push_back(i+2<s.size()?B64[v&63]:'=');} return o;}
std::string B64Decode(const std::string& s){std::vector<int> t(256,-1);for(int i=0;i<64;i++)t[(unsigned)B64[i]]=i;std::string o;int v=0,b=-8;for(unsigned char c:s){if(c=='=')break;if(t[c]<0)continue;v=(v<<6)|t[c];b+=6;if(b>=0){o.push_back((char)((v>>b)&255));b-=8;}}return o;}
jobject Context(JNIEnv* e){jclass at=e->FindClass("android/app/ActivityThread");if(!at){ClearException(e,"ActivityThread unavailable");return nullptr;}jmethodID m=e->GetStaticMethodID(at,"currentApplication","()Landroid/app/Application;");jobject app=m?e->CallStaticObjectMethod(at,m):nullptr;e->DeleteLocalRef(at);if(!app)ClearException(e,"no current Application");return app;}
jobject Service(JNIEnv* e,jobject c,const char*n){jclass cc=e->GetObjectClass(c);jmethodID m=e->GetMethodID(cc,"getSystemService","(Ljava/lang/String;)Ljava/lang/Object;");jstring s=e->NewStringUTF(n);jobject o=m?e->CallObjectMethod(c,m,s):nullptr;e->DeleteLocalRef(s);e->DeleteLocalRef(cc);if(!o)ClearException(e,"system service unavailable");return o;}
bool DirectSet(JNIEnv*e,const char*t){jobject c=Context(e);if(!c)return false;jobject cm=Service(e,c,"clipboard");jclass cd=e->FindClass("android/content/ClipData");jmethodID mk=cd?e->GetStaticMethodID(cd,"newPlainText","(Ljava/lang/CharSequence;Ljava/lang/CharSequence;)Landroid/content/ClipData;"):nullptr;jclass mc=cm?e->GetObjectClass(cm):nullptr;jmethodID set=mc?e->GetMethodID(mc,"setPrimaryClip","(Landroid/content/ClipData;)V"):nullptr;jstring l=e->NewStringUTF("AImGui"),v=e->NewStringUTF(t?t:"");jobject clip=mk?e->CallStaticObjectMethod(cd,mk,l,v):nullptr;bool ok=set&&clip; if(ok) e->CallVoidMethod(cm,set,clip); ok=ok&&!e->ExceptionCheck();e->DeleteLocalRef(l);e->DeleteLocalRef(v);if(clip)e->DeleteLocalRef(clip);if(mc)e->DeleteLocalRef(mc);if(cd)e->DeleteLocalRef(cd);if(cm)e->DeleteLocalRef(cm);e->DeleteLocalRef(c);if(!ok)ClearException(e,"setPrimaryClip failed");return ok;}
}
bool Available(){std::lock_guard<std::mutex>l(g_mu);bool a=false;JNIEnv*e=Env(&a);if(a&&g_vm)g_vm->DetachCurrentThread();return e||Connect()>=0;}
const char* LastError(){std::lock_guard<std::mutex>l(g_mu);return g_error.c_str();}
bool SetClipboard(const char*t){std::lock_guard<std::mutex>l(g_mu);bool a=false;JNIEnv*e=Env(&a);bool ok=e&&DirectSet(e,t);if(a&&g_vm)g_vm->DetachCurrentThread();if(ok)return true;return HostCommand(std::string("CLIPSET ")+B64Encode(t?t:""),nullptr);}
bool GetClipboard(std::string*out){if(!out)return false;std::lock_guard<std::mutex>l(g_mu);std::string r;if(HostCommand("CLIPGET",&r)){*out=B64Decode(r);return true;}return false;}
bool ShowInputMethod(){std::lock_guard<std::mutex>l(g_mu);std::string r;if(HostCommand("SHOW",&r))return r=="OK";return false;}
bool GetInputText(std::string*out){if(!out)return false;std::lock_guard<std::mutex>l(g_mu);std::string r;if(!HostCommand("GET",&r))return false;*out=B64Decode(r);return true;}
bool HideInputMethod(){std::lock_guard<std::mutex>l(g_mu);std::string r;return HostCommand("HIDE",&r)&&r=="OK";}
}
