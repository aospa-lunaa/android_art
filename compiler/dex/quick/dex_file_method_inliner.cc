/*
 * Copyright (C) 2013 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "dex_file_method_inliner.h"

#include <algorithm>

#include "base/logging.h"
#include "base/macros.h"
#include "base/mutex-inl.h"
#include "driver/compiler_driver.h"
#include "thread-inl.h"
#include "dex_instruction-inl.h"
#include "driver/dex_compilation_unit.h"
#include "verifier/method_verifier-inl.h"

namespace art {

namespace {  // anonymous namespace

static constexpr bool kIntrinsicIsStatic[] = {
    true,   // kIntrinsicDoubleCvt
    true,   // kIntrinsicFloatCvt
    true,   // kIntrinsicFloat2Int
    true,   // kIntrinsicDouble2Long
    true,   // kIntrinsicFloatIsInfinite
    true,   // kIntrinsicDoubleIsInfinite
    true,   // kIntrinsicFloatIsNaN
    true,   // kIntrinsicDoubleIsNaN
    true,   // kIntrinsicReverseBits
    true,   // kIntrinsicReverseBytes
    true,   // kIntrinsicBitCount
    true,   // kIntrinsicCompare,
    true,   // kIntrinsicHighestOneBit
    true,   // kIntrinsicLowestOneBit
    true,   // kIntrinsicNumberOfLeadingZeros
    true,   // kIntrinsicNumberOfTrailingZeros
    true,   // kIntrinsicRotateRight
    true,   // kIntrinsicRotateLeft
    true,   // kIntrinsicSignum
    true,   // kIntrinsicAbsInt
    true,   // kIntrinsicAbsLong
    true,   // kIntrinsicAbsFloat
    true,   // kIntrinsicAbsDouble
    true,   // kIntrinsicMinMaxInt
    true,   // kIntrinsicMinMaxLong
    true,   // kIntrinsicMinMaxFloat
    true,   // kIntrinsicMinMaxDouble
    true,   // kIntrinsicCos
    true,   // kIntrinsicSin
    true,   // kIntrinsicAcos
    true,   // kIntrinsicAsin
    true,   // kIntrinsicAtan
    true,   // kIntrinsicAtan2
    true,   // kIntrinsicCbrt
    true,   // kIntrinsicCosh
    true,   // kIntrinsicExp
    true,   // kIntrinsicExpm1
    true,   // kIntrinsicHypot
    true,   // kIntrinsicLog
    true,   // kIntrinsicLog10
    true,   // kIntrinsicNextAfter
    true,   // kIntrinsicSinh
    true,   // kIntrinsicTan
    true,   // kIntrinsicTanh
    true,   // kIntrinsicSqrt
    true,   // kIntrinsicCeil
    true,   // kIntrinsicFloor
    true,   // kIntrinsicRint
    true,   // kIntrinsicRoundFloat
    true,   // kIntrinsicRoundDouble
    false,  // kIntrinsicReferenceGetReferent
    false,  // kIntrinsicCharAt
    false,  // kIntrinsicCompareTo
    false,  // kIntrinsicEquals
    false,  // kIntrinsicGetCharsNoCheck
    false,  // kIntrinsicIsEmptyOrLength
    false,  // kIntrinsicIndexOf
    true,   // kIntrinsicNewStringFromBytes
    true,   // kIntrinsicNewStringFromChars
    true,   // kIntrinsicNewStringFromString
    true,   // kIntrinsicCurrentThread
    true,   // kIntrinsicPeek
    true,   // kIntrinsicPoke
    false,  // kIntrinsicCas
    false,  // kIntrinsicUnsafeGet
    false,  // kIntrinsicUnsafePut
    false,  // kIntrinsicUnsafeGetAndAddInt,
    false,  // kIntrinsicUnsafeGetAndAddLong,
    false,  // kIntrinsicUnsafeGetAndSetInt,
    false,  // kIntrinsicUnsafeGetAndSetLong,
    false,  // kIntrinsicUnsafeGetAndSetObject,
    false,  // kIntrinsicUnsafeLoadFence,
    false,  // kIntrinsicUnsafeStoreFence,
    false,  // kIntrinsicUnsafeFullFence,
    true,   // kIntrinsicSystemArrayCopyCharArray
    true,   // kIntrinsicSystemArrayCopy
};
static_assert(arraysize(kIntrinsicIsStatic) == kInlineOpNop,
              "arraysize of kIntrinsicIsStatic unexpected");
static_assert(kIntrinsicIsStatic[kIntrinsicDoubleCvt], "DoubleCvt must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicFloatCvt], "FloatCvt must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicFloat2Int], "Float2Int must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicDouble2Long], "Double2Long must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicFloatIsInfinite], "FloatIsInfinite must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicDoubleIsInfinite], "DoubleIsInfinite must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicFloatIsNaN], "FloatIsNaN must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicDoubleIsNaN], "DoubleIsNaN must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicReverseBits], "ReverseBits must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicReverseBytes], "ReverseBytes must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicBitCount], "BitCount must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicCompare], "Compare must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicHighestOneBit], "HighestOneBit must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicLowestOneBit], "LowestOneBit  must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicNumberOfLeadingZeros],
              "NumberOfLeadingZeros must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicNumberOfTrailingZeros],
              "NumberOfTrailingZeros must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicRotateRight], "RotateRight must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicRotateLeft], "RotateLeft must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicSignum], "Signum must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAbsInt], "AbsInt must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAbsLong], "AbsLong must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAbsFloat], "AbsFloat must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAbsDouble], "AbsDouble must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicMinMaxInt], "MinMaxInt must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicMinMaxLong], "MinMaxLong must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicMinMaxFloat], "MinMaxFloat must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicMinMaxDouble], "MinMaxDouble must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicCos], "Cos must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicSin], "Sin must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAcos], "Acos must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAsin], "Asin must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAtan], "Atan must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicAtan2], "Atan2 must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicCbrt], "Cbrt must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicCosh], "Cosh must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicExp], "Exp must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicExpm1], "Expm1 must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicHypot], "Hypot must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicLog], "Log must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicLog10], "Log10 must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicNextAfter], "NextAfter must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicSinh], "Sinh must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicTan], "Tan must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicTanh], "Tanh must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicSqrt], "Sqrt must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicCeil], "Ceil must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicFloor], "Floor must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicRint], "Rint must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicRoundFloat], "RoundFloat must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicRoundDouble], "RoundDouble must be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicReferenceGetReferent], "Get must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicCharAt], "CharAt must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicCompareTo], "CompareTo must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicEquals], "String equals must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicGetCharsNoCheck], "GetCharsNoCheck must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicIsEmptyOrLength], "IsEmptyOrLength must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicIndexOf], "IndexOf must not be static");
static_assert(kIntrinsicIsStatic[kIntrinsicNewStringFromBytes],
              "NewStringFromBytes must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicNewStringFromChars],
              "NewStringFromChars must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicNewStringFromString],
              "NewStringFromString must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicCurrentThread], "CurrentThread must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicPeek], "Peek must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicPoke], "Poke must be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicCas], "Cas must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeGet], "UnsafeGet must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafePut], "UnsafePut must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeGetAndAddInt], "UnsafeGetAndAddInt must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeGetAndAddLong], "UnsafeGetAndAddLong must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeGetAndSetInt], "UnsafeGetAndSetInt must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeGetAndSetLong], "UnsafeGetAndSetLong must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeGetAndSetObject], "UnsafeGetAndSetObject must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeLoadFence], "UnsafeLoadFence must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeStoreFence], "UnsafeStoreFence must not be static");
static_assert(!kIntrinsicIsStatic[kIntrinsicUnsafeFullFence], "UnsafeFullFence must not be static");
static_assert(kIntrinsicIsStatic[kIntrinsicSystemArrayCopyCharArray],
              "SystemArrayCopyCharArray must be static");
static_assert(kIntrinsicIsStatic[kIntrinsicSystemArrayCopy],
              "SystemArrayCopy must be static");

}  // anonymous namespace

const uint32_t DexFileMethodInliner::kIndexUnresolved;
const char* const DexFileMethodInliner::kClassCacheNames[] = {
    "Z",                       // kClassCacheBoolean
    "B",                       // kClassCacheByte
    "C",                       // kClassCacheChar
    "S",                       // kClassCacheShort
    "I",                       // kClassCacheInt
    "J",                       // kClassCacheLong
    "F",                       // kClassCacheFloat
    "D",                       // kClassCacheDouble
    "V",                       // kClassCacheVoid
    "[B",                      // kClassCacheJavaLangByteArray
    "[C",                      // kClassCacheJavaLangCharArray
    "[I",                      // kClassCacheJavaLangIntArray
    "Ljava/lang/Object;",      // kClassCacheJavaLangObject
    "Ljava/lang/ref/Reference;",   // kClassCacheJavaLangRefReference
    "Ljava/lang/String;",      // kClassCacheJavaLangString
    "Ljava/lang/StringBuffer;",    // kClassCacheJavaLangStringBuffer
    "Ljava/lang/StringBuilder;",   // kClassCacheJavaLangStringBuilder
    "Ljava/lang/StringFactory;",   // kClassCacheJavaLangStringFactory
    "Ljava/lang/Double;",      // kClassCacheJavaLangDouble
    "Ljava/lang/Float;",       // kClassCacheJavaLangFloat
    "Ljava/lang/Integer;",     // kClassCacheJavaLangInteger
    "Ljava/lang/Long;",        // kClassCacheJavaLangLong
    "Ljava/lang/Short;",       // kClassCacheJavaLangShort
    "Ljava/lang/Math;",        // kClassCacheJavaLangMath
    "Ljava/lang/StrictMath;",  // kClassCacheJavaLangStrictMath
    "Ljava/lang/Thread;",      // kClassCacheJavaLangThread
    "Ljava/nio/charset/Charset;",  // kClassCacheJavaNioCharsetCharset
    "Llibcore/io/Memory;",     // kClassCacheLibcoreIoMemory
    "Lsun/misc/Unsafe;",       // kClassCacheSunMiscUnsafe
    "Ljava/lang/System;",      // kClassCacheJavaLangSystem
};

const char* const DexFileMethodInliner::kNameCacheNames[] = {
    "reverse",               // kNameCacheReverse
    "reverseBytes",          // kNameCacheReverseBytes
    "doubleToRawLongBits",   // kNameCacheDoubleToRawLongBits
    "longBitsToDouble",      // kNameCacheLongBitsToDouble
    "floatToRawIntBits",     // kNameCacheFloatToRawIntBits
    "intBitsToFloat",        // kNameCacheIntBitsToFloat
    "abs",                   // kNameCacheAbs
    "max",                   // kNameCacheMax
    "min",                   // kNameCacheMin
    "cos",                   // kNameCacheCos
    "sin",                   // kNameCacheSin
    "acos",                  // kNameCacheAcos
    "asin",                  // kNameCacheAsin
    "atan",                  // kNameCacheAtan
    "atan2",                 // kNameCacheAtan2
    "cbrt",                  // kNameCacheCbrt
    "cosh",                  // kNameCacheCosh
    "exp",                   // kNameCacheExp
    "expm1",                 // kNameCacheExpm1
    "hypot",                 // kNameCacheHypot
    "log",                   // kNameCacheLog
    "log10",                 // kNameCacheLog10
    "nextAfter",             // kNameCacheNextAfter
    "sinh",                  // kNameCacheSinh
    "tan",                   // kNameCacheTan
    "tanh",                  // kNameCacheTanh
    "sqrt",                  // kNameCacheSqrt
    "ceil",                  // kNameCacheCeil
    "floor",                 // kNameCacheFloor
    "rint",                  // kNameCacheRint
    "round",                 // kNameCacheRound
    "getReferent",           // kNameCacheReferenceGet
    "charAt",                // kNameCacheCharAt
    "compareTo",             // kNameCacheCompareTo
    "equals",                // kNameCacheEquals
    "getCharsNoCheck",       // kNameCacheGetCharsNoCheck
    "isEmpty",               // kNameCacheIsEmpty
    "floatToIntBits",        // kNameCacheFloatToIntBits
    "doubleToLongBits",      // kNameCacheDoubleToLongBits
    "isInfinite",            // kNameCacheIsInfinite
    "isNaN",                 // kNameCacheIsNaN
    "indexOf",               // kNameCacheIndexOf
    "length",                // kNameCacheLength
    "<init>",                // kNameCacheInit
    "newStringFromBytes",    // kNameCacheNewStringFromBytes
    "newStringFromChars",    // kNameCacheNewStringFromChars
    "newStringFromString",   // kNameCacheNewStringFromString
    "currentThread",         // kNameCacheCurrentThread
    "peekByte",              // kNameCachePeekByte
    "peekIntNative",         // kNameCachePeekIntNative
    "peekLongNative",        // kNameCachePeekLongNative
    "peekShortNative",       // kNameCachePeekShortNative
    "pokeByte",              // kNameCachePokeByte
    "pokeIntNative",         // kNameCachePokeIntNative
    "pokeLongNative",        // kNameCachePokeLongNative
    "pokeShortNative",       // kNameCachePokeShortNative
    "compareAndSwapInt",     // kNameCacheCompareAndSwapInt
    "compareAndSwapLong",    // kNameCacheCompareAndSwapLong
    "compareAndSwapObject",  // kNameCacheCompareAndSwapObject
    "getInt",                // kNameCacheGetInt
    "getIntVolatile",        // kNameCacheGetIntVolatile
    "putInt",                // kNameCachePutInt
    "putIntVolatile",        // kNameCachePutIntVolatile
    "putOrderedInt",         // kNameCachePutOrderedInt
    "getLong",               // kNameCacheGetLong
    "getLongVolatile",       // kNameCacheGetLongVolatile
    "putLong",               // kNameCachePutLong
    "putLongVolatile",       // kNameCachePutLongVolatile
    "putOrderedLong",        // kNameCachePutOrderedLong
    "getObject",             // kNameCacheGetObject
    "getObjectVolatile",     // kNameCacheGetObjectVolatile
    "putObject",             // kNameCachePutObject
    "putObjectVolatile",     // kNameCachePutObjectVolatile
    "putOrderedObject",      // kNameCachePutOrderedObject
    "getAndAddInt",          // kNameCacheGetAndAddInt,
    "getAndAddLong",         // kNameCacheGetAndAddLong,
    "getAndSetInt",          // kNameCacheGetAndSetInt,
    "getAndSetLong",         // kNameCacheGetAndSetLong,
    "getAndSetObject",       // kNameCacheGetAndSetObject,
    "loadFence",             // kNameCacheLoadFence,
    "storeFence",            // kNameCacheStoreFence,
    "fullFence",             // kNameCacheFullFence,
    "arraycopy",             // kNameCacheArrayCopy
    "bitCount",              // kNameCacheBitCount
    "compare",               // kNameCacheCompare
    "highestOneBit",         // kNameCacheHighestOneBit
    "lowestOneBit",          // kNameCacheLowestOneBit
    "numberOfLeadingZeros",  // kNameCacheNumberOfLeadingZeros
    "numberOfTrailingZeros",  // kNameCacheNumberOfTrailingZeros
    "rotateRight",           // kNameCacheRotateRight
    "rotateLeft",            // kNameCacheRotateLeft
    "signum",                // kNameCacheSignum
};

const DexFileMethodInliner::ProtoDef DexFileMethodInliner::kProtoCacheDefs[] = {
    // kProtoCacheI_I
    { kClassCacheInt, 1, { kClassCacheInt } },
    // kProtoCacheJ_J
    { kClassCacheLong, 1, { kClassCacheLong } },
    // kProtoCacheS_S
    { kClassCacheShort, 1, { kClassCacheShort } },
    // kProtoCacheD_D
    { kClassCacheDouble, 1, { kClassCacheDouble } },
    // kProtoCacheDD_D
    { kClassCacheDouble, 2, { kClassCacheDouble, kClassCacheDouble } },
    // kProtoCacheF_F
    { kClassCacheFloat, 1, { kClassCacheFloat } },
    // kProtoCacheFF_F
    { kClassCacheFloat, 2, { kClassCacheFloat, kClassCacheFloat } },
    // kProtoCacheD_J
    { kClassCacheLong, 1, { kClassCacheDouble } },
    // kProtoCacheD_Z
    { kClassCacheBoolean, 1, { kClassCacheDouble } },
    // kProtoCacheJ_D
    { kClassCacheDouble, 1, { kClassCacheLong } },
    // kProtoCacheF_I
    { kClassCacheInt, 1, { kClassCacheFloat } },
    // kProtoCacheF_Z
    { kClassCacheBoolean, 1, { kClassCacheFloat } },
    // kProtoCacheI_F
    { kClassCacheFloat, 1, { kClassCacheInt } },
    // kProtoCacheII_I
    { kClassCacheInt, 2, { kClassCacheInt, kClassCacheInt } },
    // kProtoCacheI_C
    { kClassCacheChar, 1, { kClassCacheInt } },
    // kProtoCacheString_I
    { kClassCacheInt, 1, { kClassCacheJavaLangString } },
    // kProtoCache_Z
    { kClassCacheBoolean, 0, { } },
    // kProtoCache_I
    { kClassCacheInt, 0, { } },
    // kProtoCache_Object
    { kClassCacheJavaLangObject, 0, { } },
    // kProtoCache_Thread
    { kClassCacheJavaLangThread, 0, { } },
    // kProtoCacheJ_B
    { kClassCacheByte, 1, { kClassCacheLong } },
    // kProtoCacheJ_I
    { kClassCacheInt, 1, { kClassCacheLong } },
    // kProtoCacheJ_S
    { kClassCacheShort, 1, { kClassCacheLong } },
    // kProtoCacheJB_V
    { kClassCacheVoid, 2, { kClassCacheLong, kClassCacheByte } },
    // kProtoCacheJI_V
    { kClassCacheVoid, 2, { kClassCacheLong, kClassCacheInt } },
    // kProtoCacheJJ_J
    { kClassCacheLong, 2, { kClassCacheLong, kClassCacheLong } },
    // kProtoCacheJJ_I
    { kClassCacheInt, 2, { kClassCacheLong, kClassCacheLong } },
    // kProtoCacheJJ_V
    { kClassCacheVoid, 2, { kClassCacheLong, kClassCacheLong } },
    // kProtoCacheJS_V
    { kClassCacheVoid, 2, { kClassCacheLong, kClassCacheShort } },
    // kProtoCacheObject_Z
    { kClassCacheBoolean, 1, { kClassCacheJavaLangObject } },
    // kProtoCacheJI_J
    { kClassCacheLong, 2, { kClassCacheLong, kClassCacheInt } },
    // kProtoCacheObjectJII_Z
    { kClassCacheBoolean, 4, { kClassCacheJavaLangObject, kClassCacheLong,
        kClassCacheInt, kClassCacheInt } },
    // kProtoCacheObjectJJJ_Z
    { kClassCacheBoolean, 4, { kClassCacheJavaLangObject, kClassCacheLong,
        kClassCacheLong, kClassCacheLong } },
    // kProtoCacheObjectJObjectObject_Z
    { kClassCacheBoolean, 4, { kClassCacheJavaLangObject, kClassCacheLong,
        kClassCacheJavaLangObject, kClassCacheJavaLangObject } },
    // kProtoCacheObjectJ_I
    { kClassCacheInt, 2, { kClassCacheJavaLangObject, kClassCacheLong } },
    // kProtoCacheObjectJI_I
    { kClassCacheInt, 3, { kClassCacheJavaLangObject, kClassCacheLong, kClassCacheInt } },
    // kProtoCacheObjectJI_V
    { kClassCacheVoid, 3, { kClassCacheJavaLangObject, kClassCacheLong, kClassCacheInt } },
    // kProtoCacheObjectJ_J
    { kClassCacheLong, 2, { kClassCacheJavaLangObject, kClassCacheLong } },
    // kProtoCacheObjectJJ_J
    { kClassCacheLong, 3, { kClassCacheJavaLangObject, kClassCacheLong, kClassCacheLong } },
    // kProtoCacheObjectJJ_V
    { kClassCacheVoid, 3, { kClassCacheJavaLangObject, kClassCacheLong, kClassCacheLong } },
    // kProtoCacheObjectJ_Object
    { kClassCacheJavaLangObject, 2, { kClassCacheJavaLangObject, kClassCacheLong } },
    // kProtoCacheObjectJObject_V
    { kClassCacheVoid, 3, { kClassCacheJavaLangObject, kClassCacheLong,
        kClassCacheJavaLangObject } },
    // kProtoCacheObjectJObject_Object
    { kClassCacheJavaLangObject, 3, { kClassCacheJavaLangObject, kClassCacheLong,
        kClassCacheJavaLangObject } },
    // kProtoCacheCharArrayICharArrayII_V
    { kClassCacheVoid, 5, {kClassCacheJavaLangCharArray, kClassCacheInt,
        kClassCacheJavaLangCharArray, kClassCacheInt, kClassCacheInt} },
    // kProtoCacheObjectIObjectII_V
    { kClassCacheVoid, 5, {kClassCacheJavaLangObject, kClassCacheInt,
        kClassCacheJavaLangObject, kClassCacheInt, kClassCacheInt} },
    // kProtoCacheIICharArrayI_V
    { kClassCacheVoid, 4, { kClassCacheInt, kClassCacheInt, kClassCacheJavaLangCharArray,
        kClassCacheInt } },
    // kProtoCacheByteArrayIII_String
    { kClassCacheJavaLangString, 4, { kClassCacheJavaLangByteArray, kClassCacheInt, kClassCacheInt,
        kClassCacheInt } },
    // kProtoCacheIICharArray_String
    { kClassCacheJavaLangString, 3, { kClassCacheInt, kClassCacheInt,
        kClassCacheJavaLangCharArray } },
    // kProtoCacheString_String
    { kClassCacheJavaLangString, 1, { kClassCacheJavaLangString } },
    // kProtoCache_V
    { kClassCacheVoid, 0, { } },
    // kProtoCacheByteArray_V
    { kClassCacheVoid, 1, { kClassCacheJavaLangByteArray } },
    // kProtoCacheByteArrayI_V
    { kClassCacheVoid, 2, { kClassCacheJavaLangByteArray, kClassCacheInt } },
    // kProtoCacheByteArrayII_V
    { kClassCacheVoid, 3, { kClassCacheJavaLangByteArray, kClassCacheInt, kClassCacheInt } },
    // kProtoCacheByteArrayIII_V
    { kClassCacheVoid, 4, { kClassCacheJavaLangByteArray, kClassCacheInt, kClassCacheInt,
        kClassCacheInt } },
    // kProtoCacheByteArrayIIString_V
    { kClassCacheVoid, 4, { kClassCacheJavaLangByteArray, kClassCacheInt, kClassCacheInt,
        kClassCacheJavaLangString } },
    // kProtoCacheByteArrayString_V
    { kClassCacheVoid, 2, { kClassCacheJavaLangByteArray, kClassCacheJavaLangString } },
    // kProtoCacheByteArrayIICharset_V
    { kClassCacheVoid, 4, { kClassCacheJavaLangByteArray, kClassCacheInt, kClassCacheInt,
        kClassCacheJavaNioCharsetCharset } },
    // kProtoCacheByteArrayCharset_V
    { kClassCacheVoid, 2, { kClassCacheJavaLangByteArray, kClassCacheJavaNioCharsetCharset } },
    // kProtoCacheCharArray_V
    { kClassCacheVoid, 1, { kClassCacheJavaLangCharArray } },
    // kProtoCacheCharArrayII_V
    { kClassCacheVoid, 3, { kClassCacheJavaLangCharArray, kClassCacheInt, kClassCacheInt } },
    // kProtoCacheIICharArray_V
    { kClassCacheVoid, 3, { kClassCacheInt, kClassCacheInt, kClassCacheJavaLangCharArray } },
    // kProtoCacheIntArrayII_V
    { kClassCacheVoid, 3, { kClassCacheJavaLangIntArray, kClassCacheInt, kClassCacheInt } },
    // kProtoCacheString_V
    { kClassCacheVoid, 1, { kClassCacheJavaLangString } },
    // kProtoCacheStringBuffer_V
    { kClassCacheVoid, 1, { kClassCacheJavaLangStringBuffer } },
    // kProtoCacheStringBuilder_V
    { kClassCacheVoid, 1, { kClassCacheJavaLangStringBuilder } },
};

const DexFileMethodInliner::IntrinsicDef DexFileMethodInliner::kIntrinsicMethods[] = {
#define INTRINSIC(c, n, p, o, d) \
    { { kClassCache ## c, kNameCache ## n, kProtoCache ## p }, { o, kInlineIntrinsic, { d } } }

    INTRINSIC(JavaLangDouble, DoubleToRawLongBits, D_J, kIntrinsicDoubleCvt, 0),
    INTRINSIC(JavaLangDouble, LongBitsToDouble, J_D, kIntrinsicDoubleCvt, kIntrinsicFlagToFloatingPoint),
    INTRINSIC(JavaLangFloat, FloatToRawIntBits, F_I, kIntrinsicFloatCvt, 0),
    INTRINSIC(JavaLangFloat, IntBitsToFloat, I_F, kIntrinsicFloatCvt, kIntrinsicFlagToFloatingPoint),

    INTRINSIC(JavaLangFloat, FloatToIntBits, F_I, kIntrinsicFloat2Int, 0),
    INTRINSIC(JavaLangDouble, DoubleToLongBits, D_J, kIntrinsicDouble2Long, 0),

    INTRINSIC(JavaLangFloat, IsInfinite, F_Z, kIntrinsicFloatIsInfinite, 0),
    INTRINSIC(JavaLangDouble, IsInfinite, D_Z, kIntrinsicDoubleIsInfinite, 0),
    INTRINSIC(JavaLangFloat, IsNaN, F_Z, kIntrinsicFloatIsNaN, 0),
    INTRINSIC(JavaLangDouble, IsNaN, D_Z, kIntrinsicDoubleIsNaN, 0),

    INTRINSIC(JavaLangInteger, ReverseBytes, I_I, kIntrinsicReverseBytes, k32),
    INTRINSIC(JavaLangLong, ReverseBytes, J_J, kIntrinsicReverseBytes, k64),
    INTRINSIC(JavaLangShort, ReverseBytes, S_S, kIntrinsicReverseBytes, kSignedHalf),
    INTRINSIC(JavaLangInteger, Reverse, I_I, kIntrinsicReverseBits, k32),
    INTRINSIC(JavaLangLong, Reverse, J_J, kIntrinsicReverseBits, k64),

    INTRINSIC(JavaLangInteger, BitCount, I_I, kIntrinsicBitCount, k32),
    INTRINSIC(JavaLangLong, BitCount, J_I, kIntrinsicBitCount, k64),
    INTRINSIC(JavaLangInteger, Compare, II_I, kIntrinsicCompare, k32),
    INTRINSIC(JavaLangLong, Compare, JJ_I, kIntrinsicCompare, k64),
    INTRINSIC(JavaLangInteger, HighestOneBit, I_I, kIntrinsicHighestOneBit, k32),
    INTRINSIC(JavaLangLong, HighestOneBit, J_J, kIntrinsicHighestOneBit, k64),
    INTRINSIC(JavaLangInteger, LowestOneBit, I_I, kIntrinsicLowestOneBit, k32),
    INTRINSIC(JavaLangLong, LowestOneBit, J_J, kIntrinsicLowestOneBit, k64),
    INTRINSIC(JavaLangInteger, NumberOfLeadingZeros, I_I, kIntrinsicNumberOfLeadingZeros, k32),
    INTRINSIC(JavaLangLong, NumberOfLeadingZeros, J_I, kIntrinsicNumberOfLeadingZeros, k64),
    INTRINSIC(JavaLangInteger, NumberOfTrailingZeros, I_I, kIntrinsicNumberOfTrailingZeros, k32),
    INTRINSIC(JavaLangLong, NumberOfTrailingZeros, J_I, kIntrinsicNumberOfTrailingZeros, k64),
    INTRINSIC(JavaLangInteger, Signum, I_I, kIntrinsicSignum, k32),
    INTRINSIC(JavaLangLong, Signum, J_I, kIntrinsicSignum, k64),

    INTRINSIC(JavaLangMath,       Abs, I_I, kIntrinsicAbsInt, 0),
    INTRINSIC(JavaLangStrictMath, Abs, I_I, kIntrinsicAbsInt, 0),
    INTRINSIC(JavaLangMath,       Abs, J_J, kIntrinsicAbsLong, 0),
    INTRINSIC(JavaLangStrictMath, Abs, J_J, kIntrinsicAbsLong, 0),
    INTRINSIC(JavaLangMath,       Abs, F_F, kIntrinsicAbsFloat, 0),
    INTRINSIC(JavaLangStrictMath, Abs, F_F, kIntrinsicAbsFloat, 0),
    INTRINSIC(JavaLangMath,       Abs, D_D, kIntrinsicAbsDouble, 0),
    INTRINSIC(JavaLangStrictMath, Abs, D_D, kIntrinsicAbsDouble, 0),
    INTRINSIC(JavaLangMath,       Min, II_I, kIntrinsicMinMaxInt, kIntrinsicFlagMin),
    INTRINSIC(JavaLangStrictMath, Min, II_I, kIntrinsicMinMaxInt, kIntrinsicFlagMin),
    INTRINSIC(JavaLangMath,       Max, II_I, kIntrinsicMinMaxInt, kIntrinsicFlagMax),
    INTRINSIC(JavaLangStrictMath, Max, II_I, kIntrinsicMinMaxInt, kIntrinsicFlagMax),
    INTRINSIC(JavaLangMath,       Min, JJ_J, kIntrinsicMinMaxLong, kIntrinsicFlagMin),
    INTRINSIC(JavaLangStrictMath, Min, JJ_J, kIntrinsicMinMaxLong, kIntrinsicFlagMin),
    INTRINSIC(JavaLangMath,       Max, JJ_J, kIntrinsicMinMaxLong, kIntrinsicFlagMax),
    INTRINSIC(JavaLangStrictMath, Max, JJ_J, kIntrinsicMinMaxLong, kIntrinsicFlagMax),
    INTRINSIC(JavaLangMath,       Min, FF_F, kIntrinsicMinMaxFloat, kIntrinsicFlagMin),
    INTRINSIC(JavaLangStrictMath, Min, FF_F, kIntrinsicMinMaxFloat, kIntrinsicFlagMin),
    INTRINSIC(JavaLangMath,       Max, FF_F, kIntrinsicMinMaxFloat, kIntrinsicFlagMax),
    INTRINSIC(JavaLangStrictMath, Max, FF_F, kIntrinsicMinMaxFloat, kIntrinsicFlagMax),
    INTRINSIC(JavaLangMath,       Min, DD_D, kIntrinsicMinMaxDouble, kIntrinsicFlagMin),
    INTRINSIC(JavaLangStrictMath, Min, DD_D, kIntrinsicMinMaxDouble, kIntrinsicFlagMin),
    INTRINSIC(JavaLangMath,       Max, DD_D, kIntrinsicMinMaxDouble, kIntrinsicFlagMax),
    INTRINSIC(JavaLangStrictMath, Max, DD_D, kIntrinsicMinMaxDouble, kIntrinsicFlagMax),

    INTRINSIC(JavaLangMath,       Cos, D_D, kIntrinsicCos, 0),
    INTRINSIC(JavaLangMath,       Sin, D_D, kIntrinsicSin, 0),
    INTRINSIC(JavaLangMath,       Acos, D_D, kIntrinsicAcos, 0),
    INTRINSIC(JavaLangMath,       Asin, D_D, kIntrinsicAsin, 0),
    INTRINSIC(JavaLangMath,       Atan, D_D, kIntrinsicAtan, 0),
    INTRINSIC(JavaLangMath,       Atan2, DD_D, kIntrinsicAtan2, 0),
    INTRINSIC(JavaLangMath,       Cbrt, D_D, kIntrinsicCbrt, 0),
    INTRINSIC(JavaLangMath,       Cosh, D_D, kIntrinsicCosh, 0),
    INTRINSIC(JavaLangMath,       Exp, D_D, kIntrinsicExp, 0),
    INTRINSIC(JavaLangMath,       Expm1, D_D, kIntrinsicExpm1, 0),
    INTRINSIC(JavaLangMath,       Hypot, DD_D, kIntrinsicHypot, 0),
    INTRINSIC(JavaLangMath,       Log, D_D, kIntrinsicLog, 0),
    INTRINSIC(JavaLangMath,       Log10, D_D, kIntrinsicLog10, 0),
    INTRINSIC(JavaLangMath,       NextAfter, DD_D, kIntrinsicNextAfter, 0),
    INTRINSIC(JavaLangMath,       Sinh, D_D, kIntrinsicSinh, 0),
    INTRINSIC(JavaLangMath,       Tan, D_D, kIntrinsicTan, 0),
    INTRINSIC(JavaLangMath,       Tanh, D_D, kIntrinsicTanh, 0),
    INTRINSIC(JavaLangMath,       Sqrt, D_D, kIntrinsicSqrt, 0),
    INTRINSIC(JavaLangStrictMath, Sqrt, D_D, kIntrinsicSqrt, 0),

    INTRINSIC(JavaLangMath,       Ceil, D_D, kIntrinsicCeil, 0),
    INTRINSIC(JavaLangStrictMath, Ceil, D_D, kIntrinsicCeil, 0),
    INTRINSIC(JavaLangMath,       Floor, D_D, kIntrinsicFloor, 0),
    INTRINSIC(JavaLangStrictMath, Floor, D_D, kIntrinsicFloor, 0),
    INTRINSIC(JavaLangMath,       Rint, D_D, kIntrinsicRint, 0),
    INTRINSIC(JavaLangStrictMath, Rint, D_D, kIntrinsicRint, 0),
    INTRINSIC(JavaLangMath,       Round, F_I, kIntrinsicRoundFloat, 0),
    INTRINSIC(JavaLangStrictMath, Round, F_I, kIntrinsicRoundFloat, 0),
    INTRINSIC(JavaLangMath,       Round, D_J, kIntrinsicRoundDouble, 0),
    INTRINSIC(JavaLangStrictMath, Round, D_J, kIntrinsicRoundDouble, 0),

    INTRINSIC(JavaLangRefReference, ReferenceGetReferent, _Object, kIntrinsicReferenceGetReferent, 0),

    INTRINSIC(JavaLangString, CharAt, I_C, kIntrinsicCharAt, 0),
    INTRINSIC(JavaLangString, CompareTo, String_I, kIntrinsicCompareTo, 0),
    INTRINSIC(JavaLangString, Equals, Object_Z, kIntrinsicEquals, 0),
    INTRINSIC(JavaLangString, GetCharsNoCheck, IICharArrayI_V, kIntrinsicGetCharsNoCheck, 0),
    INTRINSIC(JavaLangString, IsEmpty, _Z, kIntrinsicIsEmptyOrLength, kIntrinsicFlagIsEmpty),
    INTRINSIC(JavaLangString, IndexOf, II_I, kIntrinsicIndexOf, kIntrinsicFlagNone),
    INTRINSIC(JavaLangString, IndexOf, I_I, kIntrinsicIndexOf, kIntrinsicFlagBase0),
    INTRINSIC(JavaLangString, Length, _I, kIntrinsicIsEmptyOrLength, kIntrinsicFlagLength),

    INTRINSIC(JavaLangStringFactory, NewStringFromBytes, ByteArrayIII_String,
              kIntrinsicNewStringFromBytes, kIntrinsicFlagNone),
    INTRINSIC(JavaLangStringFactory, NewStringFromChars, IICharArray_String,
              kIntrinsicNewStringFromChars, kIntrinsicFlagNone),
    INTRINSIC(JavaLangStringFactory, NewStringFromString, String_String,
              kIntrinsicNewStringFromString, kIntrinsicFlagNone),

    INTRINSIC(JavaLangThread, CurrentThread, _Thread, kIntrinsicCurrentThread, 0),

    INTRINSIC(LibcoreIoMemory, PeekByte, J_B, kIntrinsicPeek, kSignedByte),
    INTRINSIC(LibcoreIoMemory, PeekIntNative, J_I, kIntrinsicPeek, k32),
    INTRINSIC(LibcoreIoMemory, PeekLongNative, J_J, kIntrinsicPeek, k64),
    INTRINSIC(LibcoreIoMemory, PeekShortNative, J_S, kIntrinsicPeek, kSignedHalf),
    INTRINSIC(LibcoreIoMemory, PokeByte, JB_V, kIntrinsicPoke, kSignedByte),
    INTRINSIC(LibcoreIoMemory, PokeIntNative, JI_V, kIntrinsicPoke, k32),
    INTRINSIC(LibcoreIoMemory, PokeLongNative, JJ_V, kIntrinsicPoke, k64),
    INTRINSIC(LibcoreIoMemory, PokeShortNative, JS_V, kIntrinsicPoke, kSignedHalf),

    INTRINSIC(SunMiscUnsafe, CompareAndSwapInt, ObjectJII_Z, kIntrinsicCas,
              kIntrinsicFlagNone),
    INTRINSIC(SunMiscUnsafe, CompareAndSwapLong, ObjectJJJ_Z, kIntrinsicCas,
              kIntrinsicFlagIsLong),
    INTRINSIC(SunMiscUnsafe, CompareAndSwapObject, ObjectJObjectObject_Z, kIntrinsicCas,
              kIntrinsicFlagIsObject),

#define UNSAFE_GET_PUT(type, code, type_flags) \
    INTRINSIC(SunMiscUnsafe, Get ## type, ObjectJ_ ## code, kIntrinsicUnsafeGet, \
              type_flags), \
    INTRINSIC(SunMiscUnsafe, Get ## type ## Volatile, ObjectJ_ ## code, kIntrinsicUnsafeGet, \
              (type_flags) | kIntrinsicFlagIsVolatile), \
    INTRINSIC(SunMiscUnsafe, Put ## type, ObjectJ ## code ## _V, kIntrinsicUnsafePut, \
              type_flags), \
    INTRINSIC(SunMiscUnsafe, Put ## type ## Volatile, ObjectJ ## code ## _V, kIntrinsicUnsafePut, \
              (type_flags) | kIntrinsicFlagIsVolatile), \
    INTRINSIC(SunMiscUnsafe, PutOrdered ## type, ObjectJ ## code ## _V, kIntrinsicUnsafePut, \
              (type_flags) | kIntrinsicFlagIsOrdered)

    UNSAFE_GET_PUT(Int, I, kIntrinsicFlagNone),
    UNSAFE_GET_PUT(Long, J, kIntrinsicFlagIsLong),
    UNSAFE_GET_PUT(Object, Object, kIntrinsicFlagIsObject),
#undef UNSAFE_GET_PUT

    // 1.8
    INTRINSIC(SunMiscUnsafe, GetAndAddInt, ObjectJI_I, kIntrinsicUnsafeGetAndAddInt, 0),
    INTRINSIC(SunMiscUnsafe, GetAndAddLong, ObjectJJ_J, kIntrinsicUnsafeGetAndAddLong, 0),
    INTRINSIC(SunMiscUnsafe, GetAndSetInt, ObjectJI_I, kIntrinsicUnsafeGetAndSetInt, 0),
    INTRINSIC(SunMiscUnsafe, GetAndSetLong, ObjectJJ_J, kIntrinsicUnsafeGetAndSetLong, 0),
    INTRINSIC(SunMiscUnsafe, GetAndSetObject, ObjectJObject_Object, kIntrinsicUnsafeGetAndSetObject, 0),
    INTRINSIC(SunMiscUnsafe, LoadFence, _V, kIntrinsicUnsafeLoadFence, 0),
    INTRINSIC(SunMiscUnsafe, StoreFence, _V, kIntrinsicUnsafeStoreFence, 0),
    INTRINSIC(SunMiscUnsafe, FullFence, _V, kIntrinsicUnsafeFullFence, 0),

    INTRINSIC(JavaLangSystem, ArrayCopy, CharArrayICharArrayII_V , kIntrinsicSystemArrayCopyCharArray,
              0),
    INTRINSIC(JavaLangSystem, ArrayCopy, ObjectIObjectII_V , kIntrinsicSystemArrayCopy,
              0),

    INTRINSIC(JavaLangInteger, RotateRight, II_I, kIntrinsicRotateRight, k32),
    INTRINSIC(JavaLangLong, RotateRight, JI_J, kIntrinsicRotateRight, k64),
    INTRINSIC(JavaLangInteger, RotateLeft, II_I, kIntrinsicRotateLeft, k32),
    INTRINSIC(JavaLangLong, RotateLeft, JI_J, kIntrinsicRotateLeft, k64),

#undef INTRINSIC

#define SPECIAL(c, n, p, o, d) \
    { { kClassCache ## c, kNameCache ## n, kProtoCache ## p }, { o, kInlineSpecial, { d } } }

    SPECIAL(JavaLangString, Init, _V, kInlineStringInit, 0),
    SPECIAL(JavaLangString, Init, ByteArray_V, kInlineStringInit, 1),
    SPECIAL(JavaLangString, Init, ByteArrayI_V, kInlineStringInit, 2),
    SPECIAL(JavaLangString, Init, ByteArrayII_V, kInlineStringInit, 3),
    SPECIAL(JavaLangString, Init, ByteArrayIII_V, kInlineStringInit, 4),
    SPECIAL(JavaLangString, Init, ByteArrayIIString_V, kInlineStringInit, 5),
    SPECIAL(JavaLangString, Init, ByteArrayString_V, kInlineStringInit, 6),
    SPECIAL(JavaLangString, Init, ByteArrayIICharset_V, kInlineStringInit, 7),
    SPECIAL(JavaLangString, Init, ByteArrayCharset_V, kInlineStringInit, 8),
    SPECIAL(JavaLangString, Init, CharArray_V, kInlineStringInit, 9),
    SPECIAL(JavaLangString, Init, CharArrayII_V, kInlineStringInit, 10),
    SPECIAL(JavaLangString, Init, IICharArray_V, kInlineStringInit, 11),
    SPECIAL(JavaLangString, Init, IntArrayII_V, kInlineStringInit, 12),
    SPECIAL(JavaLangString, Init, String_V, kInlineStringInit, 13),
    SPECIAL(JavaLangString, Init, StringBuffer_V, kInlineStringInit, 14),
    SPECIAL(JavaLangString, Init, StringBuilder_V, kInlineStringInit, 15),

#undef SPECIAL
};

DexFileMethodInliner::DexFileMethodInliner()
    : lock_("DexFileMethodInliner lock", kDexFileMethodInlinerLock),
      dex_file_(nullptr) {
  static_assert(kClassCacheFirst == 0, "kClassCacheFirst not 0");
  static_assert(arraysize(kClassCacheNames) == kClassCacheLast,
                "bad arraysize for kClassCacheNames");
  static_assert(kNameCacheFirst == 0, "kNameCacheFirst not 0");
  static_assert(arraysize(kNameCacheNames) == kNameCacheLast,
                "bad arraysize for kNameCacheNames");
  static_assert(kProtoCacheFirst == 0, "kProtoCacheFirst not 0");
  static_assert(arraysize(kProtoCacheDefs) == kProtoCacheLast,
                "bad arraysize kProtoCacheNames");
}

DexFileMethodInliner::~DexFileMethodInliner() {
}

bool DexFileMethodInliner::AnalyseMethodCode(verifier::MethodVerifier* verifier) {
  InlineMethod method;
  bool success = InlineMethodAnalyser::AnalyseMethodCode(verifier, &method);
  return success && AddInlineMethod(verifier->GetMethodReference().dex_method_index, method);
}

InlineMethodFlags DexFileMethodInliner::IsIntrinsicOrSpecial(uint32_t method_index) {
  ReaderMutexLock mu(Thread::Current(), lock_);
  auto it = inline_methods_.find(method_index);
  if (it != inline_methods_.end()) {
    DCHECK_NE(it->second.flags & (kInlineIntrinsic | kInlineSpecial), 0);
    return it->second.flags;
  } else {
    return kNoInlineMethodFlags;
  }
}

bool DexFileMethodInliner::IsIntrinsic(uint32_t method_index, InlineMethod* intrinsic) {
  ReaderMutexLock mu(Thread::Current(), lock_);
  auto it = inline_methods_.find(method_index);
  bool res = (it != inline_methods_.end() && (it->second.flags & kInlineIntrinsic) != 0);
  if (res && intrinsic != nullptr) {
    *intrinsic = it->second;
  }
  return res;
}

bool DexFileMethodInliner::IsSpecial(uint32_t method_index) {
  ReaderMutexLock mu(Thread::Current(), lock_);
  auto it = inline_methods_.find(method_index);
  return it != inline_methods_.end() && (it->second.flags & kInlineSpecial) != 0;
}

uint32_t DexFileMethodInliner::FindClassIndex(const DexFile* dex_file, IndexCache* cache,
                                              ClassCacheIndex index) {
  uint32_t* class_index = &cache->class_indexes[index];
  if (*class_index != kIndexUnresolved) {
    return *class_index;
  }

  const DexFile::TypeId* type_id = dex_file->FindTypeId(kClassCacheNames[index]);
  if (type_id == nullptr) {
    *class_index = kIndexNotFound;
    return *class_index;
  }
  *class_index = dex_file->GetIndexForTypeId(*type_id);
  return *class_index;
}

uint32_t DexFileMethodInliner::FindNameIndex(const DexFile* dex_file, IndexCache* cache,
                                             NameCacheIndex index) {
  uint32_t* name_index = &cache->name_indexes[index];
  if (*name_index != kIndexUnresolved) {
    return *name_index;
  }

  const DexFile::StringId* string_id = dex_file->FindStringId(kNameCacheNames[index]);
  if (string_id == nullptr) {
    *name_index = kIndexNotFound;
    return *name_index;
  }
  *name_index = dex_file->GetIndexForStringId(*string_id);
  return *name_index;
}

uint32_t DexFileMethodInliner::FindProtoIndex(const DexFile* dex_file, IndexCache* cache,
                                              ProtoCacheIndex index) {
  uint32_t* proto_index = &cache->proto_indexes[index];
  if (*proto_index != kIndexUnresolved) {
    return *proto_index;
  }

  const ProtoDef& proto_def = kProtoCacheDefs[index];
  uint32_t return_index = FindClassIndex(dex_file, cache, proto_def.return_type);
  if (return_index == kIndexNotFound) {
    *proto_index = kIndexNotFound;
    return *proto_index;
  }
  uint16_t return_type = static_cast<uint16_t>(return_index);
  DCHECK_EQ(static_cast<uint32_t>(return_type), return_index);

  uint32_t signature_length = proto_def.param_count;
  uint16_t signature_type_idxs[kProtoMaxParams];
  for (uint32_t i = 0; i != signature_length; ++i) {
    uint32_t param_index = FindClassIndex(dex_file, cache, proto_def.params[i]);
    if (param_index == kIndexNotFound) {
      *proto_index = kIndexNotFound;
      return *proto_index;
    }
    signature_type_idxs[i] = static_cast<uint16_t>(param_index);
    DCHECK_EQ(static_cast<uint32_t>(signature_type_idxs[i]), param_index);
  }

  const DexFile::ProtoId* proto_id = dex_file->FindProtoId(return_type, signature_type_idxs,
                                                           signature_length);
  if (proto_id == nullptr) {
    *proto_index = kIndexNotFound;
    return *proto_index;
  }
  *proto_index = dex_file->GetIndexForProtoId(*proto_id);
  return *proto_index;
}

uint32_t DexFileMethodInliner::FindMethodIndex(const DexFile* dex_file, IndexCache* cache,
                                               const MethodDef& method_def) {
  uint32_t declaring_class_index = FindClassIndex(dex_file, cache, method_def.declaring_class);
  if (declaring_class_index == kIndexNotFound) {
    return kIndexNotFound;
  }
  uint32_t name_index = FindNameIndex(dex_file, cache, method_def.name);
  if (name_index == kIndexNotFound) {
    return kIndexNotFound;
  }
  uint32_t proto_index = FindProtoIndex(dex_file, cache, method_def.proto);
  if (proto_index == kIndexNotFound) {
    return kIndexNotFound;
  }
  const DexFile::MethodId* method_id =
      dex_file->FindMethodId(dex_file->GetTypeId(declaring_class_index),
                             dex_file->GetStringId(name_index),
                             dex_file->GetProtoId(proto_index));
  if (method_id == nullptr) {
    return kIndexNotFound;
  }
  return dex_file->GetIndexForMethodId(*method_id);
}

DexFileMethodInliner::IndexCache::IndexCache() {
  std::fill_n(class_indexes, arraysize(class_indexes), kIndexUnresolved);
  std::fill_n(name_indexes, arraysize(name_indexes), kIndexUnresolved);
  std::fill_n(proto_indexes, arraysize(proto_indexes), kIndexUnresolved);
}

void DexFileMethodInliner::FindIntrinsics(const DexFile* dex_file) {
  DCHECK(dex_file != nullptr);
  DCHECK(dex_file_ == nullptr);
  IndexCache cache;
  for (const IntrinsicDef& def : kIntrinsicMethods) {
    uint32_t method_idx = FindMethodIndex(dex_file, &cache, def.method_def);
    if (method_idx != kIndexNotFound) {
      DCHECK(inline_methods_.find(method_idx) == inline_methods_.end());
      inline_methods_.Put(method_idx, def.intrinsic);
    }
  }
  dex_file_ = dex_file;
}

bool DexFileMethodInliner::AddInlineMethod(int32_t method_idx, const InlineMethod& method) {
  WriterMutexLock mu(Thread::Current(), lock_);
  if (LIKELY(inline_methods_.find(method_idx) == inline_methods_.end())) {
    inline_methods_.Put(method_idx, method);
    return true;
  } else {
    if (PrettyMethod(method_idx, *dex_file_) == "int java.lang.String.length()") {
      // TODO: String.length is both kIntrinsicIsEmptyOrLength and kInlineOpIGet.
    } else {
      LOG(WARNING) << "Inliner: " << PrettyMethod(method_idx, *dex_file_) << " already inline";
    }
    return false;
  }
}

uint32_t DexFileMethodInliner::GetOffsetForStringInit(uint32_t method_index,
                                                      PointerSize pointer_size) {
  ReaderMutexLock mu(Thread::Current(), lock_);
  auto it = inline_methods_.find(method_index);
  if (it != inline_methods_.end() && (it->second.opcode == kInlineStringInit)) {
    uint32_t string_init_base_offset = Thread::QuickEntryPointOffsetWithSize(
              OFFSETOF_MEMBER(QuickEntryPoints, pNewEmptyString), pointer_size);
    return string_init_base_offset + it->second.d.data * static_cast<size_t>(pointer_size);
  }
  return 0;
}

bool DexFileMethodInliner::IsStringInitMethodIndex(uint32_t method_index) {
  ReaderMutexLock mu(Thread::Current(), lock_);
  auto it = inline_methods_.find(method_index);
  return (it != inline_methods_.end()) && (it->second.opcode == kInlineStringInit);
}

}  // namespace art
