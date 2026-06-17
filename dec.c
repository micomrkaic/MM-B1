/*
 * MM-B1 -- a decimal business RPN calculator.
 *
 * Copyright (C) 2026 Mico Mrkaic
 * SPDX-License-Identifier: GPL-3.0-or-later
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

/* dec.c -- decimal context, parsing, formatting, error channel. */
#include "bc.h"
#include <stdarg.h>
#include <stdlib.h>

decContext g_ctx;
char       g_err[256];
int        g_dp = 2;

void dec_init(void)
{
    decContextDefault(&g_ctx, DEC_INIT_BASE);
    g_ctx.digits = 34;                 /* working precision (decimal128-class) */
    g_ctx.round  = DEC_ROUND_HALF_EVEN;/* banker's rounding -- the finance default */
    g_ctx.emax   =  999999;
    g_ctx.emin   = -999999;
    g_ctx.traps  = 0;                  /* never raise signals; we poll status */
    g_err[0] = '\0';
}

void set_err(const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(g_err, sizeof g_err, fmt, ap);
    va_end(ap);
}

/* Treat only "hard" conditions as errors; Inexact/Rounded are normal. */
int dec_check(const char *where)
{
    uint32_t s = g_ctx.status;
    uint32_t hard = DEC_Division_by_zero | DEC_Invalid_operation |
                    DEC_Overflow | DEC_Division_undefined | DEC_Division_impossible;
    if (s & hard) {
        set_err("%s: domain error", where);
        decContextZeroStatus(&g_ctx);
        return 1;
    }
    decContextZeroStatus(&g_ctx);
    return 0;
}

int parse_num(const char *tok, num *out)
{
    decContextZeroStatus(&g_ctx);
    decNumberFromString(out, tok, &g_ctx);
    if (g_ctx.status & DEC_Conversion_syntax) {   /* not numeric */
        decContextZeroStatus(&g_ctx);
        return 0;
    }
    decContextZeroStatus(&g_ctx);
    if (decNumberIsNaN(out) || decNumberIsInfinite(out)) return 0;
    return 1;
}

/* Fixed-point string with dp decimals, half-even rounding. */
void fmt_fixed(const num *a, int dp, char *out)
{
    num scaled, expo;
    n_int(&expo, -dp);                 /* requested exponent = -dp */
    decNumberRescale(&scaled, a, &expo, &g_ctx);
    if (decNumberIsZero(&scaled))      /* normalize -0 to 0 */
        scaled.bits &= ~DECNEG;
    decNumberToString(&scaled, out);
    decContextZeroStatus(&g_ctx);
}

/* Like fmt_fixed but with thousands separators in the integer part.
 * Cosmetic, for the live stack display; falls back to plain on E-notation. */
void fmt_money(const num *a, int dp, char *out)
{
    char tmp[160];
    fmt_fixed(a, dp, tmp);
    if (strchr(tmp, 'E') || strchr(tmp, 'e')) { strcpy(out, tmp); return; }

    const char *p = tmp;
    char sign = 0;
    if (*p == '-') { sign = '-'; p++; }

    const char *dot = strchr(p, '.');
    size_t ilen = dot ? (size_t)(dot - p) : strlen(p);

    char grouped[96]; size_t g = 0;
    for (size_t i = 0; i < ilen; i++) {
        if (i > 0 && (ilen - i) % 3 == 0) grouped[g++] = ',';
        grouped[g++] = p[i];
    }
    grouped[g] = '\0';

    char *o = out;
    if (sign) *o++ = sign;
    strcpy(o, grouped); o += g;
    if (dot) strcpy(o, dot);            /* ".dd" tail */
    else *o = '\0';
}
