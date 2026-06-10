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
/*#pragma once

#include "Models.h"

class Primitive {
public:
    static jobject newJavaLong(JNIEnv *env, int64_t val) {
        jclass longClazz = env->FindClass(LONG_CLASS);
        if (!longClazz) {
            LOGE("Can't find Long class");
            return nullptr;
        }
        jmethodID longConstructorMethod = env->GetMethodID(longClazz, "<init>", "(J)V");
        if (!longConstructorMethod) {
            LOGE("Can't find Long constructor");
            return nullptr;
        }
        return env->NewObject(longClazz, longConstructorMethod, val);
    }

    static jobject newJavaBoolean(JNIEnv *env, bool val) {
        jclass boolClazz = env->FindClass(BOOLEAN_CLASS);
        if (!boolClazz) {
            LOGE("Can't find Boolean class");
            return nullptr;
        }
        jmethodID booleanConstructorMethod = env->GetMethodID(boolClazz, "<init>", "(Z)V");
        if (!booleanConstructorMethod) {
            LOGE("Can't find Boolean constructor");
            return nullptr;
        }
        return env->NewObject(boolClazz, booleanConstructorMethod, val);
    }

    static jobject newJavaInt(JNIEnv *env, int val) {
        jclass intClazz = env->FindClass(INT_CLASS);
        if (!intClazz) {
            LOGE("Can't find Integer class");
            return nullptr;
        }
        jmethodID integerConstructorMethod = env->GetMethodID(intClazz, "<init>", "(I)V");
        if (!integerConstructorMethod) {
            LOGE("Can't find Integer constructor");
            return nullptr;
        }
        return env->NewObject(intClazz, integerConstructorMethod, val);
    }

};*/

/*#pragma once

#include "Models.h"

class Primitive {
public:
    // キャッシュを保持する静的変数。JNI_OnLoadで初期化されます。
    static jclass cachedIntegerClazz;
    static jmethodID cachedValueOfMethod;

    static jobject newJavaInt(JNIEnv *env, jint value) {
        // 1. キャッシュが存在する場合は、Integer.valueOf(value) を最速ルートで実行
        if (cachedIntegerClazz && cachedValueOfMethod) {
            return env->CallStaticObjectMethod(cachedIntegerClazz, cachedValueOfMethod, value);
        }

        // 2. フォールバック（共存用）：初期化前でも元の挙動ベースで安全に動作
        jclass integerClazz = env->FindClass("java/lang/Integer");
        if (!integerClazz) {
            LOGE("Can't get Integer class");
            return nullptr;
        }

        // 互換性とパフォーマンス向上のため、コンストラクタではなく static な valueOf メソッドを取得
        jmethodID valueOfMethod = env->GetStaticMethodID(integerClazz, "valueOf", "(I)Ljava/lang/Integer;");
        if (!valueOfMethod) {
            LOGE("Can't get Integer.valueOf method");
            env->DeleteLocalRef(integerClazz);
            return nullptr;
        }

        jobject integerObj = env->CallStaticObjectMethod(integerClazz, valueOfMethod, value);
        env->DeleteLocalRef(integerClazz);
        return integerObj;
    }

    static jobject newJavaLong(JNIEnv *env, int64_t val) {
        jclass longClazz = env->FindClass(LONG_CLASS);
        if (!longClazz) {
            LOGE("Can't find Long class");
            return nullptr;
        }
        jmethodID longConstructorMethod = env->GetMethodID(longClazz, "<init>", "(J)V");
        if (!longConstructorMethod) {
            LOGE("Can't find Long constructor");
            return nullptr;
        }
        return env->NewObject(longClazz, longConstructorMethod, val);
    }

    static jobject newJavaBoolean(JNIEnv *env, bool val) {
        jclass boolClazz = env->FindClass(BOOLEAN_CLASS);
        if (!boolClazz) {
            LOGE("Can't find Boolean class");
            return nullptr;
        }
        jmethodID booleanConstructorMethod = env->GetMethodID(boolClazz, "<init>", "(Z)V");
        if (!booleanConstructorMethod) {
            LOGE("Can't find Boolean constructor");
            return nullptr;
        }
        return env->NewObject(boolClazz, booleanConstructorMethod, val);
    }
};*/

#pragma once

#include "Models.h"

class Primitive {
public:
    // glue.cpp 内の JNI_OnLoad で一括マッピングされる最速キャッシュID群
    static jclass    cachedIntegerClazz;
    static jmethodID cachedIntValueOfMethod;
    
    static jclass    cachedLongClazz;
    static jmethodID cachedLongValueOfMethod;
    
    static jclass    cachedBooleanClazz;
    static jmethodID cachedBoolValueOfMethod;

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

// 実体定義用スペース（cppファイル側で1度だけ実体化させるための宣言）
#ifdef INITIALIZE_PRIMITIVE_CACHE
jclass    Primitive::cachedIntegerClazz = nullptr;
jmethodID Primitive::cachedIntValueOfMethod = nullptr;
jclass    Primitive::cachedLongClazz = nullptr;
jmethodID Primitive::cachedLongValueOfMethod = nullptr;
jclass    Primitive::cachedBooleanClazz = nullptr;
jmethodID Primitive::cachedBoolValueOfMethod = nullptr;
#endif
