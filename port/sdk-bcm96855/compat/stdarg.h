/* SPDX-License-Identifier: GPL-2.0 */
/*
 * compat/stdarg.h —— 给厂商 SDK 头补上 <stdarg.h>
 *
 * bdmf_session.h（被 runner.c 间接包含）写了 #include <stdarg.h>。
 * 内核模块编译用 -nostdinc，编译器自带的 stdarg.h 不可见；
 * 内核自己的 linux/stdarg.h 提供 va_list/va_start/va_end，等价。
 */
#ifndef __HG3142F_COMPAT_STDARG_H
#define __HG3142F_COMPAT_STDARG_H

#include <linux/stdarg.h>

#endif /* __HG3142F_COMPAT_STDARG_H */
