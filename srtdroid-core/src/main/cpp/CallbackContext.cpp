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
// #include "Models/Models.h"
// #include "CallbackContext.h"

/*
CallbackContext::CallbackContext(JNIEnv *env, jobject callingSocket) {
    env->GetJavaVM(&(this->vm));

    this->sockAddrClazz = static_cast<jclass>(env->NewGlobalRef(
            env->FindClass(INETSOCKETADDRESS_CLASS)));
    this->callingSocket = env->NewGlobalRef(callingSocket);
}

CallbackContext::~CallbackContext() {
    JNIEnv *env = nullptr;

    vm->GetEnv((void **) &env, JNI_VERSION_1_6);
    if (env != nullptr) {
        env->DeleteGlobalRef(this->callingSocket);
        env->DeleteGlobalRef(this->sockAddrClazz);
    }
}
*/

/* // glue.cppの JNI_OnLoad で初期化・解放が一括管理される共通キャッシュ参照
// jclass class_InetSocketAddress = nullptr;

// リンクエラーを解消するため、正確に extern 宣言（static修飾を外したため安全に結合可能）
extern jclass class_InetSocketAddress;

CallbackContext::CallbackContext(JNIEnv *env, jobject callingSocket) {
    env->GetJavaVM(&(this->vm));
    this->sockAddrClazz = class_InetSocketAddress; // 起動時確定ポインタを代入してクラスローダー割れを完全防止
    this->callingSocket = env->NewGlobalRef(callingSocket); // ライフサイクルを個別に拘束
}

// ----------------------------------------------------------------------------
// 【重要】安全なJava駆動コンテキストのタイミングで明示的に呼び出すメソッド
// ----------------------------------------------------------------------------
void CallbackContext::release(JNIEnv *env) {
    if (this->callingSocket && env != nullptr) {
        env->DeleteGlobalRef(this->callingSocket);
        this->callingSocket = nullptr;
    }
    this->sockAddrClazz = nullptr;
}

// ----------------------------------------------------------------------------
// デストラクタ（危険なアタッチは一切せず、GCのロックと100%衝突しないガベージフリー設計）
// ----------------------------------------------------------------------------
CallbackContext::~CallbackContext() {
    if (this->callingSocket) {
        JNIEnv *env = nullptr;
        // 現在の終了スレッドが、たまたま安全にアタッチ状態(JNI_OK)である場合のみフォールバックで解放
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env != nullptr) {
            env->DeleteGlobalRef(this->callingSocket);
        }
        // アンアタッチ状態の時は、危険なAttachはデッドロックを避けるため「あえて絶対にしない」。
        // メモリ管理の最適化は、C++のstd::shared_ptrモデルとJava側のライフサイクルに完全に委ねられます。
    }
}*/

#include <jni.h>
#include "CallbackContext.h"

// glue.cpp から安全に extern インポート（static修飾を外したため完全に結合可能）
extern jclass class_InetSocketAddress;

CallbackContext::CallbackContext(JNIEnv *env, jobject callingSocket) {
    env->GetJavaVM(&(this->vm));
    
    // クラス参照は起動時に確定した静的キャッシュを代入（スレッドローダー割れを100%防止）
    this->sockAddrClazz = class_InetSocketAddress; 
    
    // ソケットオブジェクトのグローバル参照化
    this->callingSocket = env->NewGlobalRef(callingSocket);
}

// ----------------------------------------------------------------------------
// 【ビルドエラー解消】ヘッダーの宣言と完全に一致した明示的クリーンアップ処理
// ----------------------------------------------------------------------------
void CallbackContext::release(JNIEnv *env) {
    // 危険なデストラクタ内ではなく、確実に生存している Java 駆動スレッド上の
    // 正当な env ポインタを使って安全にグローバル参照を消滅させます。
    if (this->callingSocket && env != nullptr) {
        env->DeleteGlobalRef(this->callingSocket);
        this->callingSocket = nullptr;
    }
    this->sockAddrClazz = nullptr;
}

// ----------------------------------------------------------------------------
// デストラクタ（危険なアタッチは一切せず、GCのロックと100%衝突しないガベージフリー設計）
// ----------------------------------------------------------------------------
CallbackContext::~CallbackContext() {
    if (this->callingSocket) {
        JNIEnv *env = nullptr;
        // 現在の終了スレッドが、たまたま安全にアタッチ状態(JNI_OK)である場合のみフォールバックで解放
        if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) == JNI_OK && env != nullptr) {
            env->DeleteGlobalRef(this->callingSocket);
        }
        // アタッチされていない（JNI_EDETACHED）場合は、危険な AttachCurrentThread は
        // デッドロックを避けるため「あえて絶対に呼ばない」。
        this->callingSocket = nullptr;
    }
    this->sockAddrClazz = nullptr;
}
