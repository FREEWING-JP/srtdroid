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

#include <android/log.h>

#ifdef __cplusplus
extern "C" {
#endif

#define  TAG    "srtdroid"
// Log tools
#ifdef NDEBUG
  // リリースビルド（NDEBUG定義時）の処理
    // デバッグ・情報は完全に無効化（コンパイル時に消滅）
    #define LOGD(...) ((void)0)
    #define LOGI(...) ((void)0)
    
    // 重大なエラーと警告のみリリース版でも残す（必要に応じて消去も可能）
    #define LOGW(...) __android_log_print(ANDROID_LOG_WARN,  LOG_TAG, __VA_ARGS__)
    #define LOGE(...) __android_log_print(ANDROID_LOG_ERROR, LOG_TAG, __VA_ARGS__)

#else
  // デバッグビルドの処理
  #define  LOGE(...)  __android_log_print(ANDROID_LOG_ERROR, TAG, __VA_ARGS__)
  #define  LOGW(...)  __android_log_print(ANDROID_LOG_WARN, TAG, __VA_ARGS__)
  #define  LOGD(...)  __android_log_print(ANDROID_LOG_DEBUG, TAG, __VA_ARGS__)
  #define  LOGI(...)  __android_log_print(ANDROID_LOG_INFO, TAG, __VA_ARGS__)

#endif

#ifdef __cplusplus
}
#endif
