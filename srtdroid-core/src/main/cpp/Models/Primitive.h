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
#pragma once

#include "Models.h"

class Primitive {
public:
    // glue.cpp 内の JNI_OnLoad で一括マッピングされる最速キャッシュID群
    // ------------------------------------------------------------------------
    // 【解決策】inline を付与することで、.cpp ファイルを作らずに実体を確定させる
    // ------------------------------------------------------------------------
    static inline jclass    cachedIntegerClazz = nullptr;
    static inline jmethodID cachedIntValueOfMethod = nullptr;
    
    static inline jclass    cachedLongClazz = nullptr;
    static inline jmethodID cachedLongValueOfMethod = nullptr;
    
    static inline jclass    cachedBooleanClazz = nullptr;
    static inline jmethodID cachedBoolValueOfMethod = nullptr;

    static jobject newJavaInt(JNIEnv *env, jint value) {
        if (cachedIntegerClazz && cachedIntValueOfMethod) {
            return env->CallStaticObjectMethod(cachedIntegerClazz, cachedIntValueOfMethod, value);
        }
        // 予期せぬ未初期化時の安全フォールバック
        jclass integerClazz = env->FindClass("java/lang/Integer");
        if (!integerClazz) return nullptr;
        jmethodID valueOfMethod = env->GetStaticMethodID(integerClazz, "valueOf", "(I)Ljava/lang/Integer;");
        jobject integerObj = env->CallStaticObjectMethod(integerClazz, valueOfMethod, value);
        env->DeleteLocalRef(integerClazz);
        return integerObj;
    }

    static jobject newJavaLong(JNIEnv *env, int64_t val) {
        if (cachedLongClazz && cachedLongValueOfMethod) {
            return env->CallStaticObjectMethod(cachedLongClazz, cachedLongValueOfMethod, static_cast<jlong>(val));
        }
        jclass longClazz = env->FindClass("java/lang/Long");
        if (!longClazz) return nullptr;
        jmethodID valueOfMethod = env->GetStaticMethodID(longClazz, "valueOf", "(J)Ljava/lang/Long;");
        jobject longObj = env->CallStaticObjectMethod(longClazz, valueOfMethod, static_cast<jlong>(val));
        env->DeleteLocalRef(longClazz);
        return longObj;
    }

    static jobject newJavaBoolean(JNIEnv *env, bool val) {
        if (cachedBooleanClazz && cachedBoolValueOfMethod) {
            return env->CallStaticObjectMethod(cachedBooleanClazz, cachedBoolValueOfMethod, val ? JNI_TRUE : JNI_FALSE);
        }
        jclass boolClazz = env->FindClass("java/lang/Boolean");
        if (!boolClazz) return nullptr;
        jmethodID valueOfMethod = env->GetStaticMethodID(boolClazz, "valueOf", "(Z)Ljava/lang/Boolean;");
        jobject boolObj = env->CallStaticObjectMethod(boolClazz, valueOfMethod, val ? JNI_TRUE : JNI_FALSE);
        env->DeleteLocalRef(boolClazz);
        return boolObj;
    }
};
