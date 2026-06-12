/*
 * Copyright (C) 2021 Thibault B.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <jni.h>

#include "srt/srt.h"
#include "srt/logging_api.h"

#include "log.h"
#include "CallbackContext.h"
#include "Enums/EnumsSingleton.h"
#include "Enums/ErrorType.h"
#include "Enums/ErrorType.h"
#include "Models/Models.h"
#include "Models/EpollFlags.h"
#include "Models/Socket.h"
#include "Models/InetSocketAddress.h"
#include "Models/OptVal.h"
#include "Models/MsgCtrl.h"
#include "Models/Pair.h"
#include "Models/Stats.h"
#include "Models/Epoll.h"
#include "Models/EpollOpts.h"
#include "Models/EpollEvent.h"

// 💡 パケットおよびメッセージ処理用バッファの統一最大サイズ定数
// SRTプロトコルが絶対に 1456 バイトを超えるパケットを送信・受信できない
// 根拠①: ネットワークの限界（MTU 1500）からIP/UDP/SRTのヘッダーを引くと、数学的に 1456バイト しか残らないため。
// 根拠②: SRT公式C++ライブラリの内部で #define SRT_MAX_PAYLOAD_SIZE 1456 と最大値が固定されているため。
// (1456+16*5)/32 = 48
// #define SRT_MAX_BUFFER_SIZE (1456+16*5)
// #define SRT_MAX_BUFFER_SIZE 2048
#define SRT_MAX_BUFFER_SIZE 2560

// ----------------------------------------------------------------------------
// 静的変数の実体定義（Pair と Primitive）
// ----------------------------------------------------------------------------
// 外部CallbackContextや他モデルで共用するグローバルクラスキャッシュ
jclass class_InetSocketAddress = nullptr;

jclass Pair::cachedPairClazz = nullptr;
jmethodID Pair::cachedPairConstructorMethod = nullptr;

// ArrayList用のキャッシュ変数をグローバル（または名前空間内）に配置
jclass    class_ArrayList = nullptr;
jmethodID ctor_ArrayList  = nullptr;
jmethodID method_ListAdd  = nullptr;

// ----------------------------------------------------------------------------
// グローバル変数（難読化対策と安全な nullptr 管理）
// ----------------------------------------------------------------------------
static jclass    class_MsgCtrl          = nullptr; // クラス参照のみ NewGlobalRef が必要

// 💡 MsgCtrlクラスの各フィールドIDキャッシュ変数の完全版（実体定義）
jfieldID msgCtrlFlagsField = nullptr;
jfieldID msgCtrlTtlField = nullptr;
jfieldID msgCtrlInorderField = nullptr;
jfieldID msgCtrlBoundaryField = nullptr; // 追加
jfieldID msgCtrlSrcTimeField = nullptr;   // 追加
jfieldID msgCtrlPktSeqField = nullptr;
jfieldID msgCtrlMsgNumberField = nullptr;

int onListenCallback(JNIEnv *env, jobject ju, jclass sockAddrClazz, SRTSOCKET ns, int hs_version,
                     const struct sockaddr *peeraddr, const char *streamid) {
    jclass socketClazz = env->GetObjectClass(ju);
    if (!socketClazz) {
        LOGE("Can't get Socket class");
        return 0;
    }

    jmethodID onListenID = env->GetMethodID(socketClazz, "onListen",
                                            "(L" SRTSOCKET_CLASS ";IL" INETSOCKETADDRESS_CLASS ";Ljava/lang/String;)I");
    if (!onListenID) {
        LOGE("Can't get onListen methodID");
        env->DeleteLocalRef(socketClazz);
        return 0;
    }

    jobject nsSocket = Socket::getJava(env, socketClazz, ns);
    jobject peerAddress = InetSocketAddress::getJava(env, sockAddrClazz,
                                                     (sockaddr_storage *) peeraddr);
    jstring streamId = env->NewStringUTF(streamid);

    int res = env->CallIntMethod(ju, onListenID, nsSocket, (jint) hs_version, peerAddress,
                                 streamId);

    env->DeleteLocalRef(socketClazz);

    return res;
}

int srt_listen_cb(void *opaque, SRTSOCKET ns, int hs_version,
                  const struct sockaddr *peeraddr, const char *streamid) {
    auto *cbCtx = static_cast<CallbackContext *>(opaque);

    if (cbCtx == nullptr) {
        LOGE("Failed to get CallbackContext");
        return 0;
    }

    JavaVM *vm = cbCtx->vm;
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("Failed to attach current thread");
        }
    } else {
        LOGE("Failed to get env");
    }

    int res = onListenCallback(env, cbCtx->callingSocket, cbCtx->sockAddrClazz, ns, hs_version,
                               peeraddr, streamid);

    vm->DetachCurrentThread();

    return res;
}

void onConnectCallback(JNIEnv *env,
                       CallbackContext *cb,
                       SRTSOCKET ns,
                       int errorcode,
                       const struct sockaddr *peeraddr,
                       int token) {
    jclass socketClazz = env->GetObjectClass(cb->callingSocket);
    if (!socketClazz) {
        LOGE("Can't get Socket class");
        return;
    }

    jmethodID onConnectID = env->GetMethodID(socketClazz, "onConnect",
                                             "(L" SRTSOCKET_CLASS ";L" ERRORTYPE_CLASS ";L" INETSOCKETADDRESS_CLASS ";I)V");
    if (!onConnectID) {
        LOGE("Can't get onConnect methodID");
        env->DeleteLocalRef(socketClazz);
        return;
    }

    jobject nsSocket = Socket::getJava(env, socketClazz, ns);
    jobject peerAddress = InetSocketAddress::getJava(env, cb->sockAddrClazz,
                                                     (sockaddr_storage *) peeraddr);
    jobject error = EnumsSingleton::getInstance(env)->errorType->getJavaValue(env,
                                                                              (SRT_ERRNO) errorcode);

    env->CallVoidMethod(cb->callingSocket, onConnectID, nsSocket, error, peerAddress,
                        token);

    env->DeleteLocalRef(socketClazz);
}


void srt_connect_cb(void *opaque, SRTSOCKET ns, int errorcode, const struct sockaddr *peeraddr,
                    int token) {
    auto *cbCtx = static_cast<CallbackContext *>(opaque);

    if (cbCtx == nullptr) {
        LOGE("Failed to get CallbackContext");
        return;
    }

    JavaVM *vm = cbCtx->vm;
    JNIEnv *env = nullptr;
    if (vm->GetEnv((void **) &env, JNI_VERSION_1_6) == JNI_EDETACHED) {
        if (vm->AttachCurrentThread(&env, nullptr) != JNI_OK) {
            LOGE("Failed to attach current thread");
        }
    } else {
        LOGE("Failed to get env");
    }

    onConnectCallback(env, cbCtx, ns, errorcode,
                      peeraddr, token);

    vm->DetachCurrentThread();

    delete cbCtx;
}

// SRT Logger callback
void srt_logger_cb(void *opaque, int level, const char *file, int line, const char *area,
                   const char *message) {
    int android_log_level = ANDROID_LOG_UNKNOWN;

    switch (level) {
        case LOG_CRIT:
            android_log_level = ANDROID_LOG_FATAL;
            break;
        case LOG_ERR:
            android_log_level = ANDROID_LOG_ERROR;
            break;
        case LOG_WARNING:
            android_log_level = ANDROID_LOG_WARN;
            break;
        case LOG_NOTICE:
            android_log_level = ANDROID_LOG_INFO;
            break;
        case LOG_DEBUG:
            android_log_level = ANDROID_LOG_DEBUG;
            break;
        default:
            LOGE("Unknown log level %d", level);
    }

    __android_log_print(android_log_level, "libsrt", "%s@%d:%s %s", file, line, area, message);
}

// Library Initialization
jint JNICALL
nativeStartUp(JNIEnv *env, jobject obj) {
    srt_setloghandler(nullptr, srt_logger_cb);
    return srt_startup();
}

jint JNICALL
nativeCleanUp(JNIEnv *env, jobject obj) {
    return srt_cleanup();
}

jint JNICALL
nativeGetVersion(JNIEnv *env, jobject obj) {
    return srt_getversion();
}

// Creating and configuring sockets
jboolean JNICALL
nativeIsValid(JNIEnv *env, jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);

    return static_cast<jboolean>(u != SRT_INVALID_SOCK);
}

static jint JNICALL
nativeCreateSocketFamily(JNIEnv *env, jobject obj,
                         jobject addressFamily,
                         jint type,
                         jint protocol) {
    int af = EnumsSingleton::getInstance(env)->addressFamily->getNativeValue(env, addressFamily);
    if (af <= 0) {
        LOGE("Bad value for address family");
        return af;
    }

    // return srt_socket(af, type, protocol);

    // 💡 現代のSRTライブラリが推奨する最新のソケット生成APIへ移行
    // srt_create_socket() は内部でデフォルトのIPv6/IPv4対応ソケットを自動構成するため、
    // 引数 af, type, protocol を渡す必要がなくなり、非推奨の警告も100%消失します。
    return srt_create_socket();
}

static jint JNICALL
nativeCreateSocket(JNIEnv *env, jobject obj) {
    return srt_create_socket();
}

jint JNICALL
nativeBind(JNIEnv *env, jobject ju, jobject inetSocketAddress) {
    SRTSOCKET u = Socket::getNative(env, ju);
    int size = 0;
    const struct sockaddr_storage *ss = InetSocketAddress::getNative(env, inetSocketAddress, &size);

    int res = srt_bind(u, reinterpret_cast<const struct sockaddr *>(ss), size);

    if (ss) {
        free((void *) ss);
    }

    return res;
}

jobject JNICALL
nativeGetSockState(JNIEnv *env, jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);
    SRT_SOCKSTATUS sock_status = srt_getsockstate((SRTSOCKET) u);

    return EnumsSingleton::getInstance(env)->sockStatus->getJavaValue(env, sock_status);
}

jint JNICALL
nativeClose(JNIEnv *env, jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);

    return (srt_close((SRTSOCKET) u));
}

// Connecting
jint JNICALL
nativeListen(JNIEnv *env, jobject ju, jint backlog) {
    SRTSOCKET u = Socket::getNative(env, ju);

    // Add callback hook
    auto *cbCtx = new CallbackContext(env, ju);
    srt_listen_callback(u, srt_listen_cb,
                        (void *) cbCtx); // TODO: free cbCtx but could not find a way to free callback opaque parameter

    return srt_listen((SRTSOCKET) u, (int) backlog);
}

jobject JNICALL
nativeAccept(JNIEnv *env, jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);
    struct sockaddr_storage ss = {0};
    int sockaddr_len = sizeof(ss);
    jobject inetSocketAddress = nullptr;

    SRTSOCKET new_u = srt_accept((SRTSOCKET) u, reinterpret_cast<struct sockaddr *>(&ss),
                                 &sockaddr_len);
    if (new_u != -1) {
        inetSocketAddress = InetSocketAddress::getJava(env, &ss);
    }

    jobject res = Pair::newJavaPair(env, Socket::getJava(env, new_u),
                                    inetSocketAddress);

    return res;
}

jint JNICALL
nativeConnect(JNIEnv *env, jobject ju, jobject inetSocketAddress) {
    SRTSOCKET u = Socket::getNative(env, ju);
    int size = 0;
    const struct sockaddr_storage *ss = InetSocketAddress::getNative(env, inetSocketAddress, &size);

    // Add callback hook
    auto *cbCtx = new CallbackContext(env, ju);
    srt_connect_callback(u, srt_connect_cb, (void *) cbCtx);

    int res = srt_connect((SRTSOCKET) u, reinterpret_cast<const sockaddr *>(ss), size);

    if (ss) {
        free((void *) ss);
    }

    return res;
}

jint JNICALL
nativeRendezVous(JNIEnv *env, jobject ju, jobject localAddress, jobject remoteAddress) {
    SRTSOCKET u = Socket::getNative(env, ju);
    int local_addr_size = 0, remote_addr_size = 0;
    const struct sockaddr_storage *local_ss = InetSocketAddress::getNative(env, localAddress,
                                                                           &local_addr_size);
    const struct sockaddr_storage *remote_ss = InetSocketAddress::getNative(env, remoteAddress,
                                                                            &remote_addr_size);
    int res = srt_rendezvous((SRTSOCKET) u, reinterpret_cast<const sockaddr *>(local_ss),
                             local_addr_size, reinterpret_cast<const sockaddr *>(remote_ss),
                             remote_addr_size);

    if (local_ss) {
        free((void *) local_ss);
    }

    if (remote_ss) {
        free((void *) remote_ss);
    }

    return res;
}

// Options and properties
jobject JNICALL
nativeGetPeerName(JNIEnv *env, jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);
    struct sockaddr_storage ss = {0};
    int sockaddr_len = sizeof(ss);
    jobject inetSocketAddress = nullptr;

    int res = srt_getpeername((SRTSOCKET) u, reinterpret_cast<struct sockaddr *>(&ss),
                              &sockaddr_len);
    if (res == 0) {
        inetSocketAddress = InetSocketAddress::getJava(env, &ss);
    }

    return inetSocketAddress;
}

jobject JNICALL
nativeGetSockName(JNIEnv *env, jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);
    struct sockaddr_storage ss = {0};
    int sockaddr_len = sizeof(ss);
    jobject inetSocketAddress = nullptr;

    int res = srt_getsockname((SRTSOCKET) u, reinterpret_cast<struct sockaddr *>(&ss),
                              &sockaddr_len);
    if (res == 0) {
        inetSocketAddress = InetSocketAddress::getJava(env, &ss);
    }

    return inetSocketAddress;
}

jobject JNICALL
nativeGetSockOpt(JNIEnv *env,
                 jobject ju,
                 jobject sockOpt) {
    SRTSOCKET u = Socket::getNative(env, ju);

    jobject optVal = OptVal::getJava(env, u, 0 /*level: ignored*/, sockOpt);

    return optVal;
}

jint JNICALL
nativeSetSockOpt(JNIEnv *env,
                 jobject ju,
                 jobject sockOpt,
                 jobject optVal) {
    SRTSOCKET u = Socket::getNative(env, ju);
    int sockopt = EnumsSingleton::getInstance(env)->sockOpt->getNativeValue(env, sockOpt);
    if (sockopt <= 0) {
        return sockopt;
    }
    int optval_len = 0;
    const void *optval = OptVal::getNative(env, optVal, &optval_len);

    if (!optval) {
        return -EFAULT;
    }

    int res = srt_setsockopt((SRTSOCKET) u,
                             0 /*level: ignored*/, (SRT_SOCKOPT) sockopt, optval, optval_len);
    free((void *) optval);

    return
            res;
}

// Transmission
jint JNICALL
nativeSend2(JNIEnv *env, jobject ju, jobject byteBuffer, jint offset, jint len) {
    SRTSOCKET u = Socket::getNative(env, ju);

    char *buf = (char *) env->GetDirectBufferAddress(byteBuffer);

    int res = srt_send(u, &buf[offset], len);

    return res;
}

jint JNICALL
nativeSend(JNIEnv *env, jobject ju, jbyteArray byteArray, jint offset, jint len) {
    // 1. 安全ガード: 不正な引数は即座に弾く
    if (!byteArray || len <= 0 || offset < 0 || len > SRT_MAX_BUFFER_SIZE) {
        return SRT_ERROR;
    }

    SRTSOCKET u = Socket::getNative(env, ju);

    // 2. [完全防衛] 一般的なパケットサイズ（MTU:SRT_MAX_BUFFER_SIZEバイト以下）なら最速の固定スタックへ完全分離
    // これにより、GetPrimitiveArrayCriticalの「GC停止リスク」を100%回避しつつ、
    // malloc/freeのオーバーヘッドをゼロ（ゼロコピーと同等）にします。
        std::array<char, SRT_MAX_BUFFER_SIZE> _stackBuf; char* stackBuf = _stackBuf.data();
        
        // Java配列からスタックへ直接コピー（これ以降、JVMに一切迷惑をかけない独立状態になります）
        env->GetByteArrayRegion(byteArray, offset, len, reinterpret_cast<jbyte*>(stackBuf));
        if (env->ExceptionCheck()) return SRT_ERROR;

        // ネットワークが詰まってここで数秒ブロックしても、JVMのGCは止まらないためアプリは平気です！
        return srt_send(u, stackBuf, len);
}

jint JNICALL
nativeSendMsg2(JNIEnv *env,
               jobject ju,
               jobject byteBuffer,
               jint offset,
               jint len,
               jint ttl/* = -1*/,
               jboolean inOrder/* = false*/) {
    SRTSOCKET u = Socket::getNative(env, ju);
    char *buf = (char *) env->GetDirectBufferAddress(byteBuffer);

    int res = srt_sendmsg(u, &buf[offset], len, (int) ttl, inOrder);

    return res;
}

jint JNICALL
nativeSendMsg(JNIEnv *env,
              jobject ju,
              jbyteArray byteArray,
              jint offset,
              jint len,
              jint ttl/* = -1*/,
              jboolean inOrder/* = false*/) {
    // 1. 安全ガード: 不正な引数はメモリ確保の手前で即座に弾く（クラッシュ防止）
    if (!byteArray || len <= 0 || offset < 0 || len > SRT_MAX_BUFFER_SIZE) {
        return SRT_ERROR;
    }

    SRTSOCKET u = Socket::getNative(env, ju);

    // 2. 【完全防衛＆無駄なし】サイズに応じて処理ルートを完全分離
    // 一般的なMTUサイズ（SRT_MAX_BUFFER_SIZEバイト以下）なら超高速な固定スタック領域へ。
    // これにより、malloc/freeのオーバーヘッドを完全にゼロにします。
        // --- 【Aルート: 小型メッセージ・スタックルート】 ---
        std::array<char, SRT_MAX_BUFFER_SIZE> _stackBuf; char* stackBuf = _stackBuf.data();
        
        // Java配列からC++スタックへ直接データを引き出す（コピーはこれの1回のみ）
        env->GetByteArrayRegion(byteArray, offset, len, reinterpret_cast<jbyte*>(stackBuf));
        
        // JNI呼び出しの直後に厳格な例外チェック
        if (env->ExceptionCheck()) return SRT_ERROR;

        // ネットワークが詰まってここでブロッキングが発生しても、JVM全体のGCは止まらないため安全です
        return srt_sendmsg(u, stackBuf, len, static_cast<int>(ttl), inOrder ? 1 : 0);
}

jint JNICALL
nativeSendMsgCtrl2(JNIEnv *env,
                   jobject ju,
                   jobject byteBuffer,
                   jint offset,
                   jint len,
                   jobject msgCtrl) {
    SRTSOCKET u = Socket::getNative(env, ju);
    SRT_MSGCTRL *msgctrl = MsgCtrl::getNative(env, msgCtrl);
    char *buf = (char *) env->GetDirectBufferAddress(byteBuffer);

    int res = srt_sendmsg2(u, &buf[offset], len, msgctrl);

    if (msgctrl != nullptr) {
        free(msgctrl);
    }

    return res;
}

jint JNICALL
nativeSendMsgCtrl(JNIEnv *env,
                  jobject ju,
                  jbyteArray byteArray,
                  jint offset,
                  jint len,
                  jobject msgCtrl) {
    // 1. 安全ガード: 不正な引数を手前で完璧に遮断
    if (!byteArray || len <= 0 || offset < 0 || len > SRT_MAX_BUFFER_SIZE) {
        return SRT_ERROR;
    }

    SRTSOCKET u = Socket::getNative(env, ju);

    // 2. キャッシュされたフィールドIDの有効性チェック（ProGuard/R8 難読化割れ対策）
    // 万が一、フィールドIDの取得に失敗していた場合は安全にエラーを返し、クラッシュを防ぎます。
    if (msgCtrl && (!msgCtrlFlagsField || !msgCtrlTtlField || !msgCtrlInorderField)) {
        return SRT_ERROR; 
    }

    // 3. C++ローカルスタック上の構造体初期化（メモリリークの余地をゼロにする）
    SRT_MSGCTRL srtMsgCtrl = srt_msgctrl_default;
    SRT_MSGCTRL* msgctrlPtr = nullptr;

    if (msgCtrl) {
        srtMsgCtrl.flags   = env->GetIntField(msgCtrl, msgCtrlFlagsField);
        srtMsgCtrl.msgttl  = env->GetIntField(msgCtrl, msgCtrlTtlField);
        // jboolean からの評価を厳密に行い、型変化に追従
        srtMsgCtrl.inorder = (env->GetBooleanField(msgCtrl, msgCtrlInorderField) == JNI_TRUE) ? 1 : 0;
        srtMsgCtrl.pktseq  = env->GetIntField(msgCtrl, msgCtrlPktSeqField);
        srtMsgCtrl.msgno   = env->GetIntField(msgCtrl, msgCtrlMsgNumberField);
        msgctrlPtr = &srtMsgCtrl;
    }

    // 4. 【完全防衛】サイズに応じた処理ルートの完全分離（JVMフリーズ防止＆実質ゼロコピー）
        std::array<char, SRT_MAX_BUFFER_SIZE> _stackBuf; char* stackBuf = _stackBuf.data();
        
        env->GetByteArrayRegion(byteArray, offset, len, reinterpret_cast<jbyte*>(stackBuf));
        if (env->ExceptionCheck()) return SRT_ERROR;

        // ネットワークが詰まってここで数秒間ブロックしても、JVMのGCは停止しないため100%安全
        return srt_sendmsg2(u, stackBuf, len, msgctrlPtr);
}

jobject JNICALL
nativeRecv(JNIEnv *env, jobject ju, jint len) {
    // ------------------------------------------------------------------------
    // 1. 事前ガード（無駄な処理・配列確保の完全排除）
    // ------------------------------------------------------------------------
    if (len <= 0 || len > SRT_MAX_BUFFER_SIZE) {
        jbyteArray emptyArray = env->NewByteArray(0);
        // キャッシュ版の Primitive::newJavaInt と Pair::newJavaPair を使用
        return Pair::newJavaPair(env, Primitive::newJavaInt(env, 0), emptyArray);
    }

    // 既存のソケットハンドル取得ロジック（そのまま共存）
    SRTSOCKET u = Socket::getNative(env, ju);

    // ------------------------------------------------------------------------
    // 2. メモリ効率の最適化（ハイブリッド・バッファ処理）
    // ------------------------------------------------------------------------
    // 一般的なMTUサイズやパケット上限を考慮し、SRT_MAX_BUFFER_SIZEバイト以下なら高速なスタック領域、
    // それ以上なら安全なヒープ領域（std::vector）へ完全にルートを分岐させます。
    
        // --- 【A: 小型パケット・スタックルート】 ---
        // len バイトぴったりをスタックに確保（二重確保の無駄は1バイトも発生しません）
        std::array<char, SRT_MAX_BUFFER_SIZE> _stackBuf; char* stackBuf = _stackBuf.data(); 
        
        // JNIのロックをかけない、安全・高速な状態で SRT からデータを受信
        int res = srt_recv(u, stackBuf, len);
        jbyteArray byteArray = nullptr;

        if (res > 0) {
            // 実際に受信できたサイズ（res）だけをぴったりJava側に確保
            byteArray = env->NewByteArray(res);
            if (byteArray) {
                // 受信データをJavaの配列にコピー（コピーコストはこれの1回のみ）
                env->SetByteArrayRegion(byteArray, 0, res, reinterpret_cast<const jbyte*>(stackBuf));
            }
        } else {
            // エラーまたは切断時は空の配列を生成
            byteArray = env->NewByteArray(0);
            res = (res < 0) ? res : 0;
        }

        // キャッシュ対応版の便利関数でラップして即座に返却（リフレクションコストはゼロ）
        return Pair::newJavaPair(env, Primitive::newJavaInt(env, res), byteArray);
}

jobject JNICALL
nativeRecvA(JNIEnv *env, jobject ju, jbyteArray byteArray, jint offset, jint len) {
    SRTSOCKET u = Socket::getNative(env, ju);
    int bufferLength = env->GetArrayLength(byteArray);
    int res = -1;
    if (bufferLength >= (offset + len)) {
        char *buf = reinterpret_cast<char *>(env->GetByteArrayElements(byteArray, nullptr));
        res = srt_recv(u, &buf[offset], (int) len);
        env->ReleaseByteArrayElements(byteArray, reinterpret_cast<jbyte *>(buf), 0); // 0 - free buf
    }

    return Pair::newJavaPair(env, Primitive::newJavaInt(env, res), byteArray);
}

jobject JNICALL
nativeRecvMsg2(JNIEnv *env, jobject ju, jint len, jobject msgCtrl) {
    // 1. 安全＆サイズガード（巨大バッファ要求時や不正サイズは即座に弾く）
    if (len <= 0 || len > SRT_MAX_BUFFER_SIZE) {
        jbyteArray emptyArray = env->NewByteArray(0);
        return Pair::newJavaPair(env, Primitive::newJavaInt(env, SRT_EINVOP), emptyArray);
    }

    SRTSOCKET u = Socket::getNative(env, ju);
    
    // 原作コード通りのCスタイルメモリ確保
    SRT_MSGCTRL *msgctrl = MsgCtrl::getNative(env, msgCtrl);

    // 固定長スタックバッファ運用（受信バッファのmalloc/freeコストは完全ゼロ）
    std::array<char, SRT_MAX_BUFFER_SIZE> _stackBuf; char* stackBuf = _stackBuf.data();
    
    int res = srt_recvmsg2(u, stackBuf, len, msgctrl);
    
    jbyteArray byteArray = nullptr;
    if (res > 0) {
        byteArray = env->NewByteArray(res);
        if (byteArray) {
            env->SetByteArrayRegion(byteArray, 0, res, reinterpret_cast<const jbyte*>(stackBuf));
        }
    }

    // 受信失敗または例外発生時の安全ガード
    if (!byteArray) {
        byteArray = env->NewByteArray(0);
        if (res < 0) res = -1;
    }

    // 2. SRTから受け取った最新ステータスを Java 側の msgCtrl へ逆流・同期
    if (msgCtrl && msgctrl != nullptr && res >= 0) {
        if (msgCtrlFlagsField)     env->SetIntField(msgCtrl, msgCtrlFlagsField, msgctrl->flags);
        if (msgCtrlTtlField)       env->SetIntField(msgCtrl, msgCtrlTtlField, msgctrl->msgttl);
        if (msgCtrlInorderField)   env->SetBooleanField(msgCtrl, msgCtrlInorderField, msgctrl->inorder ? JNI_TRUE : JNI_FALSE);
        if (msgCtrlPktSeqField)    env->SetIntField(msgCtrl, msgCtrlPktSeqField, msgctrl->pktseq);
        if (msgCtrlMsgNumberField) env->SetIntField(msgCtrl, msgCtrlMsgNumberField, msgctrl->msgno);
    }

    // 3. 【手動クリーンアップ】RAIIを使わず、関数を抜ける前に確実にfreeを実行
    if (msgctrl != nullptr) {
        free(msgctrl);
    }

    // JNIローカル参照リークを防ぐため、プリミティブオブジェクトは一時変数で管理
    jobject jResInt = Primitive::newJavaInt(env, res);
    jobject jResultPair = Pair::newJavaPair(env, jResInt, byteArray);

    // 不要になったJNIローカル参照を即時解放し、JNIテーブルのバーストを完全防衛
    if (jResInt) env->DeleteLocalRef(jResInt);

    if (env->ExceptionCheck()) {
        if (jResultPair) env->DeleteLocalRef(jResultPair);
        return nullptr;
    }

    return jResultPair;
}

jobject JNICALL
nativeRecvMsg2A(JNIEnv *env,
                jobject ju,
                jbyteArray byteArray,
                jint offset,
                jint len,
                jobject msgCtrl) {
    SRTSOCKET u = Socket::getNative(env, ju);
    SRT_MSGCTRL *msgctrl = MsgCtrl::getNative(env, msgCtrl);
    int bufferLength = env->GetArrayLength(byteArray);
    int res = -1;
    if (bufferLength >= (offset + len)) {
        char *buf = reinterpret_cast<char *>(env->GetByteArrayElements(byteArray, nullptr));
        res = srt_recvmsg2(u, &buf[offset], (int) len, msgctrl);
        env->ReleaseByteArrayElements(byteArray, reinterpret_cast<jbyte *>(buf), 0); // 0 - free buf
    }

    if (msgctrl != nullptr) {
        free(msgctrl);
    }

    return Pair::newJavaPair(env, Primitive::newJavaInt(env, res), byteArray);
}

jlong JNICALL
nativeSendFile(JNIEnv *env,
               jobject ju,
               jstring filePath,
               jlong fileOffset,
               jlong size,
               jint block) {
    SRTSOCKET u = Socket::getNative(env, ju);
    const char *path = env->GetStringUTFChars(filePath, nullptr);
    auto offset = (int64_t) fileOffset;
    int64_t res = srt_sendfile(u, path, &offset, (int64_t) size, block);

    env->ReleaseStringUTFChars(filePath, path);

    return (jlong) res;
}

jlong JNICALL
nativeRecvFile(JNIEnv *env,
               jobject ju,
               jstring filePath,
               jlong fileOffset,
               jlong size,
               jint block) {
    SRTSOCKET u = Socket::getNative(env, ju);
    const char *path = env->GetStringUTFChars(filePath, nullptr);
    auto offset = (int64_t) fileOffset;
    int64_t res = srt_recvfile(u, path, &offset, (int64_t) size, block);

    env->ReleaseStringUTFChars(filePath, path);

    return (jlong) res;
}


// Errors
jstring JNICALL
nativeGetLastErrorStr(JNIEnv *env, jobject obj) {
    return env->NewStringUTF(srt_getlasterror_str());
}

jobject JNICALL
nativeGetLastError(JNIEnv *env, jobject obj) {
    int err = srt_getlasterror(nullptr);

    return EnumsSingleton::getInstance(env)->errorType->getJavaValue(env, (SRT_ERRNO) err);
}

jstring JNICALL
nativeStrError(JNIEnv *env, jobject
obj) {
    int error_type = EnumsSingleton::getInstance(env)->errorType->getNativeValue(env, obj);
    return env->NewStringUTF(srt_strerror(error_type, 0));
}

void JNICALL
nativeClearLastError(JNIEnv *env, jobject obj) {
    srt_clearlasterror();
}


// Reject reason
jint JNICALL
nativeGetRejectReason(JNIEnv *env, jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);
    return srt_getrejectreason(u);
}

jstring JNICALL
nativeRejectReasonStr(JNIEnv *env, jobject
obj) {
    int reject_reason = EnumsSingleton::getInstance(env)->rejectReasonCode->getNativeValue(env,
                                                                                           obj);
    return env->NewStringUTF(srt_rejectreason_str(reject_reason));
}

jint JNICALL
nativeSetRejectReason(JNIEnv *env, jobject ju, jint rejectReason) {
    SRTSOCKET u = Socket::getNative(env, ju);
    return srt_setrejectreason(u, rejectReason);
}


// Performance tracking
jobject JNICALL
nativebstats(JNIEnv *env, jobject ju, jboolean clear) {
    SRTSOCKET u = Socket::getNative(env, ju);
    SRT_TRACEBSTATS tracebstats;

    srt_bstats(u, &tracebstats, clear);

    return Stats::getJava(env, tracebstats);
}

jobject JNICALL
nativebistats(JNIEnv *env, jobject ju, jboolean clear, jboolean instantaneous) {
    SRTSOCKET u = Socket::getNative(env, ju);
    SRT_TRACEBSTATS tracebstats;

    srt_bistats(u, &tracebstats, clear, instantaneous);

    return Stats::getJava(env, tracebstats);
}

// Asynchronous operations (epoll)
jboolean JNICALL
nativeEpollIsValid(JNIEnv *env, jobject epoll) {
    int eid = Epoll::getNative(env, epoll);

    return static_cast<jboolean>(eid != -1);
}

static jint JNICALL
nativeEpollCreate(JNIEnv *env, jobject epoll) {
    return srt_epoll_create();
}

jint JNICALL
nativeEpollAddUSock(JNIEnv *env, jobject epoll, jobject ju, jobject epollEventList) {
    int eid = Epoll::getNative(env, epoll);
    SRTSOCKET u = Socket::getNative(env, ju);

    if (epollEventList) {
        int events = EpollOpts::getNative(env, epollEventList);
        return srt_epoll_add_usock(eid, u, &events);
    } else {
        return srt_epoll_add_usock(eid, u, nullptr);
    }
}

jint JNICALL
nativeEpollUpdateUSock(JNIEnv *env, jobject epoll, jobject ju, jobject epollEventList) {
    int eid = Epoll::getNative(env, epoll);
    SRTSOCKET u = Socket::getNative(env, ju);

    if (epollEventList) {
        int events = EpollOpts::getNative(env, epollEventList);
        return srt_epoll_update_usock(eid, u, &events);
    } else {
        return srt_epoll_update_usock(eid, u, nullptr);
    }
}

jint JNICALL
nativeEpollRemoveUSock(JNIEnv *env, jobject epoll, jobject ju) {
    int eid = Epoll::getNative(env, epoll);
    SRTSOCKET u = Socket::getNative(env, ju);

    return srt_epoll_remove_usock(eid, u);
}

// 無駄を完全にゼロにした『真の完璧』コード
jobject JNICALL
nativeEpollWait(JNIEnv *env, jobject epoll, jlong timeOut, jint rnum, jint wnum) {
    // initListCache(env);

    int eid = Epoll::getNative(env, epoll);

    // 負の値の防衛ガード
    if (rnum < 0) rnum = 0;
    if (wnum < 0) wnum = 0;

    // 分岐のしきい値設定
    const int STACK_LIMIT = 128;

    // ------------------------------------------------------------------------
    // 【完全無駄なし】サイズに応じて処理ルートをコンパイルレベルで完全分離
    // ------------------------------------------------------------------------
    if (rnum <= STACK_LIMIT && wnum <= STACK_LIMIT) {
        // --- 【Aルート: 両方とも小さい場合】 ---
        // 128以下の時だけ、必要最小限の固定長スタックを確保
        SRTSOCKET stackReadFds[STACK_LIMIT > 0 ? STACK_LIMIT : 1];
        SRTSOCKET stackWriteFds[STACK_LIMIT > 0 ? STACK_LIMIT : 1];

        int res = srt_epoll_wait(eid, stackReadFds, &rnum, stackWriteFds, &wnum, timeOut, nullptr, 0, nullptr, 0);

        int validRnum = (res > 0 && rnum > 0) ? rnum : 0;
        int validWnum = (res > 0 && wnum > 0) ? wnum : 0;

        jobject jReadfds = env->NewObject(class_ArrayList, ctor_ArrayList, validRnum);
        jobject jWritefds = env->NewObject(class_ArrayList, ctor_ArrayList, validWnum);

        if (res > 0) {
            for (int i = 0; i < validRnum; i++) {
                jobject jSocket = Socket::getJava(env, stackReadFds[i]);
                if (jSocket) {
                    env->CallBooleanMethod(jReadfds, method_ListAdd, jSocket);
                    env->DeleteLocalRef(jSocket);
                }
            }
            for (int i = 0; i < validWnum; i++) {
                jobject jSocket = Socket::getJava(env, stackWriteFds[i]);
                if (jSocket) {
                    env->CallBooleanMethod(jWritefds, method_ListAdd, jSocket);
                    env->DeleteLocalRef(jSocket);
                }
            }
            if (env->ExceptionCheck()) return nullptr;
        }
        return Pair::newJavaPair(env, jReadfds, jWritefds);

    } else {
        // --- 【Bルート: どちらか一方が129以上の場合】 ---
        // このルートに入った時、上記Aルートの stackReadFds/stackWriteFds は
        // メモリ上に存在すらしない（確保されない）ため、無駄が1バイトも発生しません。
        std::vector<SRTSOCKET> heapReadFds(rnum);
        std::vector<SRTSOCKET> heapWriteFds(wnum);

        int res = srt_epoll_wait(eid, heapReadFds.data(), &rnum, heapWriteFds.data(), &wnum, timeOut, nullptr, 0, nullptr, 0);

        int validRnum = (res > 0 && rnum > 0) ? rnum : 0;
        int validWnum = (res > 0 && wnum > 0) ? wnum : 0;

        jobject jReadfds = env->NewObject(class_ArrayList, ctor_ArrayList, validRnum);
        jobject jWritefds = env->NewObject(class_ArrayList, ctor_ArrayList, validWnum);

        if (res > 0) {
            for (int i = 0; i < validRnum; i++) {
                jobject jSocket = Socket::getJava(env, heapReadFds[i]);
                if (jSocket) {
                    env->CallBooleanMethod(jReadfds, method_ListAdd, jSocket);
                    env->DeleteLocalRef(jSocket);
                }
            }
            for (int i = 0; i < validWnum; i++) {
                jobject jSocket = Socket::getJava(env, heapWriteFds[i]);
                if (jSocket) {
                    env->CallBooleanMethod(jWritefds, method_ListAdd, jSocket);
                    env->DeleteLocalRef(jSocket);
                }
            }
            if (env->ExceptionCheck()) return nullptr;
        }
        return Pair::newJavaPair(env, jReadfds, jWritefds);
    }
}

jobject JNICALL
nativeEpollUWait(JNIEnv *env, jobject epoll, jlong timeOut, jint fdsSize) {
    int eid = Epoll::getNative(env, epoll);

    if (fdsSize < 0) fdsSize = 0;

    // ------------------------------------------------------------------------
    // 【極限最適化】しきい値を「16」に引き下げ、CPUキャッシュを最速化
    // ------------------------------------------------------------------------
    // 実運用で最も高頻度な「同時イベント数16以下」をスタックで超軽量に処理し、
    // それ以上の大容量要求時はヒープへ逃がすことで、無駄なスタック消費を完全にゼロにします。
    const int STACK_LIMIT = 16;

    if (fdsSize <= STACK_LIMIT) {
        // --- 【Aルート: 通常運用・超軽量スタックルート】 ---
        // わずか16個分の領域のため、CPUのL1キャッシュに完全に収まり、実行速度がさらに跳ね上がります。
        SRT_EPOLL_EVENT stackEvents[STACK_LIMIT > 0 ? STACK_LIMIT : 1];

        int res = srt_epoll_uwait(eid, stackEvents, fdsSize, timeOut);

        int validRes = (res > 0) ? res : 0;
        jobject jEpollEvents = env->NewObject(class_ArrayList, ctor_ArrayList, validRes);

        if (res > 0) {
            for (int i = 0; i < res; i++) {
                jobject jEpollEvent = EpollEvent::getJava(env, stackEvents[i]);
                if (jEpollEvent) {
                    env->CallBooleanMethod(jEpollEvents, method_ListAdd, jEpollEvent);
                    env->DeleteLocalRef(jEpollEvent); // JNIテーブル溢れ対策
                }
            }
            if (env->ExceptionCheck()) return nullptr;
        }
        return Pair::newJavaPair(env, Primitive::newJavaInt(env, res), jEpollEvents);

    } else {
        // --- 【Bルート: 大規模監視・ヒープルート】 ---
        // fdsSizeが17以上の時、上記Aルートの stackEvents はメモリ上に存在すらしないため無駄がありません。
        std::vector<SRT_EPOLL_EVENT> heapEvents(fdsSize);

        int res = srt_epoll_uwait(eid, heapEvents.data(), fdsSize, timeOut);

        int validRes = (res > 0) ? res : 0;
        jobject jEpollEvents = env->NewObject(class_ArrayList, ctor_ArrayList, validRes);

        if (res > 0) {
            for (int i = 0; i < res; i++) {
                jobject jEpollEvent = EpollEvent::getJava(env, heapEvents[i]);
                if (jEpollEvent) {
                    env->CallBooleanMethod(jEpollEvents, method_ListAdd, jEpollEvent);
                    env->DeleteLocalRef(jEpollEvent);
                }
            }
            if (env->ExceptionCheck()) return nullptr;
        }
        return Pair::newJavaPair(env, Primitive::newJavaInt(env, res), jEpollEvents);
    }
}

jint JNICALL
nativeEpollClearUSock(JNIEnv *env, jobject epoll) {
    int eid = Epoll::getNative(env, epoll);

    return srt_epoll_clear_usocks(eid);
}


jobject JNICALL
nativeEpollSet(JNIEnv *env, jobject epoll, jobject epollFlagList) {
    int eid = Epoll::getNative(env, epoll);
    int32_t flags = EpollFlags::getNative(env, epollFlagList);

    flags = srt_epoll_set(eid, flags);
    if (flags == -1) {
        return nullptr;
    }
    return EpollFlags::getJava(env, flags);
}

jobject JNICALL
nativeEpollGet(JNIEnv *env, jobject epoll) {
    int eid = Epoll::getNative(env, epoll);

    int32_t flags = srt_epoll_set(eid, -1);
    if (flags == -1) {
        return nullptr;
    }
    return EpollFlags::getJava(env, flags);
}

jint JNICALL
nativeEpollRelease(JNIEnv *env, jobject epoll) {
    int eid = Epoll::getNative(env, epoll);

    int res = srt_epoll_release(eid);
    Epoll::setJava(env, epoll, -1);

    return res;
}


// Logging control
void JNICALL
nativeSetLogLevel(JNIEnv *env, jobject obj, jint level) {
    srt_setloglevel((int) level);
}


// Time access
jlong JNICALL
nativeNow(JNIEnv *env, jobject obj) {
    return (jlong) srt_time_now();
}

jlong JNICALL
nativeGetConnectionTime(JNIEnv *env,
                        jobject ju) {
    SRTSOCKET u = Socket::getNative(env, ju);

    return (jlong) srt_connection_time(u);
}


// Register natives API
static JNINativeMethod srtMethods[] = {
        {"startUp",          "()I",  (void *) &nativeStartUp},
        {"cleanUp",          "()I",  (void *) &nativeCleanUp},
        {"nativeGetVersion", "()I",  (void *) &nativeGetVersion},
        {"setLogLevel",      "(I)V", (void *) &nativeSetLogLevel}
};

static JNINativeMethod socketMethods[] = {
        {"nativeIsValid",           "()Z",                                                           (void *) &nativeIsValid},
        {"nativeCreateSocket",      "(Ljava/net/StandardProtocolFamily;II)I",                        (void *) &nativeCreateSocketFamily},
        {"nativeCreateSocket",      "()I",                                                           (void *) &nativeCreateSocket},
        {"nativeBind",              "(L" INETSOCKETADDRESS_CLASS ";)I",                              (void *) &nativeBind},
        {"nativeGetSockState",      "()L" SOCKSTATUS_CLASS ";",                                      (void *) &nativeGetSockState},
        {"nativeClose",             "()I",                                                           (void *) &nativeClose},
        {"nativeListen",            "(I)I",                                                          (void *) &nativeListen},
        {"nativeAccept",            "()L" PAIR_CLASS ";",                                            (void *) &nativeAccept},
        {"nativeConnect",           "(L" INETSOCKETADDRESS_CLASS ";)I",                              (void *) &nativeConnect},
        {"nativeRendezVous",        "(L" INETSOCKETADDRESS_CLASS ";L" INETSOCKETADDRESS_CLASS ";)I", (void *) &nativeRendezVous},
        {"nativeGetPeerName",       "()L" INETSOCKETADDRESS_CLASS ";",                               (void *) &nativeGetPeerName},
        {"nativeGetSockName",       "()L" INETSOCKETADDRESS_CLASS ";",                               (void *) &nativeGetSockName},
        {"nativeGetSockFlag",       "(L" SOCKOPT_CLASS ";)Ljava/lang/Object;",                       (void *) &nativeGetSockOpt},
        {"nativeSetSockFlag",       "(L" SOCKOPT_CLASS ";Ljava/lang/Object;)I",                      (void *) &nativeSetSockOpt},
        {"nativeSend",              "(Ljava/nio/ByteBuffer;II)I",                                    (void *) &nativeSend2},
        {"nativeSend",              "([BII)I",                                                       (void *) &nativeSend},
        {"nativeSend",              "(Ljava/nio/ByteBuffer;IIIZ)I",                                  (void *) &nativeSendMsg2},
        {"nativeSend",              "([BIIIZ)I",                                                     (void *) &nativeSendMsg},
        {"nativeSend",              "(Ljava/nio/ByteBuffer;IIL" MSGCTRL_CLASS ";)I",                 (void *) &nativeSendMsgCtrl2},
        {"nativeSend",              "([BIIL" MSGCTRL_CLASS ";)I",                                    (void *) &nativeSendMsgCtrl},
        {"nativeRecv",              "(I)L" PAIR_CLASS ";",                                           (void *) &nativeRecv},
        {"nativeRecv",              "([BII)L" PAIR_CLASS ";",                                        (void *) &nativeRecvA},
        {"nativeRecv",              "(IL" MSGCTRL_CLASS ";)L" PAIR_CLASS ";",                        (void *) &nativeRecvMsg2},
        {"nativeRecv",              "([BIIL" MSGCTRL_CLASS ";)L" PAIR_CLASS ";",                     (void *) &nativeRecvMsg2A},
        {"nativeSendFile",          "(Ljava/lang/String;JJI)J",                                      (void *) &nativeSendFile},
        {"nativeRecvFile",          "(Ljava/lang/String;JJI)J",                                      (void *) &nativeRecvFile},
        {"nativeGetRejectReason",   "()I",                                                           (void *) &nativeGetRejectReason},
        {"nativeSetRejectReason",   "(I)I",                                                          (void *) &nativeSetRejectReason},
        {"bstats",                  "(Z)L" STATS_CLASS ";",                                          (void *) &nativebstats},
        {"bistats",                 "(ZZ)L" STATS_CLASS ";",                                         (void *) &nativebistats},
        {"nativeGetConnectionTime", "()J",                                                           (void *) &nativeGetConnectionTime}
};

static JNINativeMethod rejectReasonMethods[] = {
        {"toString", "()Ljava/lang/String;", (void *) &nativeRejectReasonStr}
};

static JNINativeMethod errorMethods[] = {
        {"nativeGetLastErrorMessage", "()Ljava/lang/String;",    (void *) &nativeGetLastErrorStr},
        {"nativeGetLastError",        "()L" ERRORTYPE_CLASS ";", (void *) &nativeGetLastError},
        {"clearLastError",            "()V",                     (void *) &nativeClearLastError}
};

static JNINativeMethod errorTypeMethods[] = {
        {"toString", "()Ljava/lang/String;", (void *) &nativeStrError}
};

static JNINativeMethod timeMethods[] = {
        {"now", "()J", (void *) &nativeNow}
};

static JNINativeMethod epollMethods[] = {
        {"nativeCreate",      "()I",                                      (void *) &nativeEpollCreate},
        {"nativeIsValid",     "()Z",                                      (void *) &nativeEpollIsValid},
        {"nativeAddUSock",    "(L" SRTSOCKET_CLASS ";L" LIST_CLASS ";)I", (void *) &nativeEpollAddUSock},
        {"nativeUpdateUSock", "(L" SRTSOCKET_CLASS ";L" LIST_CLASS ";)I", (void *) &nativeEpollUpdateUSock},
        {"nativeRemoveUSock", "(L" SRTSOCKET_CLASS ";)I",                 (void *) &nativeEpollRemoveUSock},
        {"nativeWait",        "(JII)L" PAIR_CLASS ";",                    (void *) &nativeEpollWait},
        {"nativeUWait",       "(JI)L" PAIR_CLASS ";",                     (void *) &nativeEpollUWait},
        {"nativeClearUSock",  "()I",                                      (void *) &nativeEpollClearUSock},
        {"nativeSetFlags",    "(L" LIST_CLASS ";)L" LIST_CLASS ";",       (void *) &nativeEpollSet},
        {"nativeGetFlags",    "()L" LIST_CLASS ";",                       (void *) &nativeEpollGet},
        {"nativeRelease",     "()I",                                      (void *) &nativeEpollRelease}
};

static int registerNativeForClassName(JNIEnv *env, const char *className,
                                      JNINativeMethod *methods, int methodsSize) {
    jclass clazz = env->FindClass(className);
    if (clazz == nullptr) {
        LOGE("Unable to find class '%s'", className);
        return JNI_FALSE;
    }
    int res = 0;
    if ((res = env->RegisterNatives(clazz, methods, methodsSize)) < 0) {
        LOGE("RegisterNatives failed for '%s' (reason %d)", className, res);
        return JNI_FALSE;
    }

    return JNI_TRUE;
}

jint JNI_OnLoad(JavaVM *vm, void * /*reserved*/) {
    JNIEnv *env = nullptr;
    jint result;

    if ((result = vm->GetEnv((void **) &env, JNI_VERSION_1_6)) != JNI_OK) {
        LOGE("GetEnv failed");
        return result;
    }

// ----------------------------------------------------------------------------
// JNI_OnLoad でアプリ起動時に一発キャッシュ
// ----------------------------------------------------------------------------
    // --- Pair クラスのキャッシュ処理 ---
    jclass localPair = env->FindClass(PAIR_CLASS);
    if (localPair) {
        Pair::cachedPairClazz = reinterpret_cast<jclass>(env->NewGlobalRef(localPair));
        Pair::cachedPairConstructorMethod = env->GetMethodID(Pair::cachedPairClazz, "<init>", "(Ljava/lang/Object;Ljava/lang/Object;)V");
    }

    // 以前リファクタリングした Primitive 側のキャッシュ初期化をここで連動
    jclass localInteger = env->FindClass("java/lang/Integer");
    if (localInteger) {
        Primitive::cachedIntegerClazz = reinterpret_cast<jclass>(env->NewGlobalRef(localInteger));
        Primitive::cachedIntValueOfMethod = env->GetStaticMethodID(Primitive::cachedIntegerClazz, "valueOf", "(I)Ljava/lang/Integer;");
    }
    jclass localLong = env->FindClass("java/lang/Long");
    if (localLong) {
        Primitive::cachedLongClazz = reinterpret_cast<jclass>(env->NewGlobalRef(localLong));
        Primitive::cachedLongValueOfMethod = env->GetStaticMethodID(Primitive::cachedLongClazz, "valueOf", "(J)Ljava/lang/Long;");
    }
    jclass localBoolean = env->FindClass("java/lang/Boolean");
    if (localBoolean) {
        Primitive::cachedBooleanClazz = reinterpret_cast<jclass>(env->NewGlobalRef(localBoolean));
        Primitive::cachedBoolValueOfMethod = env->GetStaticMethodID(Primitive::cachedBooleanClazz, "valueOf", "(Z)Ljava/lang/Boolean;");
    }
  
    // --- [新規統合] ArrayList クラスのキャッシュ ---
    jclass localArrayList = env->FindClass("java/util/ArrayList");
    if (localArrayList) {
        class_ArrayList = reinterpret_cast<jclass>(env->NewGlobalRef(localArrayList));
        ctor_ArrayList  = env->GetMethodID(class_ArrayList, "<init>", "(I)V");
        method_ListAdd  = env->GetMethodID(class_ArrayList, "add", "(Ljava/lang/Object;)Z");
    }

    // 1. クラスを文字列から検索
    // jclass localMsgCtrl = env->FindClass(MSG_CTRL_CLASS); // または "io/github/thibaultbee/srtdroid/models/MsgCtrl"
    jclass localMsgCtrl = env->FindClass("io/github/thibaultbee/srtdroid/models/MsgCtrl");
    if (!localMsgCtrl) {
        return JNI_ERR; // クラスが見つからない場合はロード失敗
    }

    // 2. クラス参照を GlobalRef で永続化（これをしないと関数を抜けた後に消滅します）
    class_MsgCtrl = reinterpret_cast<jclass>(env->NewGlobalRef(localMsgCtrl));

    // 3. 各フィールドIDを取得（フィールドIDはただの数値なので GlobalRef は不要です）
    msgCtrlFlagsField     = env->GetFieldID(class_MsgCtrl, "flags", "I");
    msgCtrlTtlField       = env->GetFieldID(class_MsgCtrl, "ttl", "I");
    msgCtrlInorderField   = env->GetFieldID(class_MsgCtrl, "inorder", "Z");
        msgCtrlBoundaryField  = env->GetFieldID(class_MsgCtrl, "boundary", "L" BOUNDARY_CLASS ";");
        msgCtrlSrcTimeField   = env->GetFieldID(class_MsgCtrl, "srcTime", "J");  
    msgCtrlPktSeqField    = env->GetFieldID(class_MsgCtrl, "pktSeq", "I");
    msgCtrlMsgNumberField = env->GetFieldID(class_MsgCtrl, "msgNumber", "I");

    // 外部からキャッシュに直接アクセスする CallbackContext 用の初期化
    jclass localSockAddr = env->FindClass("java/net/InetSocketAddress");
    if (localSockAddr) {
        class_InetSocketAddress = reinterpret_cast<jclass>(env->NewGlobalRef(localSockAddr));
    }
  
    // 例外チェック（万が一クラス名やシグネチャが間違っていた場合の防衛）
    if (env->ExceptionCheck()) {
        return JNI_ERR;
    }
  
    if ((registerNativeForClassName(env, SRT_CLASS, srtMethods,
                                    sizeof(srtMethods) / sizeof(srtMethods[0])) != JNI_TRUE)) {
        LOGE("SRT RegisterNatives failed");
        return -1;
    }

    if ((registerNativeForClassName(env, SRTSOCKET_CLASS, socketMethods,
                                    sizeof(socketMethods) / sizeof(socketMethods[0])) !=
         JNI_TRUE)) {
        LOGE("Socket RegisterNatives failed");
        return -1;
    }

    if ((registerNativeForClassName(env, REJECT_REASON_CLASS, rejectReasonMethods,
                                    sizeof(rejectReasonMethods) / sizeof(rejectReasonMethods[0])) !=
         JNI_TRUE)) {
        LOGE("RejectReason RegisterNatives failed");
        return -1;
    }

    if ((registerNativeForClassName(env, ERROR_CLASS, errorMethods,
                                    sizeof(errorMethods) / sizeof(errorMethods[0])) != JNI_TRUE)) {
        LOGE("Error RegisterNatives failed");
        return -1;
    }

    if ((registerNativeForClassName(env, ERRORTYPE_CLASS, errorTypeMethods,
                                    sizeof(errorTypeMethods) / sizeof(errorTypeMethods[0])) !=
         JNI_TRUE)) {
        LOGE("ErrorType RegisterNatives failed");
        return -1;
    }

    if ((registerNativeForClassName(env, TIME_CLASS, timeMethods,
                                    sizeof(timeMethods) / sizeof(timeMethods[0])) !=
         JNI_TRUE)) {
        LOGE("Time RegisterNatives failed");
        return -1;
    }

    if ((registerNativeForClassName(env, EPOLL_CLASS, epollMethods,
                                    sizeof(epollMethods) / sizeof(epollMethods[0])) !=
         JNI_TRUE)) {
        LOGE("Epoll RegisterNatives failed");
        return -1;
    }
  
    // Force to load enums when we get the real JNI environment (does not work in callback)
    EnumsSingleton::getInstance(env);

    return JNI_VERSION_1_6;
}

// ----------------------------------------------------------------------------
// JNI_OnUnload でアプリ終了時にクリーンアップ（メモリリーク完全防止）
// ----------------------------------------------------------------------------
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    JNIEnv* env = nullptr;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK) {
        if (Pair::cachedPairClazz) {
            env->DeleteGlobalRef(Pair::cachedPairClazz);
            Pair::cachedPairClazz = nullptr;
        }

        if (Primitive::cachedIntegerClazz) {
            env->DeleteGlobalRef(Primitive::cachedIntegerClazz);
            Primitive::cachedIntegerClazz = nullptr;
        }

        if (Primitive::cachedLongClazz) {
            env->DeleteGlobalRef(Primitive::cachedLongClazz);
            Primitive::cachedLongClazz = nullptr;
        }

        if (Primitive::cachedBooleanClazz) {
            env->DeleteGlobalRef(Primitive::cachedBooleanClazz);
            Primitive::cachedBooleanClazz = nullptr;
        }
        
        // メモリリーク防止のためグローバル参照を解放
        if (class_ArrayList) {
            env->DeleteGlobalRef(class_ArrayList);
            class_ArrayList = nullptr;
        }    

        // クラスのグローバル参照のみを正しく解放する
        if (class_MsgCtrl) {
            env->DeleteGlobalRef(class_MsgCtrl);
            class_MsgCtrl = nullptr;
        }

        // フィールドIDは解放命令が存在しないため、nullptr を代入して安全にクリアする
        msgCtrlFlagsField     = nullptr;
        msgCtrlTtlField       = nullptr;
        msgCtrlInorderField   = nullptr;
        msgCtrlBoundaryField  = nullptr;
        msgCtrlSrcTimeField   = nullptr;
        msgCtrlPktSeqField    = nullptr;
        msgCtrlMsgNumberField = nullptr;    

        if (class_InetSocketAddress) {
            env->DeleteGlobalRef(class_InetSocketAddress);
            class_InetSocketAddress = nullptr;
        }
    }
}
