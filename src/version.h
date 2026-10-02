// アプリの版。ここだけを直す（res/TLIV.rc、About、インストーラーはこれを読む）。
// .rc からも読まれるので、書いてよいのは #define だけ
#pragma once
#define TLIV_VER_MAJOR 1
#define TLIV_VER_MINOR 0
#define TLIV_VER_PATCH 0
#define TLIV_VER_STR "1.0.0"

#ifndef RC_INVOKED
#define TLIV_WIDEN_(x) L##x
#define TLIV_WIDEN(x) TLIV_WIDEN_(x)
#define TLIV_VER_WSTR TLIV_WIDEN(TLIV_VER_STR)
#endif
