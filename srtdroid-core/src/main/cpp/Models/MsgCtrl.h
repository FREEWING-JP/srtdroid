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

// 💡 glue.cpp 側（またはJNI_OnLoad）で管理されているグローバルキャッシュを外部参照
extern jfieldID msgCtrlFlagsField;
extern jfieldID msgCtrlTtlField;
extern jfieldID msgCtrlInorderField; // ※ glue.cpp 側の変数名（msgCtrlInorderField）に統一
extern jfieldID msgCtrlPktSeqField;
extern jfieldID msgCtrlMsgNumberField; // ※ glue.cpp 側の変数名（msgCtrlMsgNumberField）に統一

// 今回追加で必要となる、上部でキャッシュすべきフィールドIDの宣言
extern jfieldID msgCtrlBoundaryField;
extern jfieldID msgCtrlSrcTimeField;

class MsgCtrl {
public:
    static SRT_MSGCTRL *
    getNative(JNIEnv *env, jobject msgCtrl) {
        if (msgCtrl == nullptr)
            return nullptr;

        // 🔴【超弩級の無駄を排除】
        // 毎回呼ばれていた env->GetObjectClass や 7連続の env->GetFieldID を完全に撤廃。
        // 万が一、JNI_OnLoad等でのキャッシュが未完了な場合の安全ガードのみ配置します。
        if (!msgCtrlFlagsField || !msgCtrlTtlField || !msgCtrlInorderField) {
            LOGE("MsgCtrl fields are not initialized in JNI_OnLoad");
            return nullptr;
        }

        SRT_MSGCTRL *srt_msgctrl = (SRT_MSGCTRL *) malloc(sizeof(SRT_MSGCTRL));
        if (srt_msgctrl != nullptr) {
            // 💡 起動時に1度だけ検索済みの最速ポインタ(fieldID)でダイレクトにJavaの値を引く
            srt_msgctrl->flags = env->GetIntField(msgCtrl, msgCtrlFlagsField);
            srt_msgctrl->msgttl = env->GetIntField(msgCtrl, msgCtrlTtlField);
            srt_msgctrl->inorder = (env->GetBooleanField(msgCtrl, msgCtrlInorderField) == JNI_TRUE) ? 1 : 0;
            
            // boundary と srcTime もグローバルキャッシュから取得
            jobject jBoundary = env->GetObjectField(msgCtrl, msgCtrlBoundaryField);
            srt_msgctrl->boundary = EnumsSingleton::getInstance(env)->boundary->getNativeValue(env, jBoundary);
            if (jBoundary) env->DeleteLocalRef(jBoundary); // 局所オブジェクト参照の即時解放（無駄の排除）

            srt_msgctrl->srctime = (uint64_t) env->GetLongField(msgCtrl, msgCtrlSrcTimeField);
            srt_msgctrl->pktseq = env->GetIntField(msgCtrl, msgCtrlPktSeqField);
            srt_msgctrl->msgno = env->GetIntField(msgCtrl, msgCtrlMsgNumberField);
        }

        return srt_msgctrl;
    }
};
