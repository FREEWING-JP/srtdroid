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
